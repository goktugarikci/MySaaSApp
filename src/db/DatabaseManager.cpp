#include "DatabaseManager.h"
#include "../utils/Security.h"
#include <iostream>
#include <algorithm> 

// SQLite verisini güvenle string'e çeviren makro
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
    // Tümü TEXT PRIMARY KEY olarak güncellendi!
    std::string sql =
        "CREATE TABLE IF NOT EXISTS Users (ID TEXT PRIMARY KEY, Name TEXT NOT NULL, Email TEXT UNIQUE NOT NULL, PasswordHash TEXT, GoogleID TEXT UNIQUE, IsSystemAdmin INTEGER DEFAULT 0, Status TEXT DEFAULT 'Offline', AvatarURL TEXT, SubscriptionLevel INTEGER DEFAULT 0, SubscriptionExpiresAt DATETIME, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP);"
        "CREATE TABLE IF NOT EXISTS Servers (ID TEXT PRIMARY KEY, OwnerID TEXT, Name TEXT NOT NULL, InviteCode TEXT UNIQUE, IconURL TEXT, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(OwnerID) REFERENCES Users(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Roles (ID TEXT PRIMARY KEY, ServerID TEXT, RoleName TEXT NOT NULL, Color TEXT DEFAULT '#FFFFFF', Hierarchy INTEGER DEFAULT 0, Permissions INTEGER DEFAULT 0, FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS ServerMembers (ServerID TEXT, UserID TEXT, Nickname TEXT, JoinedAt DATETIME DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (ServerID, UserID), FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE, FOREIGN KEY(UserID) REFERENCES Users(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Channels (ID TEXT PRIMARY KEY, ServerID TEXT, Name TEXT NOT NULL, Type INTEGER NOT NULL, FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Messages (ID TEXT PRIMARY KEY, ChannelID TEXT, SenderID TEXT, Content TEXT, AttachmentURL TEXT, Timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(ChannelID) REFERENCES Channels(ID) ON DELETE CASCADE, FOREIGN KEY(SenderID) REFERENCES Users(ID));"
        "CREATE TABLE IF NOT EXISTS Friends (RequesterID TEXT, TargetID TEXT, Status INTEGER DEFAULT 0, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (RequesterID, TargetID), FOREIGN KEY(RequesterID) REFERENCES Users(ID), FOREIGN KEY(TargetID) REFERENCES Users(ID));"
        "CREATE TABLE IF NOT EXISTS KanbanLists (ID TEXT PRIMARY KEY, ChannelID TEXT, Title TEXT, Position INTEGER, FOREIGN KEY(ChannelID) REFERENCES Channels(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS KanbanCards (ID TEXT PRIMARY KEY, ListID TEXT, Title TEXT, Description TEXT, Priority INTEGER, Position INTEGER, FOREIGN KEY(ListID) REFERENCES KanbanLists(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Payments (ID TEXT PRIMARY KEY, UserID TEXT, ProviderPaymentID TEXT, Amount REAL, Currency TEXT, Status TEXT DEFAULT 'pending', CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(UserID) REFERENCES Users(ID));"
        "CREATE TABLE IF NOT EXISTS Reports (ID TEXT PRIMARY KEY, ReporterID TEXT, ContentID TEXT, Type TEXT, Reason TEXT, Status TEXT DEFAULT 'OPEN', CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(ReporterID) REFERENCES Users(ID));";

    return executeQuery(sql);
}

// =============================================================
// YÖNETİCİ VE İSTATİSTİKLER
// =============================================================

SystemStats DatabaseManager::getSystemStats() {
    SystemStats stats = { 0, 0, 0 };
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM Users;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) stats.user_count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM Servers;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) stats.server_count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM Messages;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) stats.message_count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    return stats;
}

std::vector<User> DatabaseManager::getAllUsers() {
    std::vector<User> users;
    std::string sql = "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL FROM Users;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            users.push_back(User{
                SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), "",
                sqlite3_column_int(stmt, 4) != 0, SAFE_TEXT(3), SAFE_TEXT(5), 0, "", ""
                });
        }
    }
    sqlite3_finalize(stmt);
    return users;
}

