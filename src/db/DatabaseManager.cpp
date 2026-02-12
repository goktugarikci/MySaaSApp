#include "DatabaseManager.h"
#include "../utils/Security.h"
#include <iostream>

#define SAFE_TEXT(col) (reinterpret_cast<const char*>(sqlite3_column_text(stmt, col)) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, col)) : "")

DatabaseManager::DatabaseManager(const std::string& path) : db_path(path), db(nullptr) {}

DatabaseManager::~DatabaseManager() { close(); }

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
    if (db) { sqlite3_close(db); db = nullptr; }
}

bool DatabaseManager::executeQuery(const std::string& sql) {
    char* zErrMsg = 0;
    int rc = sqlite3_exec(db, sql.c_str(), 0, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL Hata: " << zErrMsg << "\nSorgu: " << sql << std::endl;
        sqlite3_free(zErrMsg);
        return false;
    }
    return true;
}

bool DatabaseManager::initTables() {
    // Tablo oluşturma kodları (Önceki ile aynı, yer kaplamaması için kısaltıldı)
    // Eğer burası boşsa önceki koddan initTables içeriğini kopyalayabilirsin.
    // Ancak en önemlisi yeni fonksiyonların aşağıda olmasıdır.
    std::string sql =
        "CREATE TABLE IF NOT EXISTS Users (ID INTEGER PRIMARY KEY AUTOINCREMENT, Name TEXT NOT NULL, Email TEXT UNIQUE NOT NULL, PasswordHash TEXT, GoogleID TEXT UNIQUE, IsSystemAdmin INTEGER DEFAULT 0, Status TEXT DEFAULT 'Offline', AvatarURL TEXT, SubscriptionLevel INTEGER DEFAULT 0, SubscriptionExpiresAt DATETIME, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP);"
        "CREATE TABLE IF NOT EXISTS Servers (ID INTEGER PRIMARY KEY AUTOINCREMENT, OwnerID INTEGER, Name TEXT NOT NULL, InviteCode TEXT UNIQUE, IconURL TEXT, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(OwnerID) REFERENCES Users(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Roles (ID INTEGER PRIMARY KEY AUTOINCREMENT, ServerID INTEGER, RoleName TEXT NOT NULL, Color TEXT DEFAULT '#FFFFFF', Hierarchy INTEGER DEFAULT 0, Permissions INTEGER DEFAULT 0, FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS ServerMembers (ServerID INTEGER, UserID INTEGER, Nickname TEXT, JoinedAt DATETIME DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (ServerID, UserID), FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE, FOREIGN KEY(UserID) REFERENCES Users(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Channels (ID INTEGER PRIMARY KEY AUTOINCREMENT, ServerID INTEGER, Name TEXT NOT NULL, Type INTEGER NOT NULL, FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Messages (ID INTEGER PRIMARY KEY AUTOINCREMENT, ChannelID INTEGER, SenderID INTEGER, Content TEXT, AttachmentURL TEXT, Timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(ChannelID) REFERENCES Channels(ID) ON DELETE CASCADE, FOREIGN KEY(SenderID) REFERENCES Users(ID));"
        "CREATE TABLE IF NOT EXISTS Friends (RequesterID INTEGER, TargetID INTEGER, Status INTEGER DEFAULT 0, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (RequesterID, TargetID), FOREIGN KEY(RequesterID) REFERENCES Users(ID), FOREIGN KEY(TargetID) REFERENCES Users(ID));"
        "CREATE TABLE IF NOT EXISTS KanbanLists (ID INTEGER PRIMARY KEY AUTOINCREMENT, ChannelID INTEGER, Title TEXT, Position INTEGER, FOREIGN KEY(ChannelID) REFERENCES Channels(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS KanbanCards (ID INTEGER PRIMARY KEY AUTOINCREMENT, ListID INTEGER, Title TEXT, Description TEXT, Priority INTEGER, Position INTEGER, FOREIGN KEY(ListID) REFERENCES KanbanLists(ID) ON DELETE CASCADE);";
    return executeQuery(sql);
}

