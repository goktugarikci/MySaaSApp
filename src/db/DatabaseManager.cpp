#include "DatabaseManager.h"
#include "../utils/Security.h"
#include <iostream>

// Yardımcı Makro
#define SAFE_TEXT(col) (reinterpret_cast<const char*>(sqlite3_column_text(stmt, col)) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, col)) : "")

DatabaseManager::DatabaseManager(const std::string& path) : db_path(path), db(nullptr) {}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::open() {
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc) {
        std::cerr << "DB Hatasi: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    executeQuery("PRAGMA foreign_keys = ON;");
    return true;
}

void DatabaseManager::close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool DatabaseManager::executeQuery(const std::string& sql) {
    char* zErrMsg = 0;
    int rc = sqlite3_exec(db, sql.c_str(), 0, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL Exec Hatasi: " << zErrMsg << "\nSorgu: " << sql << std::endl;
        sqlite3_free(zErrMsg);
        return false;
    }
    return true;
}

// --- TABLO OLUŞTURMA (SCHEMA) ---
bool DatabaseManager::initTables() {
    std::string sql =
        "CREATE TABLE IF NOT EXISTS Users ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "Name TEXT NOT NULL, "
        "Email TEXT UNIQUE NOT NULL, "
        "PasswordHash TEXT NOT NULL, "
        "IsSystemAdmin INTEGER DEFAULT 0, "
        "Status TEXT DEFAULT 'Offline', "
        "AvatarURL TEXT, "
        "CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP);"

        "CREATE TABLE IF NOT EXISTS Servers ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "OwnerID INTEGER, "
        "Name TEXT NOT NULL, "
        "InviteCode TEXT UNIQUE, "
        "IconURL TEXT, "
        "FOREIGN KEY(OwnerID) REFERENCES Users(ID));"

        // EKLENDİ: Roller Tablosu
        "CREATE TABLE IF NOT EXISTS Roles ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "ServerID INTEGER, "
        "RoleName TEXT NOT NULL, "
        "Color TEXT DEFAULT '#FFFFFF', "
        "Hierarchy INTEGER DEFAULT 0, "
        "Permissions INTEGER DEFAULT 0, "
        "FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE);"

        // EKLENDİ: Sunucu Üyeleri
        "CREATE TABLE IF NOT EXISTS ServerMembers ("
        "ServerID INTEGER, "
        "UserID INTEGER, "
        "Nickname TEXT, "
        "JoinedAt DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "PRIMARY KEY (ServerID, UserID), "
        "FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE, "
        "FOREIGN KEY(UserID) REFERENCES Users(ID) ON DELETE CASCADE);"

        "CREATE TABLE IF NOT EXISTS Channels ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "ServerID INTEGER, "
        "Name TEXT NOT NULL, "
        "Type INTEGER NOT NULL, "
        "FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE);"

        // EKLENDİ: Mesajlar Tablosu
        "CREATE TABLE IF NOT EXISTS Messages ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "ChannelID INTEGER, "
        "SenderID INTEGER, "
        "Content TEXT, "
        "AttachmentURL TEXT, "
        "Timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "FOREIGN KEY(ChannelID) REFERENCES Channels(ID) ON DELETE CASCADE, "
        "FOREIGN KEY(SenderID) REFERENCES Users(ID));"

        "CREATE TABLE IF NOT EXISTS Friends ("
        "RequesterID INTEGER, "
        "TargetID INTEGER, "
        "Status INTEGER DEFAULT 0, "
        "CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "PRIMARY KEY (RequesterID, TargetID), "
        "FOREIGN KEY(RequesterID) REFERENCES Users(ID), "
        "FOREIGN KEY(TargetID) REFERENCES Users(ID));"

        "CREATE TABLE IF NOT EXISTS KanbanLists ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "ChannelID INTEGER, "
        "Title TEXT, "
        "Position INTEGER, "
        "FOREIGN KEY(ChannelID) REFERENCES Channels(ID));"

        "CREATE TABLE IF NOT EXISTS KanbanCards ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "ListID INTEGER, "
        "Title TEXT, "
        "Description TEXT, "
        "Priority INTEGER, "
        "Position INTEGER, "
        "FOREIGN KEY(ListID) REFERENCES KanbanLists(ID));";

    return executeQuery(sql);
}