bool DatabaseManager::isSystemAdmin(std::string userId) {
    std::string sql = "SELECT IsSystemAdmin FROM Users WHERE ID = '" + userId + "';";
    sqlite3_stmt* stmt;
    bool isAdmin = false;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) isAdmin = (sqlite3_column_int(stmt, 0) == 1);
    }
    sqlite3_finalize(stmt);
    return isAdmin;
}

// =============================================================
// KULLANICI & GOOGLE AUTH
// =============================================================

bool DatabaseManager::createGoogleUser(const std::string& name, const std::string& email, const std::string& googleId, const std::string& avatarUrl) {
    std::string id = Security::generateId(15);
    const char* sql = "INSERT INTO Users (ID, Name, Email, GoogleID, AvatarURL, IsSystemAdmin) VALUES (?, ?, ?, ?, ?, 0);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, email.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, googleId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, avatarUrl.c_str(), -1, SQLITE_STATIC);
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
            SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), "",
            sqlite3_column_int(stmt, 4) != 0, SAFE_TEXT(3), SAFE_TEXT(5),
            sqlite3_column_int(stmt, 6), SAFE_TEXT(7), SAFE_TEXT(8)
        };
    }
    sqlite3_finalize(stmt);
    return user;
}

bool DatabaseManager::createUser(const std::string& name, const std::string& email, const std::string& rawPassword, bool isAdmin) {
    std::string hash = Security::hashPassword(rawPassword);
    if (hash.empty()) return false;

    std::string id = Security::generateId(15);
    const char* sql = "INSERT INTO Users (ID, Name, Email, PasswordHash, IsSystemAdmin) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, email.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, hash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, isAdmin ? 1 : 0);
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
            SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), "",
            sqlite3_column_int(stmt, 4) != 0, SAFE_TEXT(3), SAFE_TEXT(5),
            sqlite3_column_int(stmt, 6), SAFE_TEXT(7), SAFE_TEXT(8)
        };
    }
    sqlite3_finalize(stmt);
    return user;
}

std::optional<User> DatabaseManager::getUserById(std::string id) {
    const char* sql = "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL, SubscriptionLevel, SubscriptionExpiresAt, GoogleID FROM Users WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    std::optional<User> user = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user = User{
            SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), "",
            sqlite3_column_int(stmt, 4) != 0, SAFE_TEXT(3), SAFE_TEXT(5),
            sqlite3_column_int(stmt, 6), SAFE_TEXT(7), SAFE_TEXT(8)
        };
    }
    sqlite3_finalize(stmt);
    return user;
}

bool DatabaseManager::updateUserAvatar(std::string userId, const std::string& avatarUrl) {
    return executeQuery("UPDATE Users SET AvatarURL = '" + avatarUrl + "' WHERE ID = '" + userId + "';");
}

bool DatabaseManager::updateUserDetails(std::string userId, const std::string& name, const std::string& status) {
    const char* sql = "UPDATE Users SET Name = ?, Status = ? WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, userId.c_str(), -1, SQLITE_STATIC);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::deleteUser(std::string userId) {
    return executeQuery("DELETE FROM Users WHERE ID = '" + userId + "'");
}
bool DatabaseManager::banUser(std::string userId) {
    return deleteUser(userId);
}

// =============================================================
// SUNUCU & ROL YÖNETİMİ
// =============================================================

std::string DatabaseManager::createServer(const std::string& name, std::string ownerId) {
    if (!isSubscriptionActive(ownerId) && getUserServerCount(ownerId) >= 1) return "";

    std::string id = Security::generateId(15);
    std::string sql = "INSERT INTO Servers (ID, OwnerID, Name, InviteCode) VALUES ('" + id + "', '" + ownerId + "', '" + name + "', 'INV-" + id + "');";

    if (!executeQuery(sql)) return "";

    addMemberToServer(id, ownerId);
    createRole(id, "Admin", 100, 9999);
    return id;
}

bool DatabaseManager::updateServer(std::string serverId, const std::string& name, const std::string& iconUrl) {
    const char* sql = "UPDATE Servers SET Name = ?, IconURL = ? WHERE ID = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, iconUrl.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, serverId.c_str(), -1, SQLITE_STATIC);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::deleteServer(std::string serverId) {
    return executeQuery("DELETE FROM Servers WHERE ID = '" + serverId + "'");
}