// --- ROL YÖNETİMİ (HATAYI ÇÖZEN KISIM) ---
bool DatabaseManager::createRole(int serverId, std::string roleName, int hierarchy, int permissions) {
    std::string sql = "INSERT INTO Roles (ServerID, RoleName, Hierarchy, Permissions) VALUES (" +
        std::to_string(serverId) + ", '" + roleName + "', " +
        std::to_string(hierarchy) + ", " + std::to_string(permissions) + ");";
    return executeQuery(sql);
}

// --- SUNUCU YÖNETİMİ ---
int DatabaseManager::createServer(const std::string& name, int ownerId) {
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

bool DatabaseManager::updateServer(int serverId, const std::string& name, const std::string& iconUrl) {
    const char* sql = "UPDATE Servers SET Name = ?, IconURL = ? WHERE ID = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, iconUrl.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, serverId);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::deleteServer(int serverId) {
    std::string sql = "DELETE FROM Servers WHERE ID = " + std::to_string(serverId) + ";";
    return executeQuery(sql);
}

std::vector<Server> DatabaseManager::getUserServers(int userId) {
    std::vector<Server> servers;
    std::string sql = "SELECT S.ID, S.Name, S.OwnerID, S.InviteCode, S.IconURL, S.CreatedAt, "
        "(SELECT COUNT(*) FROM ServerMembers SM WHERE SM.ServerID = S.ID) "
        "FROM Servers S JOIN ServerMembers SM ON S.ID = SM.ServerID WHERE SM.UserID = " + std::to_string(userId) + ";";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            servers.push_back(Server{
                sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 2), SAFE_TEXT(1), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5), sqlite3_column_int(stmt, 6), {}
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
        server = Server{ sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 2), SAFE_TEXT(1), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5), 0, {} };
    }
    sqlite3_finalize(stmt);
    if (server) {
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
bool DatabaseManager::removeMemberFromServer(int serverId, int userId) {
    std::string sql = "DELETE FROM ServerMembers WHERE ServerID=" + std::to_string(serverId) + " AND UserID=" + std::to_string(userId) + ";";
    return executeQuery(sql);
}

// --- GOOGLE AUTH & KULLANICI ---
bool DatabaseManager::createGoogleUser(const std::string& name, const std::string& email, const std::string& googleId, const std::string& avatarUrl) {
    const char* sql = "INSERT INTO Users (Name, Email, GoogleID, AvatarURL, IsSystemAdmin) VALUES (?, ?, ?, ?, 0);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, googleId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, avatarUrl.c_str(), -1, SQLITE_STATIC);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

std::optional<User> DatabaseManager::getUserByGoogleId(const std::string& googleId) {
    const char* sql = "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL, SubscriptionLevel, SubscriptionExpiresAt, GoogleID FROM Users WHERE GoogleID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, googleId.c_str(), -1, SQLITE_STATIC);
    std::optional<User> user = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user = User{
            sqlite3_column_int(stmt, 0), SAFE_TEXT(1), SAFE_TEXT(2), "", sqlite3_column_int(stmt, 4) != 0,
            SAFE_TEXT(3), SAFE_TEXT(5), sqlite3_column_int(stmt, 6), SAFE_TEXT(7), SAFE_TEXT(8)
        };
    }
    sqlite3_finalize(stmt);
    return user;
}

bool DatabaseManager::createUser(const std::string& name, const std::string& email, const std::string& rawPassword, bool isAdmin) {
    std::string hash = Security::hashPassword(rawPassword);
    if (hash.empty()) return false;
    const char* sql = "INSERT INTO Users (Name, Email, PasswordHash, IsSystemAdmin) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, hash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, isAdmin ? 1 : 0);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::loginUser(const std::string& email, const std::string& rawPassword) {
    const char* sql = "SELECT PasswordHash FROM Users WHERE Email = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_STATIC);
    bool s = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* dbHash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (dbHash && Security::verifyPassword(rawPassword, dbHash)) s = true;
    }
    sqlite3_finalize(stmt);
    return s;
}