// --- KULLANICI İŞLEMLERİ ---

bool DatabaseManager::createUser(const std::string& name, const std::string& email, const std::string& rawPassword, bool isAdmin) {
    std::string passwordHash = Security::hashPassword(rawPassword);
    if (passwordHash.empty()) return false;

    const char* sql = "INSERT INTO Users (Name, Email, PasswordHash, IsSystemAdmin) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, passwordHash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, isAdmin ? 1 : 0);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool DatabaseManager::loginUser(const std::string& email, const std::string& rawPassword) {
    const char* sql = "SELECT PasswordHash FROM Users WHERE Email = ?;";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_STATIC);

    bool loginSuccess = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* dbHash = sqlite3_column_text(stmt, 0);
        if (dbHash && Security::verifyPassword(rawPassword, reinterpret_cast<const char*>(dbHash))) {
            loginSuccess = true;
        }
    }
    sqlite3_finalize(stmt);
    return loginSuccess;
}

std::optional<User> DatabaseManager::getUser(const std::string& email) {
    const char* sql = "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL FROM Users WHERE Email = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_STATIC);

    std::optional<User> user = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // DÜZELTİLDİ: Struct sırasına tam uyum
        // Struct: id, name, email, password_hash, is_system_admin, status, avatar_url
        user = User{
            sqlite3_column_int(stmt, 0),      // id
            SAFE_TEXT(1),                     // name
            SAFE_TEXT(2),                     // email
            "",                               // password_hash (güvenlik için boş dönüyoruz)
            sqlite3_column_int(stmt, 4) != 0, // is_system_admin
            SAFE_TEXT(3),                     // status
            SAFE_TEXT(5)                      // avatar_url
        };
    }
    sqlite3_finalize(stmt);
    return user;
}

std::optional<User> DatabaseManager::getUserById(int id) {
    const char* sql = "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL FROM Users WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, id);

    std::optional<User> user = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user = User{
            sqlite3_column_int(stmt, 0),
            SAFE_TEXT(1),
            SAFE_TEXT(2),
            "",
            sqlite3_column_int(stmt, 4) != 0,
            SAFE_TEXT(3),
            SAFE_TEXT(5)
        };
    }
    sqlite3_finalize(stmt);
    return user;
}

// EKLENDİ: Profil Fotosu Güncelleme
bool DatabaseManager::updateUserAvatar(int userId, const std::string& avatarUrl) {
    std::string sql = "UPDATE Users SET AvatarURL = '" + avatarUrl + "' WHERE ID = " + std::to_string(userId) + ";";
    return executeQuery(sql);
}

// --- ARKADAŞLIK SİSTEMİ ---

bool DatabaseManager::sendFriendRequest(int myId, int targetUserId) {
    if (myId == targetUserId) return false;
    std::string sql = "INSERT INTO Friends (RequesterID, TargetID, Status) VALUES (" +
        std::to_string(myId) + ", " + std::to_string(targetUserId) + ", 0);";
    return executeQuery(sql);
}

bool DatabaseManager::acceptFriendRequest(int requesterId, int myId) {
    std::string sql = "UPDATE Friends SET Status = 1 WHERE RequesterID = " +
        std::to_string(requesterId) + " AND TargetID = " + std::to_string(myId) + ";";
    return executeQuery(sql);
}

bool DatabaseManager::rejectOrRemoveFriend(int otherUserId, int myId) {
    std::string sql = "DELETE FROM Friends WHERE (RequesterID = " + std::to_string(otherUserId) +
        " AND TargetID = " + std::to_string(myId) + ") OR (RequesterID = " +
        std::to_string(myId) + " AND TargetID = " + std::to_string(otherUserId) + ");";
    return executeQuery(sql);
}