std::vector<Server> DatabaseManager::getUserServers(std::string userId) {
    std::vector<Server> servers;
    std::string sql = "SELECT S.ID, S.Name, S.OwnerID, S.InviteCode, S.IconURL, S.CreatedAt, "
        "(SELECT COUNT(*) FROM ServerMembers SM WHERE SM.ServerID = S.ID) "
        "FROM Servers S JOIN ServerMembers SM ON S.ID = SM.ServerID WHERE SM.UserID = '" + userId + "';";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            servers.push_back(Server{
                SAFE_TEXT(0), SAFE_TEXT(2), SAFE_TEXT(1), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5), sqlite3_column_int(stmt, 6), {}
                });
        }
    }
    sqlite3_finalize(stmt);
    return servers;
}

std::optional<Server> DatabaseManager::getServerDetails(std::string serverId) {
    std::string sql = "SELECT ID, Name, OwnerID, InviteCode, IconURL, CreatedAt FROM Servers WHERE ID = '" + serverId + "';";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    std::optional<Server> server = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        server = Server{ SAFE_TEXT(0), SAFE_TEXT(2), SAFE_TEXT(1), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5), 0, {} };
    }
    sqlite3_finalize(stmt);
    return server;
}

bool DatabaseManager::addMemberToServer(std::string serverId, std::string userId) {
    return executeQuery("INSERT INTO ServerMembers (ServerID, UserID) VALUES ('" + serverId + "', '" + userId + "');");
}

bool DatabaseManager::removeMemberFromServer(std::string serverId, std::string userId) {
    return executeQuery("DELETE FROM ServerMembers WHERE ServerID='" + serverId + "' AND UserID='" + userId + "'");
}

bool DatabaseManager::joinServerByCode(std::string userId, const std::string& inviteCode) {
    std::string sql = "SELECT ID FROM Servers WHERE InviteCode = ?;";
    sqlite3_stmt* stmt;
    std::string serverId = "";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, inviteCode.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) serverId = SAFE_TEXT(0);
    }
    sqlite3_finalize(stmt);
    if (serverId.empty()) return false;
    return addMemberToServer(serverId, userId);
}

bool DatabaseManager::kickMember(std::string serverId, std::string userId) {
    return removeMemberFromServer(serverId, userId);
}

bool DatabaseManager::createRole(std::string serverId, std::string roleName, int hierarchy, int permissions) {
    std::string id = Security::generateId(15);
    std::string sql = "INSERT INTO Roles (ID, ServerID, RoleName, Hierarchy, Permissions) VALUES ('" +
        id + "', '" + serverId + "', '" + roleName + "', " +
        std::to_string(hierarchy) + ", " + std::to_string(permissions) + ");";
    return executeQuery(sql);
}

std::vector<Role> DatabaseManager::getServerRoles(std::string serverId) {
    std::vector<Role> roles;
    std::string sql = "SELECT ID, RoleName, Color, Hierarchy, Permissions FROM Roles WHERE ServerID = '" + serverId + "';";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            roles.push_back(Role{ SAFE_TEXT(0), serverId, SAFE_TEXT(1), SAFE_TEXT(2), sqlite3_column_int(stmt, 3), sqlite3_column_int(stmt, 4) });
        }
    }
    sqlite3_finalize(stmt);
    return roles;
}

bool DatabaseManager::assignRole(std::string serverId, std::string userId, std::string roleId) { return true; }

// =============================================================
// KANAL YÖNETİMİ
// =============================================================

bool DatabaseManager::createChannel(std::string serverId, std::string name, int type) {
    if (type == 3 && getServerKanbanCount(serverId) >= 1) return false;
    std::string id = Security::generateId(15);
    return executeQuery("INSERT INTO Channels (ID, ServerID, Name, Type) VALUES ('" + id + "', '" + serverId + "', '" + name + "', " + std::to_string(type) + ");");
}

bool DatabaseManager::updateChannel(std::string channelId, const std::string& name) {
    return executeQuery("UPDATE Channels SET Name = '" + name + "' WHERE ID = '" + channelId + "'");
}

bool DatabaseManager::deleteChannel(std::string channelId) {
    return executeQuery("DELETE FROM Channels WHERE ID = '" + channelId + "'");
}