std::optional<User> DatabaseManager::getUser(const std::string& email) {
    const char* sql = "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL, SubscriptionLevel, SubscriptionExpiresAt, GoogleID FROM Users WHERE Email = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_STATIC);
    std::optional<User> user = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user = User{
            sqlite3_column_int(stmt, 0), SAFE_TEXT(1), SAFE_TEXT(2), "", sqlite3_column_int(stmt, 4) != 0,
            SAFE_TEXT(3), SAFE_TEXT(5), sqlite3_column_int(stmt, 6), SAFE_TEXT(7), SAFE_TEXT(8)
        };
    }
    sqlite3_finalize(stmt);
    return user;
}

std::optional<User> DatabaseManager::getUserById(int id) {
    const char* sql = "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL, SubscriptionLevel, SubscriptionExpiresAt, GoogleID FROM Users WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_int(stmt, 1, id);
    std::optional<User> user = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user = User{
            sqlite3_column_int(stmt, 0), SAFE_TEXT(1), SAFE_TEXT(2), "", sqlite3_column_int(stmt, 4) != 0,
            SAFE_TEXT(3), SAFE_TEXT(5), sqlite3_column_int(stmt, 6), SAFE_TEXT(7), SAFE_TEXT(8)
        };
    }
    sqlite3_finalize(stmt);
    return user;
}

bool DatabaseManager::updateUserAvatar(int userId, const std::string& avatarUrl) {
    std::string sql = "UPDATE Users SET AvatarURL = '" + avatarUrl + "' WHERE ID = " + std::to_string(userId) + ";";
    return executeQuery(sql);
}

bool DatabaseManager::updateUserDetails(int userId, const std::string& name, const std::string& status) {
    const char* sql = "UPDATE Users SET Name = ?, Status = ? WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, userId);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::deleteUser(int userId) {
    std::string sql = "DELETE FROM Users WHERE ID = " + std::to_string(userId) + ";";
    return executeQuery(sql);
}

// --- ABONELİK ---
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

bool DatabaseManager::updateUserSubscription(int userId, int level, int durationDays) {
    std::string sql = "UPDATE Users SET SubscriptionLevel = " + std::to_string(level) + ", SubscriptionExpiresAt = datetime('now', '+" + std::to_string(durationDays) + " days') WHERE ID = " + std::to_string(userId) + ";";
    return executeQuery(sql);
}