std::vector<FriendRequest> DatabaseManager::getPendingRequests(int myId) {
    std::vector<FriendRequest> requests;
    std::string sql = "SELECT U.ID, U.Name, U.AvatarURL, F.CreatedAt FROM Users U "
        "JOIN Friends F ON U.ID = F.RequesterID "
        "WHERE F.TargetID = " + std::to_string(myId) + " AND F.Status = 0;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            requests.push_back(FriendRequest{
                sqlite3_column_int(stmt, 0), // requester_id
                SAFE_TEXT(1),                // requester_name
                SAFE_TEXT(2),                // requester_avatar
                SAFE_TEXT(3)                 // sent_at
                });
        }
    }
    sqlite3_finalize(stmt);
    return requests;
}

std::vector<User> DatabaseManager::getFriendsList(int myId) {
    std::vector<User> friends;
    std::string sql = "SELECT U.ID, U.Name, U.Email, U.Status, U.IsSystemAdmin, U.AvatarURL FROM Users U "
        "JOIN Friends F ON (U.ID = F.RequesterID OR U.ID = F.TargetID) "
        "WHERE (F.RequesterID = " + std::to_string(myId) + " OR F.TargetID = " + std::to_string(myId) + ") "
        "AND F.Status = 1 AND U.ID != " + std::to_string(myId) + ";";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            // DÜZELTİLDİ: Struct sırasına uyum
            friends.push_back(User{
                sqlite3_column_int(stmt, 0),
                SAFE_TEXT(1),
                SAFE_TEXT(2),
                "",
                sqlite3_column_int(stmt, 4) != 0,
                SAFE_TEXT(3),
                SAFE_TEXT(5)
                });
        }
    }
    sqlite3_finalize(stmt);
    return friends;
}

// --- SUNUCU, ROL VE KANAL İŞLEMLERİ ---

int DatabaseManager::createServer(const std::string& name, int ownerId) {
    std::string sql = "INSERT INTO Servers (Name, OwnerID, InviteCode) VALUES ('" + name + "', " + std::to_string(ownerId) + ", 'INV-" + name + "');";
    if (!executeQuery(sql)) return -1;

    int serverId = (int)sqlite3_last_insert_rowid(db);

    // Kurucuyu otomatik üye yap ve Admin rolü ver
    addMemberToServer(serverId, ownerId);
    createRole(serverId, "Admin", 100, 9999);

    return serverId;
}

bool DatabaseManager::addMemberToServer(int serverId, int userId) {
    std::string sql = "INSERT INTO ServerMembers (ServerID, UserID) VALUES (" + std::to_string(serverId) + ", " + std::to_string(userId) + ");";
    return executeQuery(sql);
}

bool DatabaseManager::createChannel(int serverId, std::string name, int type) {
    std::string sql = "INSERT INTO Channels (ServerID, Name, Type) VALUES (" + std::to_string(serverId) + ", '" + name + "', " + std::to_string(type) + ");";
    return executeQuery(sql);
}

bool DatabaseManager::createRole(int serverId, std::string roleName, int hierarchy, int permissions) {
    std::string sql = "INSERT INTO Roles (ServerID, RoleName, Hierarchy, Permissions) VALUES (" +
        std::to_string(serverId) + ", '" + roleName + "', " +
        std::to_string(hierarchy) + ", " + std::to_string(permissions) + ");";
    return executeQuery(sql);
}

// --- MESAJLAŞMA ---

bool DatabaseManager::sendMessage(int channelId, int senderId, const std::string& content, const std::string& attachmentUrl) {
    const char* sql = "INSERT INTO Messages (ChannelID, SenderID, Content, AttachmentURL) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, channelId);
    sqlite3_bind_int(stmt, 2, senderId);
    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, attachmentUrl.c_str(), -1, SQLITE_STATIC);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

std::vector<Message> DatabaseManager::getChannelMessages(int channelId, int limit) {
    std::vector<Message> messages;
    std::string sql = "SELECT M.ID, M.SenderID, U.Name, U.AvatarURL, M.Content, M.AttachmentURL, M.Timestamp "
        "FROM Messages M JOIN Users U ON M.SenderID = U.ID "
        "WHERE M.ChannelID = " + std::to_string(channelId) +
        " ORDER BY M.Timestamp ASC LIMIT " + std::to_string(limit) + ";";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            messages.push_back(Message{
                sqlite3_column_int(stmt, 0),
                channelId,
                sqlite3_column_int(stmt, 1),
                SAFE_TEXT(2), // Sender Name
                SAFE_TEXT(3), // Sender Avatar
                SAFE_TEXT(4), // Content
                SAFE_TEXT(5), // Attachment
                SAFE_TEXT(6)  // Timestamp
                });
        }
    }
    sqlite3_finalize(stmt);
    return messages;
}