std::vector<Channel> DatabaseManager::getServerChannels(std::string serverId) {
    std::vector<Channel> channels;
    std::string sql = "SELECT ID, Name, Type FROM Channels WHERE ServerID = '" + serverId + "';";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            channels.push_back(Channel{ SAFE_TEXT(0), serverId, SAFE_TEXT(1), sqlite3_column_int(stmt, 2), false });
        }
    }
    sqlite3_finalize(stmt);
    return channels;
}

int DatabaseManager::getServerKanbanCount(std::string serverId) {
    std::string sql = "SELECT COUNT(*) FROM Channels WHERE ServerID = '" + serverId + "' AND Type = 3;";
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

// =============================================================
// MESAJLAŞMA & DM
// =============================================================

std::string DatabaseManager::getOrCreateDMChannel(std::string user1Id, std::string user2Id) {
    std::string u1 = std::min(user1Id, user2Id);
    std::string u2 = std::max(user1Id, user2Id);
    std::string dmName = "dm_" + u1 + "_" + u2;

    std::string sql = "SELECT ID FROM Channels WHERE Name = '" + dmName + "' AND ServerID = '0';";
    sqlite3_stmt* stmt;
    std::string channelId = "";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) channelId = SAFE_TEXT(0);
    }
    sqlite3_finalize(stmt);
    if (!channelId.empty()) return channelId;

    channelId = Security::generateId(15);
    sql = "INSERT INTO Channels (ID, ServerID, Name, Type) VALUES ('" + channelId + "', '0', '" + dmName + "', 0);";
    if (executeQuery(sql)) return channelId;
    return "";
}

bool DatabaseManager::sendMessage(std::string channelId, std::string senderId, const std::string& content, const std::string& attachmentUrl) {
    std::string id = Security::generateId(15);
    const char* sql = "INSERT INTO Messages (ID, ChannelID, SenderID, Content, AttachmentURL) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, channelId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, senderId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, content.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, attachmentUrl.c_str(), -1, SQLITE_STATIC);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::updateMessage(std::string messageId, const std::string& newContent) {
    const char* sql = "UPDATE Messages SET Content = ? WHERE ID = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, newContent.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, messageId.c_str(), -1, SQLITE_STATIC);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::deleteMessage(std::string messageId) {
    return executeQuery("DELETE FROM Messages WHERE ID = '" + messageId + "'");
}

std::vector<Message> DatabaseManager::getChannelMessages(std::string channelId, int limit) {
    std::vector<Message> messages;
    std::string sql = "SELECT M.ID, M.SenderID, U.Name, U.AvatarURL, M.Content, M.AttachmentURL, M.Timestamp FROM Messages M JOIN Users U ON M.SenderID = U.ID WHERE M.ChannelID = '" + channelId + "' ORDER BY M.Timestamp ASC LIMIT " + std::to_string(limit) + ";";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            messages.push_back(Message{ SAFE_TEXT(0), channelId, SAFE_TEXT(1), SAFE_TEXT(2), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5), SAFE_TEXT(6) });
        }
    }
    sqlite3_finalize(stmt);
    return messages;
}

// =============================================================
// KANBAN SİSTEMİ
// =============================================================

std::vector<KanbanList> DatabaseManager::getKanbanBoard(std::string channelId) {
    std::vector<KanbanList> board;
    std::string sql = "SELECT ID, Title, Position FROM KanbanLists WHERE ChannelID = '" + channelId + "' ORDER BY Position ASC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string listId = SAFE_TEXT(0);
            std::vector<KanbanCard> cards;
            std::string cardSql = "SELECT ID, Title, Description, Priority, Position FROM KanbanCards WHERE ListID = '" + listId + "' ORDER BY Position ASC;";
            sqlite3_stmt* cardStmt;
            if (sqlite3_prepare_v2(db, cardSql.c_str(), -1, &cardStmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(cardStmt) == SQLITE_ROW) {
                    cards.push_back(KanbanCard{ SAFE_TEXT(0), listId, SAFE_TEXT(1), SAFE_TEXT(2), sqlite3_column_int(cardStmt, 3), sqlite3_column_int(cardStmt, 4) });
                }
            }
            sqlite3_finalize(cardStmt);
            board.push_back(KanbanList{ listId, SAFE_TEXT(1), sqlite3_column_int(stmt, 2), cards });
        }
    }
    sqlite3_finalize(stmt);
    return board;
}