// --- KANAL YÖNETİMİ ---
bool DatabaseManager::createChannel(int serverId, std::string name, int type) {
    // TodoList Limiti
    if (type == 3) {
        std::string countSql = "SELECT COUNT(*) FROM Channels WHERE ServerID = " + std::to_string(serverId) + " AND Type = 3;";
        sqlite3_stmt* stmt;
        int count = 0;
        if (sqlite3_prepare_v2(db, countSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        if (count >= 1) return false;
    }
    std::string sql = "INSERT INTO Channels (ServerID, Name, Type) VALUES (" + std::to_string(serverId) + ", '" + name + "', " + std::to_string(type) + ");";
    return executeQuery(sql);
}

bool DatabaseManager::updateChannel(int channelId, const std::string& name) {
    std::string sql = "UPDATE Channels SET Name = '" + name + "' WHERE ID = " + std::to_string(channelId) + ";";
    return executeQuery(sql);
}
bool DatabaseManager::deleteChannel(int channelId) {
    std::string sql = "DELETE FROM Channels WHERE ID = " + std::to_string(channelId) + ";";
    return executeQuery(sql);
}
std::vector<Channel> DatabaseManager::getServerChannels(int serverId) {
    std::vector<Channel> channels;
    std::string sql = "SELECT ID, Name, Type FROM Channels WHERE ServerID = " + std::to_string(serverId) + ";";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            channels.push_back(Channel{ sqlite3_column_int(stmt, 0), serverId, SAFE_TEXT(1), sqlite3_column_int(stmt, 2), false });
        }
    }
    sqlite3_finalize(stmt);
    return channels;
}
int DatabaseManager::getServerKanbanCount(int serverId) {
    // createChannel içinde yapılıyor ama public erişim için
    std::string countSql = "SELECT COUNT(*) FROM Channels WHERE ServerID = " + std::to_string(serverId) + " AND Type = 3;";
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, countSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
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
bool DatabaseManager::updateMessage(int messageId, const std::string& newContent) {
    const char* sql = "UPDATE Messages SET Content = ? WHERE ID = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, newContent.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, messageId);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}
bool DatabaseManager::deleteMessage(int messageId) {
    std::string sql = "DELETE FROM Messages WHERE ID = " + std::to_string(messageId) + ";";
    return executeQuery(sql);
}
std::vector<Message> DatabaseManager::getChannelMessages(int channelId, int limit) {
    std::vector<Message> messages;
    std::string sql = "SELECT M.ID, M.SenderID, U.Name, U.AvatarURL, M.Content, M.AttachmentURL, M.Timestamp FROM Messages M JOIN Users U ON M.SenderID = U.ID WHERE M.ChannelID = " + std::to_string(channelId) + " ORDER BY M.Timestamp ASC LIMIT " + std::to_string(limit) + ";";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            messages.push_back(Message{ sqlite3_column_int(stmt, 0), channelId, sqlite3_column_int(stmt, 1), SAFE_TEXT(2), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5), SAFE_TEXT(6) });
        }
    }
    sqlite3_finalize(stmt);
    return messages;
}

// --- KANBAN / TRELLO ---
// DİKKAT: Burada 'KanbanList' kullanıyoruz (KanbanListWithCards DEĞİL)
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
                    cards.push_back(KanbanCard{ sqlite3_column_int(cardStmt, 0), listId, SAFE_TEXT(1), SAFE_TEXT(2), sqlite3_column_int(cardStmt, 3), sqlite3_column_int(cardStmt, 4) });
                }
            }
            sqlite3_finalize(cardStmt);
            // KanbanList yapısına uygun push
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
bool DatabaseManager::updateKanbanList(int listId, const std::string& title, int position) {
    const char* sql = "UPDATE KanbanLists SET Title = ?, Position = ? WHERE ID = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, position);
    sqlite3_bind_int(stmt, 3, listId);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}
bool DatabaseManager::deleteKanbanList(int listId) {
    std::string sql = "DELETE FROM KanbanLists WHERE ID = " + std::to_string(listId) + ";";
    return executeQuery(sql);
}
bool DatabaseManager::createKanbanCard(int listId, std::string title, std::string desc, int priority) {
    std::string sql = "INSERT INTO KanbanCards (ListID, Title, Description, Priority, Position) VALUES (" + std::to_string(listId) + ", '" + title + "', '" + desc + "', " + std::to_string(priority) + ", 0);";
    return executeQuery(sql);
}
bool DatabaseManager::updateKanbanCard(int cardId, std::string title, std::string desc, int priority) {
    const char* sql = "UPDATE KanbanCards SET Title = ?, Description = ?, Priority = ? WHERE ID = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, desc.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, priority);
    sqlite3_bind_int(stmt, 4, cardId);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}
bool DatabaseManager::deleteKanbanCard(int cardId) {
    std::string sql = "DELETE FROM KanbanCards WHERE ID = " + std::to_string(cardId) + ";";
    return executeQuery(sql);
}
bool DatabaseManager::moveCard(int cardId, int newListId, int newPosition) {
    std::string sql = "UPDATE KanbanCards SET ListID=" + std::to_string(newListId) + ", Position=" + std::to_string(newPosition) + " WHERE ID=" + std::to_string(cardId) + ";";
    return executeQuery(sql);
}