// --- KANBAN ---

bool DatabaseManager::createKanbanList(int boardChannelId, std::string title) {
    std::string sql = "INSERT INTO KanbanLists (ChannelID, Title, Position) VALUES (" + std::to_string(boardChannelId) + ", '" + title + "', 0);";
    return executeQuery(sql);
}

bool DatabaseManager::createKanbanCard(int listId, std::string title, std::string desc, int priority) {
    std::string sql = "INSERT INTO KanbanCards (ListID, Title, Description, Priority, Position) VALUES (" +
        std::to_string(listId) + ", '" + title + "', '" + desc + "', " + std::to_string(priority) + ", 0);";
    return executeQuery(sql);
}

bool DatabaseManager::moveCard(int cardId, int newListId, int newPosition) {
    std::string sql = "UPDATE KanbanCards SET ListID=" + std::to_string(newListId) + ", Position=" + std::to_string(newPosition) + " WHERE ID=" + std::to_string(cardId) + ";";
    return executeQuery(sql);
}

std::vector<Server> DatabaseManager::getUserServers(int userId) {
    std::vector<Server> servers;
    // Kullanıcının üye olduğu sunucuları getir
    std::string sql = "SELECT S.ID, S.Name, S.OwnerID, S.InviteCode FROM Servers S "
        "JOIN ServerMembers SM ON S.ID = SM.ServerID "
        "WHERE SM.UserID = " + std::to_string(userId) + ";";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            servers.push_back({
                sqlite3_column_int(stmt, 0),
                sqlite3_column_int(stmt, 2),
                SAFE_TEXT(1),
                SAFE_TEXT(3),
                "" // IconURL şimdilik boş
                });
        }
    }
    sqlite3_finalize(stmt);
    return servers;
}

std::vector<Channel> DatabaseManager::getServerChannels(int serverId) {
    std::vector<Channel> channels;
    std::string sql = "SELECT ID, Name, Type FROM Channels WHERE ServerID = " + std::to_string(serverId) + ";";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            channels.push_back({
                sqlite3_column_int(stmt, 0),
                serverId,
                SAFE_TEXT(1),
                sqlite3_column_int(stmt, 2),
                false
                });
        }
    }
    sqlite3_finalize(stmt);
    return channels;
}

std::vector<KanbanCard> DatabaseManager::getKanbanCards(int listId) {
    std::vector<KanbanCard> cards;
    std::string sql = "SELECT ID, Title, Description, Priority, Position FROM KanbanCards WHERE ListID = " + std::to_string(listId) + " ORDER BY Position ASC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            cards.push_back({
                sqlite3_column_int(stmt, 0),
                listId,
                SAFE_TEXT(1),
                SAFE_TEXT(2),
                sqlite3_column_int(stmt, 3),
                sqlite3_column_int(stmt, 4)
                });
        }
    }
    sqlite3_finalize(stmt);
    return cards;
}

std::vector<DatabaseManager::KanbanListWithCards> DatabaseManager::getKanbanBoard(int channelId) {
    std::vector<KanbanListWithCards> board;
    // 1. Listeleri Çek
    std::string sql = "SELECT ID, Title, Position FROM KanbanLists WHERE ChannelID = " + std::to_string(channelId) + " ORDER BY Position ASC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int listId = sqlite3_column_int(stmt, 0);
            std::string title = SAFE_TEXT(1);
            int pos = sqlite3_column_int(stmt, 2);

            // 2. Her listenin kartlarını çek (Recursive mantık yerine döngü içinde çağırıyoruz)
            // Not: Performans için JOIN kullanılabilir ama şimdilik anlaşılır olması için böyle yapıyoruz.
            std::vector<KanbanCard> cards = getKanbanCards(listId);

            board.push_back({ listId, title, pos, cards });
        }
    }
    sqlite3_finalize(stmt);
    return board;
}