bool DatabaseManager::createKanbanList(std::string boardChannelId, std::string title) {
    std::string id = Security::generateId(15);
    return executeQuery("INSERT INTO KanbanLists (ID, ChannelID, Title, Position) VALUES ('" + id + "', '" + boardChannelId + "', '" + title + "', 0);");
}

bool DatabaseManager::updateKanbanList(std::string listId, const std::string& title, int position) {
    return executeQuery("UPDATE KanbanLists SET Title='" + title + "', Position=" + std::to_string(position) + " WHERE ID='" + listId + "'");
}

bool DatabaseManager::deleteKanbanList(std::string listId) {
    return executeQuery("DELETE FROM KanbanLists WHERE ID='" + listId + "'");
}

bool DatabaseManager::createKanbanCard(std::string listId, std::string title, std::string desc, int priority) {
    std::string id = Security::generateId(15);
    return executeQuery("INSERT INTO KanbanCards (ID, ListID, Title, Description, Priority, Position) VALUES ('" + id + "', '" + listId + "', '" + title + "', '" + desc + "', " + std::to_string(priority) + ", 0);");
}

bool DatabaseManager::updateKanbanCard(std::string cardId, std::string title, std::string desc, int priority) {
    return executeQuery("UPDATE KanbanCards SET Title='" + title + "', Description='" + desc + "', Priority=" + std::to_string(priority) + " WHERE ID='" + cardId + "'");
}

bool DatabaseManager::deleteKanbanCard(std::string cardId) {
    return executeQuery("DELETE FROM KanbanCards WHERE ID='" + cardId + "'");
}

bool DatabaseManager::moveCard(std::string cardId, std::string newListId, int newPosition) {
    return executeQuery("UPDATE KanbanCards SET ListID='" + newListId + "', Position=" + std::to_string(newPosition) + " WHERE ID='" + cardId + "'");
}

// =============================================================
// ARKADAŞLIK
// =============================================================

bool DatabaseManager::sendFriendRequest(std::string myId, std::string targetUserId) {
    if (myId == targetUserId) return false;
    return executeQuery("INSERT INTO Friends (RequesterID, TargetID, Status) VALUES ('" + myId + "', '" + targetUserId + "', 0);");
}

bool DatabaseManager::acceptFriendRequest(std::string requesterId, std::string myId) {
    return executeQuery("UPDATE Friends SET Status=1 WHERE RequesterID='" + requesterId + "' AND TargetID='" + myId + "'");
}

bool DatabaseManager::rejectOrRemoveFriend(std::string otherUserId, std::string myId) {
    return executeQuery("DELETE FROM Friends WHERE (RequesterID='" + otherUserId + "' AND TargetID='" + myId + "') OR (RequesterID='" + myId + "' AND TargetID='" + otherUserId + "');");
}

std::vector<FriendRequest> DatabaseManager::getPendingRequests(std::string myId) {
    std::vector<FriendRequest> reqs;
    std::string sql = "SELECT U.ID, U.Name, U.AvatarURL, F.CreatedAt FROM Users U JOIN Friends F ON U.ID=F.RequesterID WHERE F.TargetID='" + myId + "' AND F.Status=0;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            reqs.push_back({ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), SAFE_TEXT(3) });
        }
    }
    sqlite3_finalize(stmt);
    return reqs;
}

std::vector<User> DatabaseManager::getFriendsList(std::string myId) {
    std::vector<User> friends;
    std::string sql = "SELECT U.ID, U.Name, U.Email, U.Status, U.IsSystemAdmin, U.AvatarURL FROM Users U JOIN Friends F ON (U.ID=F.RequesterID OR U.ID=F.TargetID) WHERE (F.RequesterID='" + myId + "' OR F.TargetID='" + myId + "') AND F.Status=1 AND U.ID!='" + myId + "';";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            friends.push_back(User{ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), "", sqlite3_column_int(stmt, 4) != 0, SAFE_TEXT(3), SAFE_TEXT(5), 0, "", "" });
        }
    }
    sqlite3_finalize(stmt);
    return friends;
}

// =============================================================
// ABONELİK & LİMİT & ÖDEME
// =============================================================