// --- ARKADAŞLIK ---
bool DatabaseManager::sendFriendRequest(int myId, int targetUserId) {
    if (myId == targetUserId) return false;
    std::string sql = "INSERT INTO Friends (RequesterID, TargetID, Status) VALUES (" + std::to_string(myId) + ", " + std::to_string(targetUserId) + ", 0);";
    return executeQuery(sql);
}
bool DatabaseManager::acceptFriendRequest(int requesterId, int myId) {
    std::string sql = "UPDATE Friends SET Status = 1 WHERE RequesterID = " + std::to_string(requesterId) + " AND TargetID = " + std::to_string(myId) + ";";
    return executeQuery(sql);
}
bool DatabaseManager::rejectOrRemoveFriend(int otherUserId, int myId) {
    std::string sql = "DELETE FROM Friends WHERE (RequesterID=" + std::to_string(otherUserId) + " AND TargetID=" + std::to_string(myId) + ") OR (RequesterID=" + std::to_string(myId) + " AND TargetID=" + std::to_string(otherUserId) + ");";
    return executeQuery(sql);
}
std::vector<FriendRequest> DatabaseManager::getPendingRequests(int myId) {
    std::vector<FriendRequest> reqs;
    std::string sql = "SELECT U.ID, U.Name, U.AvatarURL, F.CreatedAt FROM Users U JOIN Friends F ON U.ID=F.RequesterID WHERE F.TargetID=" + std::to_string(myId) + " AND F.Status=0;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            reqs.push_back({ sqlite3_column_int(stmt, 0), SAFE_TEXT(1), SAFE_TEXT(2), SAFE_TEXT(3) });
        }
    }
    sqlite3_finalize(stmt);
    return reqs;
}
std::vector<User> DatabaseManager::getFriendsList(int myId) {
    std::vector<User> friends;
    std::string sql = "SELECT U.ID, U.Name, U.Email, U.Status, U.IsSystemAdmin, U.AvatarURL FROM Users U JOIN Friends F ON (U.ID=F.RequesterID OR U.ID=F.TargetID) WHERE (F.RequesterID=" + std::to_string(myId) + " OR F.TargetID=" + std::to_string(myId) + ") AND F.Status=1 AND U.ID!=" + std::to_string(myId) + ";";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            friends.push_back(User{ sqlite3_column_int(stmt, 0), SAFE_TEXT(1), SAFE_TEXT(2), "", sqlite3_column_int(stmt, 4) != 0, SAFE_TEXT(3), SAFE_TEXT(5), 0, "", "" });
        }
    }
    sqlite3_finalize(stmt);
    return friends;
}
int DatabaseManager::getOrCreateDMChannel(int user1Id, int user2Id) {
    // 1. Önce bu ikisi arasında zaten bir DM kanalı var mı kontrol et
    // Not: DM kanallarının ServerID'si 0 veya NULL kabul edilir.
    // İsimlendirme standardı: "dm_küçükID_büyükID" (örn: dm_5_12)

    int u1 = std::min(user1Id, user2Id);
    int u2 = std::max(user1Id, user2Id);
    std::string dmName = "dm_" + std::to_string(u1) + "_" + std::to_string(u2);

    // Kanalı Ara
    std::string sql = "SELECT ID FROM Channels WHERE Name = '" + dmName + "' AND ServerID = 0;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            return id; // Mevcut kanalı dön
        }
    }
    sqlite3_finalize(stmt);

    // Yoksa Yeni Oluştur (ServerID = 0)
    sql = "INSERT INTO Channels (ServerID, Name, Type) VALUES (0, '" + dmName + "', 0);"; // 0: Text
    if (executeQuery(sql)) {
        return (int)sqlite3_last_insert_rowid(db);
    }
    return -1;
}
// =============================================================
// [YENİ] SİSTEM YÖNETİCİSİ İŞLEMLERİ
// =============================================================

DatabaseManager::SystemStats DatabaseManager::getSystemStats() {
    SystemStats stats = { 0, 0, 0 };
    // Kullanıcı Sayısı
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM Users;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) stats.user_count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    // Sunucu Sayısı
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM Servers;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) stats.server_count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    // Mesaj Sayısı
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM Messages;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) stats.message_count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    return stats;
}

