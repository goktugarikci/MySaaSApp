#include "DatabaseManager.h"
#include "../utils/Security.h"
#include <iostream>

// Yardımcı Makro: SQLite'dan gelen NULL metinleri güvenli string'e çevirir
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
    // Foreign Key desteğini aç
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
        "SubscriptionLevel INTEGER DEFAULT 0, "
        "SubscriptionExpiresAt DATETIME, "
        "CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP);"

        "CREATE TABLE IF NOT EXISTS Servers ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "OwnerID INTEGER, "
        "Name TEXT NOT NULL, "
        "InviteCode TEXT UNIQUE, "
        "IconURL TEXT, "
        "CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "FOREIGN KEY(OwnerID) REFERENCES Users(ID));"

        "CREATE TABLE IF NOT EXISTS Roles ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "ServerID INTEGER, "
        "RoleName TEXT NOT NULL, "
        "Color TEXT DEFAULT '#FFFFFF', "
        "Hierarchy INTEGER DEFAULT 0, "
        "Permissions INTEGER DEFAULT 0, "
        "FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE);"

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
        "Type INTEGER NOT NULL, " // 0:Text, 1:Voice, 2:Video, 3:Kanban
        "FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE);"

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
        "Status INTEGER DEFAULT 0, " // 0:Bekliyor, 1:Arkadaş
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

// --- ABONELİK & LİMİT ---

bool DatabaseManager::isSubscriptionActive(int userId) {
    std::string sql = "SELECT SubscriptionLevel FROM Users WHERE ID = " + std::to_string(userId) + ";";
    sqlite3_stmt* stmt;
    bool active = false;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            if (sqlite3_column_int(stmt, 0) > 0) active = true;
        }
    }
    sqlite3_finalize(stmt);
    return active;
}

int DatabaseManager::getUserServerCount(int userId) {
    std::string sql = "SELECT COUNT(*) FROM Servers WHERE OwnerID = " + std::to_string(userId) + ";";
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

int DatabaseManager::getServerKanbanCount(int serverId) {
    std::string sql = "SELECT COUNT(*) FROM Channels WHERE ServerID = " + std::to_string(serverId) + " AND Type = 3;";
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

bool DatabaseManager::updateUserSubscription(int userId, int level, int durationDays) {
    std::string sql = "UPDATE Users SET SubscriptionLevel = " + std::to_string(level) +
        ", SubscriptionExpiresAt = datetime('now', '+" + std::to_string(durationDays) + " days') "
        "WHERE ID = " + std::to_string(userId) + ";";
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
    const char* sql = "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL, SubscriptionLevel, SubscriptionExpiresAt FROM Users WHERE Email = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_STATIC);

    std::optional<User> user = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user = User{
            sqlite3_column_int(stmt, 0),      // id
            SAFE_TEXT(1),                     // name
            SAFE_TEXT(2),                     // email
            "",                               // password_hash (boş dönüyoruz)
            sqlite3_column_int(stmt, 4) != 0, // is_system_admin
            SAFE_TEXT(3),                     // status
            SAFE_TEXT(5),                     // avatar_url
            sqlite3_column_int(stmt, 6),      // subscription_level
            SAFE_TEXT(7)                      // subscription_expires_at
        };
    }
    sqlite3_finalize(stmt);
    return user;
}

std::optional<User> DatabaseManager::getUserById(int id) {
    const char* sql = "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL, SubscriptionLevel, SubscriptionExpiresAt FROM Users WHERE ID = ?;";
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
            SAFE_TEXT(5),
            sqlite3_column_int(stmt, 6),
            SAFE_TEXT(7)
        };
    }
    sqlite3_finalize(stmt);
    return user;
}

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
            friends.push_back(User{
                sqlite3_column_int(stmt, 0),
                SAFE_TEXT(1),
                SAFE_TEXT(2),
                "",
                sqlite3_column_int(stmt, 4) != 0,
                SAFE_TEXT(3),
                SAFE_TEXT(5),
                0, "" // Abonelik bilgileri bu sorguda yok, varsayılan değerler
                });
        }
    }
    sqlite3_finalize(stmt);
    return friends;
}

// --- SUNUCU YÖNETİMİ ---

int DatabaseManager::createServer(const std::string& name, int ownerId) {
    // Limit Kontrolü
    if (!isSubscriptionActive(ownerId) && getUserServerCount(ownerId) >= 1) {
        std::cerr << "LIMIT: Free kullanici max 1 sunucu acabilir.\n";
        return -1;
    }

    std::string sql = "INSERT INTO Servers (Name, OwnerID, InviteCode) VALUES ('" + name + "', " + std::to_string(ownerId) + ", 'INV-" + name + "');";
    if (!executeQuery(sql)) return -1;

    int serverId = (int)sqlite3_last_insert_rowid(db);
    addMemberToServer(serverId, ownerId);
    createRole(serverId, "Admin", 100, 9999);

    return serverId;
}