bool DatabaseManager::isSubscriptionActive(std::string userId) {
    std::string sql = "SELECT SubscriptionLevel FROM Users WHERE ID = '" + userId + "';";
    sqlite3_stmt* stmt;
    bool active = false;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) if (sqlite3_column_int(stmt, 0) > 0) active = true;
    }
    sqlite3_finalize(stmt);
    return active;
}

int DatabaseManager::getUserServerCount(std::string userId) {
    std::string sql = "SELECT COUNT(*) FROM Servers WHERE OwnerID = '" + userId + "';";
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

bool DatabaseManager::updateUserSubscription(std::string userId, int level, int durationDays) {
    return executeQuery("UPDATE Users SET SubscriptionLevel=" + std::to_string(level) + ", SubscriptionExpiresAt=datetime('now', '+" + std::to_string(durationDays) + " days') WHERE ID='" + userId + "'");
}

bool DatabaseManager::createPaymentRecord(std::string userId, const std::string& providerId, float amount, const std::string& currency) {
    std::string id = Security::generateId(15);
    return executeQuery("INSERT INTO Payments (ID, UserID, ProviderPaymentID, Amount, Currency) VALUES ('" + id + "', '" + userId + "', '" + providerId + "', " + std::to_string(amount) + ", '" + currency + "');");
}

bool DatabaseManager::updatePaymentStatus(const std::string& providerId, const std::string& status) {
    return executeQuery("UPDATE Payments SET Status='" + status + "' WHERE ProviderPaymentID='" + providerId + "'");
}

std::vector<PaymentTransaction> DatabaseManager::getUserPayments(std::string userId) {
    std::vector<PaymentTransaction> payments;
    std::string sql = "SELECT ID, ProviderPaymentID, Amount, Currency, Status, CreatedAt FROM Payments WHERE UserID='" + userId + "'";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            payments.push_back({ SAFE_TEXT(0), userId, SAFE_TEXT(1), (float)sqlite3_column_double(stmt, 2), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5) });
        }
    }
    sqlite3_finalize(stmt);
    return payments;
}

// =============================================================
// RAPORLAMA
// =============================================================

bool DatabaseManager::createReport(std::string reporterId, std::string contentId, const std::string& type, const std::string& reason) {
    std::string id = Security::generateId(15);
    const char* sql = "INSERT INTO Reports (ID, ReporterID, ContentID, Type, Reason) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, reporterId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, contentId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, reason.c_str(), -1, SQLITE_STATIC);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

std::vector<UserReport> DatabaseManager::getOpenReports() {
    std::vector<UserReport> reports;
    std::string sql = "SELECT ID, ReporterID, ContentID, Type, Reason, Status FROM Reports WHERE Status='OPEN';";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            reports.push_back({ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5) });
        }
    }
    sqlite3_finalize(stmt);
    return reports;
}

std::string DatabaseManager::authenticateUser(const std::string& email, const std::string& password) {
    std::string query = "SELECT id, password FROM users WHERE email = ?;";
    sqlite3_stmt* stmt;
    std::string userId = "";
    std::string dbPasswordHash = "";

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            userId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            dbPasswordHash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        }
        sqlite3_finalize(stmt);
    }

    // 1. KONTROL: Kullanıcı DB'de var mı?
    if (userId.empty()) {
        std::cout << "[GIRIS HATASI] Veritabaninda bu e-posta kayitli degil: " << email << std::endl;
        return "";
    }

    // 2. KONTROL: Şifre (Argon2 Hash) eşleşiyor mu?
    if (Security::verifyPassword(password, dbPasswordHash)) {
        std::cout << "[GIRIS BASARILI] Kullanici dogrulandi: " << email << std::endl;
        return userId;
    }
    else {
        std::cout << "[GIRIS HATASI] Yanlis sifre girildi! E-posta: " << email << std::endl;
        return "";
    }
}

bool DatabaseManager::resolveReport(std::string reportId) {
    return executeQuery("UPDATE Reports SET Status='RESOLVED' WHERE ID='" + reportId + "'");
}
bool DatabaseManager::updateUserStatus(const std::string& userId, const std::string& newStatus) {
    // Sadece güvenlik için geçerli statülere izin veriyoruz
    if (newStatus != "Online" && newStatus != "Offline" && newStatus != "Away") {
        return false;
    }

    std::string query = "UPDATE users SET status = ? WHERE id = ?;";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, newStatus.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_TRANSIENT);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}