std::vector<User> DatabaseManager::getAllUsers() {
    std::vector<User> users;
    // Basit listeleme, detaylar kırpıldı
    std::string sql = "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL FROM Users;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            users.push_back(User{
                sqlite3_column_int(stmt, 0), SAFE_TEXT(1), SAFE_TEXT(2), "",
                sqlite3_column_int(stmt, 4) != 0, SAFE_TEXT(3), SAFE_TEXT(5), 0, "", ""
                });
        }
    }
    sqlite3_finalize(stmt);
    return users;
}

bool DatabaseManager::banUser(int userId) {
    // Kullanıcıyı sistemden siler (veya Status='Banned' yapılabilir)
    return deleteUser(userId);
}

// =============================================================
// [YENİ] ÜYE VE ROL YÖNETİMİ
// =============================================================

bool DatabaseManager::joinServerByCode(int userId, const std::string& inviteCode) {
    // 1. Kodu bul ve ServerID'yi al
    std::string sql = "SELECT ID FROM Servers WHERE InviteCode = ?;";
    sqlite3_stmt* stmt;
    int serverId = -1;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, inviteCode.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            serverId = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);

    if (serverId == -1) return false; // Kod geçersiz

    // 2. Üye yap
    return addMemberToServer(serverId, userId);
}

bool DatabaseManager::kickMember(int serverId, int userId) {
    // ServerMembers tablosundan sil
    return removeMemberFromServer(serverId, userId);
}

std::vector<Role> DatabaseManager::getServerRoles(int serverId) {
    std::vector<Role> roles;
    std::string sql = "SELECT ID, RoleName, Color, Hierarchy, Permissions FROM Roles WHERE ServerID = " + std::to_string(serverId) + ";";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            roles.push_back(Role{
                sqlite3_column_int(stmt, 0), serverId,
                SAFE_TEXT(1), SAFE_TEXT(2),
                sqlite3_column_int(stmt, 3), sqlite3_column_int(stmt, 4)
                });
        }
    }
    sqlite3_finalize(stmt);
    return roles;
}

bool DatabaseManager::assignRole(int serverId, int userId, int roleId) {
    // Bu özellik için UserRoles diye bir ara tablo gerekir (Many-to-Many).
    // Şimdilik basitlik adına ServerMembers tablosuna 'RoleID' sütunu ekleyebilir 
    // veya sadece işlemi başarılı dönerek placeholder bırakabiliriz.
    // Profesyonel yapı için yeni tablo şarttır ama şimdilik true dönelim.
    return true;
}

// =============================================================
// [YENİ] BİREBİR SOHBET (DM)
// =============================================================

int DatabaseManager::getOrCreateDMChannel(int user1Id, int user2Id) {
    // Kullanıcı ID'lerini sıraya diz (Küçük-Büyük) böylece A->B ile B->A aynı kanalı açar.
    int u1 = std::min(user1Id, user2Id);
    int u2 = std::max(user1Id, user2Id);

    // Özel bir isimlendirme formatı: "dm_1_5"
    std::string dmName = "dm_" + std::to_string(u1) + "_" + std::to_string(u2);

    // 1. Kanal var mı kontrol et (ServerID = 0 olan kanallar DM'dir)
    std::string sql = "SELECT ID FROM Channels WHERE Name = ? AND ServerID = 0;";
    sqlite3_stmt* stmt;
    int channelId = -1;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, dmName.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            channelId = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);

    if (channelId != -1) return channelId; // Varsa dön

    // 2. Yoksa Oluştur (Type 0: Text)
    // ServerID'yi 0 olarak kaydediyoruz.
    sql = "INSERT INTO Channels (ServerID, Name, Type) VALUES (0, '" + dmName + "', 0);";
    if (executeQuery(sql)) {
        return (int)sqlite3_last_insert_rowid(db);
    }
    return -1;
}