std::vector<Server> DatabaseManager::getUserServers(int userId) {
    std::vector<Server> servers;
    std::string sql = "SELECT S.ID, S.Name, S.OwnerID, S.InviteCode, S.IconURL, S.CreatedAt, "
        "(SELECT COUNT(*) FROM ServerMembers SM WHERE SM.ServerID = S.ID) "
        "FROM Servers S "
        "JOIN ServerMembers SM ON S.ID = SM.ServerID "
        "WHERE SM.UserID = " + std::to_string(userId) + ";";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            servers.push_back(Server{
                sqlite3_column_int(stmt, 0), // ID
                sqlite3_column_int(stmt, 2), // OwnerID
                SAFE_TEXT(1),                // Name
                SAFE_TEXT(3),                // InviteCode
                SAFE_TEXT(4),                // IconURL
                SAFE_TEXT(5),                // CreatedAt
                sqlite3_column_int(stmt, 6), // MemberCount
                {}                           // member_ids (boş)
                });
        }
    }
    sqlite3_finalize(stmt);
    return servers;
}

std::optional<Server> DatabaseManager::getServerDetails(int serverId) {
    std::string sql = "SELECT ID, Name, OwnerID, InviteCode, IconURL, CreatedAt FROM Servers WHERE ID = " + std::to_string(serverId) + ";";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    std::optional<Server> server = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        server = Server{
            sqlite3_column_int(stmt, 0),
            sqlite3_column_int(stmt, 2),
            SAFE_TEXT(1),
            SAFE_TEXT(3),
            SAFE_TEXT(4),
            SAFE_TEXT(5),
            0, {}
        };
    }
    sqlite3_finalize(stmt);

    if (server) {
        // Üye listesini doldur
        sql = "SELECT UserID FROM ServerMembers WHERE ServerID = " + std::to_string(serverId) + ";";
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                server->member_ids.push_back(sqlite3_column_int(stmt, 0));
            }
            server->member_count = (int)server->member_ids.size();
        }
        sqlite3_finalize(stmt);
    }
    return server;
}

bool DatabaseManager::addMemberToServer(int serverId, int userId) {
    std::string sql = "INSERT INTO ServerMembers (ServerID, UserID) VALUES (" + std::to_string(serverId) + ", " + std::to_string(userId) + ");";
    return executeQuery(sql);
}

// --- KANAL & ROL ---

bool DatabaseManager::createChannel(int serverId, std::string name, int type) {
    if (type == 3) {
        if (getServerKanbanCount(serverId) >= 1) {
            std::cerr << "LIMIT: Bu sunucuda sadece 1 adet TodoList oluşturulabilir.\n";
            return false;
        }
    }
    std::string sql = "INSERT INTO Channels (ServerID, Name, Type) VALUES (" + std::to_string(serverId) + ", '" + name + "', " + std::to_string(type) + ");";
    return executeQuery(sql);
}

std::vector<Channel> DatabaseManager::getServerChannels(int serverId) {
    std::vector<Channel> channels;
    std::string sql = "SELECT ID, Name, Type FROM Channels WHERE ServerID = " + std::to_string(serverId) + ";";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            channels.push_back(Channel{
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
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
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
                sqlite3_column_int(stmt, 0), channelId, sqlite3_column_int(stmt, 1),
                SAFE_TEXT(2), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5), SAFE_TEXT(6)
                });
        }
    }
    sqlite3_finalize(stmt);
    return messages;
}

// --- KANBAN / TRELLO ---

std::vector<KanbanList> DatabaseManager::getKanbanBoard(int channelId) {
    std::vector<KanbanList> board;
    std::string sql = "SELECT ID, Title, Position FROM KanbanLists WHERE ChannelID = " + std::to_string(channelId) + " ORDER BY Position ASC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int listId = sqlite3_column_int(stmt, 0);
            std::string title = SAFE_TEXT(1);
            int pos = sqlite3_column_int(stmt, 2);

            std::vector<KanbanCard> cards;
            std::string cardSql = "SELECT ID, Title, Description, Priority, Position FROM KanbanCards WHERE ListID = " + std::to_string(listId) + " ORDER BY Position ASC;";
            sqlite3_stmt* cardStmt;
            if (sqlite3_prepare_v2(db, cardSql.c_str(), -1, &cardStmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(cardStmt) == SQLITE_ROW) {
                    cards.push_back(KanbanCard{
                        sqlite3_column_int(cardStmt, 0),
                        listId,
                        reinterpret_cast<const char*>(sqlite3_column_text(cardStmt, 1)),
                        reinterpret_cast<const char*>(sqlite3_column_text(cardStmt, 2)),
                        sqlite3_column_int(cardStmt, 3),
                        sqlite3_column_int(cardStmt, 4)
                        });
                }
            }
            sqlite3_finalize(cardStmt);

            board.push_back(KanbanList{ listId, title, pos, cards });
        }
    }
    sqlite3_finalize(stmt);
    return board;
}

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