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

    // SaaS performansı ve Lock önleme için kritik PRAGMA ayarları
    executeQuery("PRAGMA foreign_keys = ON;");
    executeQuery("PRAGMA journal_mode = WAL;");
    executeQuery("PRAGMA synchronous = NORMAL;");
    executeQuery("PRAGMA temp_store = MEMORY;");

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
    std::string sql =
        "CREATE TABLE IF NOT EXISTS Users (ID TEXT PRIMARY KEY, Name TEXT NOT NULL, Email TEXT UNIQUE NOT NULL, PasswordHash TEXT, GoogleID TEXT UNIQUE, IsSystemAdmin INTEGER DEFAULT 0, Status TEXT DEFAULT 'Offline', AvatarURL TEXT, SubscriptionLevel INTEGER DEFAULT 0, SubscriptionExpiresAt DATETIME, LastSeen DATETIME DEFAULT CURRENT_TIMESTAMP, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP);"
        "CREATE INDEX IF NOT EXISTS idx_users_email ON Users(Email);"
        "CREATE TABLE IF NOT EXISTS Servers (ID TEXT PRIMARY KEY, OwnerID TEXT, Name TEXT NOT NULL, InviteCode TEXT UNIQUE, IconURL TEXT, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(OwnerID) REFERENCES Users(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Roles (ID TEXT PRIMARY KEY, ServerID TEXT, RoleName TEXT NOT NULL, Color TEXT DEFAULT '#FFFFFF', Hierarchy INTEGER DEFAULT 0, Permissions INTEGER DEFAULT 0, FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS ServerMembers (ServerID TEXT, UserID TEXT, Nickname TEXT, JoinedAt DATETIME DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (ServerID, UserID), FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE, FOREIGN KEY(UserID) REFERENCES Users(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Channels (ID TEXT PRIMARY KEY, ServerID TEXT, Name TEXT NOT NULL, Type INTEGER NOT NULL, FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Messages (ID TEXT PRIMARY KEY, ChannelID TEXT, SenderID TEXT, Content TEXT, AttachmentURL TEXT, Timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(ChannelID) REFERENCES Channels(ID) ON DELETE CASCADE, FOREIGN KEY(SenderID) REFERENCES Users(ID));"
        "CREATE INDEX IF NOT EXISTS idx_messages_channel ON Messages(ChannelID);"
        "CREATE TABLE IF NOT EXISTS Friends (RequesterID TEXT, TargetID TEXT, Status INTEGER DEFAULT 0, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (RequesterID, TargetID), FOREIGN KEY(RequesterID) REFERENCES Users(ID), FOREIGN KEY(TargetID) REFERENCES Users(ID));"
        "CREATE TABLE IF NOT EXISTS KanbanLists (ID TEXT PRIMARY KEY, ChannelID TEXT, Title TEXT, Position INTEGER, FOREIGN KEY(ChannelID) REFERENCES Channels(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS KanbanCards (ID TEXT PRIMARY KEY, ListID TEXT, Title TEXT, Description TEXT, Priority INTEGER, Position INTEGER, AssigneeID TEXT, IsCompleted INTEGER DEFAULT 0, AttachmentURL TEXT, DueDate DATETIME, WarningSent INTEGER DEFAULT 0, ExpiredSent INTEGER DEFAULT 0, FOREIGN KEY(ListID) REFERENCES KanbanLists(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Payments (ID TEXT PRIMARY KEY, UserID TEXT, ProviderPaymentID TEXT, Amount REAL, Currency TEXT, Status TEXT DEFAULT 'pending', CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(UserID) REFERENCES Users(ID));"
        "CREATE TABLE IF NOT EXISTS Reports (ID TEXT PRIMARY KEY, ReporterID TEXT, ContentID TEXT, Type TEXT, Reason TEXT, Status TEXT DEFAULT 'OPEN', CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(ReporterID) REFERENCES Users(ID));"
        "CREATE TABLE IF NOT EXISTS ServerLogs (ID INTEGER PRIMARY KEY AUTOINCREMENT, ServerID TEXT, Action TEXT, Details TEXT, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP);"
        "CREATE TABLE IF NOT EXISTS ServerInvites (ServerID TEXT, InviterID TEXT, InviteeID TEXT, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY(ServerID, InviteeID));"
        "CREATE TABLE IF NOT EXISTS BlockedUsers (UserID TEXT, BlockedID TEXT, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY(UserID, BlockedID));"
        "CREATE TABLE IF NOT EXISTS ServerMemberRoles (ServerID TEXT, UserID TEXT, RoleID TEXT, PRIMARY KEY(ServerID, UserID, RoleID));"
        "CREATE TABLE IF NOT EXISTS Notifications (ID INTEGER PRIMARY KEY AUTOINCREMENT, UserID TEXT, Message TEXT, Type TEXT, IsRead INTEGER DEFAULT 0, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(UserID) REFERENCES Users(ID) ON DELETE CASCADE);";

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
    std::string sql = "SELECT IsSystemAdmin FROM Users WHERE ID = ?;";
    sqlite3_stmt* stmt;
    bool isAdmin = false;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);
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
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, isAdmin ? 1 : 0);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::loginUser(const std::string& email, const std::string& rawPassword) {
    const char* sql = "SELECT PasswordHash FROM Users WHERE Email = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
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
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
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
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
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
    const char* sql = "UPDATE Users SET AvatarURL = ? WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, avatarUrl.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::updateUserDetails(std::string userId, const std::string& name, const std::string& status) {
    const char* sql = "UPDATE Users SET Name = ?, Status = ? WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, userId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::deleteUser(std::string userId) {
    const char* sql = "DELETE FROM Users WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::banUser(std::string userId) {
    return deleteUser(userId);
}

// =============================================================
// SUNUCU & ROL YÖNETİMİ
// =============================================================

std::string DatabaseManager::createServer(const std::string& name, std::string ownerId) {
    if (!isSubscriptionActive(ownerId) && getUserServerCount(ownerId) >= 1) {
        std::cout << "[UYARI] Standart kullanici sunucu limitine takildi (Maks 1): " << ownerId << std::endl;
        return "";
    }

    std::string checkUserSql = "SELECT ID FROM Users WHERE ID = ?;";
    sqlite3_stmt* checkStmt;
    bool userExists = false;
    if (sqlite3_prepare_v2(db, checkUserSql.c_str(), -1, &checkStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(checkStmt, 1, ownerId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(checkStmt) == SQLITE_ROW) userExists = true;
    }
    sqlite3_finalize(checkStmt);

    if (!userExists) {
        std::cout << "[HATA] Sunucu kurmaya calisan OwnerID veritabaninda bulunamadi!" << std::endl;
        return "";
    }

    std::string id = Security::generateId(15);
    std::string inviteCode = "INV-" + id;
    std::string sql = "INSERT INTO Servers (ID, OwnerID, Name, InviteCode) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return "";

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ownerId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, inviteCode.c_str(), -1, SQLITE_TRANSIENT);
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    if (success) {
        addMemberToServer(id, ownerId);
        createRole(id, "Admin", 100, 9999);
        return id;
    }
    return "";
}

bool DatabaseManager::updateServer(std::string serverId, const std::string& name, const std::string& iconUrl) {
    const char* sql = "UPDATE Servers SET Name = ?, IconURL = ? WHERE ID = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, iconUrl.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, serverId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::deleteServer(std::string serverId) {
    const char* sql = "DELETE FROM Servers WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

std::vector<Server> DatabaseManager::getUserServers(std::string userId) {
    std::vector<Server> servers;
    std::string sql = "SELECT S.ID, S.Name, S.OwnerID, S.InviteCode, S.IconURL, S.CreatedAt, "
        "(SELECT COUNT(*) FROM ServerMembers SM2 WHERE SM2.ServerID = S.ID) "
        "FROM Servers S JOIN ServerMembers SM ON S.ID = SM.ServerID WHERE SM.UserID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);
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
    std::string sql = "SELECT ID, Name, OwnerID, InviteCode, IconURL, CreatedAt FROM Servers WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Server> server = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        server = Server{ SAFE_TEXT(0), SAFE_TEXT(2), SAFE_TEXT(1), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5), 0, {} };
    }
    sqlite3_finalize(stmt);
    return server;
}

bool DatabaseManager::addMemberToServer(std::string serverId, std::string userId) {
    const char* sql = "INSERT INTO ServerMembers (ServerID, UserID) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::removeMemberFromServer(std::string serverId, std::string userId) {
    const char* sql = "DELETE FROM ServerMembers WHERE ServerID = ? AND UserID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::joinServerByCode(std::string userId, const std::string& inviteCode) {
    std::string sql = "SELECT ID FROM Servers WHERE InviteCode = ?;";
    sqlite3_stmt* stmt;
    std::string serverId = "";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, inviteCode.c_str(), -1, SQLITE_TRANSIENT);
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
    const char* sql = "INSERT INTO Roles (ID, ServerID, RoleName, Hierarchy, Permissions) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, serverId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, roleName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, hierarchy);
    sqlite3_bind_int(stmt, 5, permissions);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

std::vector<Role> DatabaseManager::getServerRoles(std::string serverId) {
    std::vector<Role> roles;
    std::string sql = "SELECT ID, RoleName, Color, Hierarchy, Permissions FROM Roles WHERE ServerID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT);
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
    const char* sql = "INSERT INTO Channels (ID, ServerID, Name, Type) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, serverId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, type);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::updateChannel(std::string channelId, const std::string& name) {
    const char* sql = "UPDATE Channels SET Name = ? WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, channelId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::deleteChannel(std::string channelId) {
    const char* sql = "DELETE FROM Channels WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, channelId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

std::vector<Channel> DatabaseManager::getServerChannels(std::string serverId) {
    std::vector<Channel> channels;
    std::string sql = "SELECT ID, Name, Type FROM Channels WHERE ServerID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            channels.push_back(Channel{ SAFE_TEXT(0), serverId, SAFE_TEXT(1), sqlite3_column_int(stmt, 2), false });
        }
    }
    sqlite3_finalize(stmt);
    return channels;
}

int DatabaseManager::getServerKanbanCount(std::string serverId) {
    std::string sql = "SELECT COUNT(*) FROM Channels WHERE ServerID = ? AND Type = 3;";
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT);
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

    std::string sql = "SELECT ID FROM Channels WHERE Name = ? AND ServerID = '0';";
    sqlite3_stmt* stmt;
    std::string channelId = "";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, dmName.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) channelId = SAFE_TEXT(0);
    }
    sqlite3_finalize(stmt);
    if (!channelId.empty()) return channelId;

    channelId = Security::generateId(15);
    const char* insertSql = "INSERT INTO Channels (ID, ServerID, Name, Type) VALUES (?, '0', ?, 0);";
    if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, channelId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, dmName.c_str(), -1, SQLITE_TRANSIENT);
        bool s = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        if (s) return channelId;
    }
    return "";
}

bool DatabaseManager::sendMessage(std::string channelId, std::string senderId, const std::string& content, const std::string& attachmentUrl) {
    std::string id = Security::generateId(15);
    const char* sql = "INSERT INTO Messages (ID, ChannelID, SenderID, Content, AttachmentURL) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, channelId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, senderId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, attachmentUrl.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::updateMessage(std::string messageId, const std::string& newContent) {
    const char* sql = "UPDATE Messages SET Content = ? WHERE ID = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, newContent.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, messageId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::deleteMessage(std::string messageId) {
    const char* sql = "DELETE FROM Messages WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, messageId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

std::vector<Message> DatabaseManager::getChannelMessages(std::string channelId, int limit) {
    std::vector<Message> messages;
    std::string sql = "SELECT M.ID, M.SenderID, U.Name, U.AvatarURL, M.Content, M.AttachmentURL, M.Timestamp FROM Messages M JOIN Users U ON M.SenderID = U.ID WHERE M.ChannelID = ? ORDER BY M.Timestamp ASC LIMIT ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, channelId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);
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
    std::string sql = "SELECT ID, Title, Position FROM KanbanLists WHERE ChannelID = ? ORDER BY Position ASC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, channelId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string listId = SAFE_TEXT(0);
            std::string title = SAFE_TEXT(1);
            int pos = sqlite3_column_int(stmt, 2);
            std::vector<KanbanCard> cards;

            std::string cardSql = "SELECT ID, Title, Description, Priority, Position FROM KanbanCards WHERE ListID = ? ORDER BY Position ASC;";
            sqlite3_stmt* cardStmt;
            if (sqlite3_prepare_v2(db, cardSql.c_str(), -1, &cardStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(cardStmt, 1, listId.c_str(), -1, SQLITE_TRANSIENT);
                while (sqlite3_step(cardStmt) == SQLITE_ROW) {
                    cards.push_back(KanbanCard{ SAFE_TEXT(0), listId, SAFE_TEXT(1), SAFE_TEXT(2), sqlite3_column_int(cardStmt, 3), sqlite3_column_int(cardStmt, 4) });
                }
            }
            sqlite3_finalize(cardStmt);
            board.push_back(KanbanList{ listId, title, pos, cards });
        }
    }
    sqlite3_finalize(stmt);
    return board;
}

bool DatabaseManager::createKanbanList(std::string boardChannelId, std::string title) {
    std::string id = Security::generateId(15);
    const char* sql = "INSERT INTO KanbanLists (ID, ChannelID, Title, Position) VALUES (?, ?, ?, 0);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, boardChannelId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::updateKanbanList(std::string listId, const std::string& title, int position) {
    const char* sql = "UPDATE KanbanLists SET Title = ?, Position = ? WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, position);
    sqlite3_bind_text(stmt, 3, listId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::deleteKanbanList(std::string listId) {
    const char* sql = "DELETE FROM KanbanLists WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, listId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::createKanbanCard(std::string listId, std::string title, std::string desc, int priority) {
    std::string id = Security::generateId(15);
    const char* sql = "INSERT INTO KanbanCards (ID, ListID, Title, Description, Priority, Position) VALUES (?, ?, ?, ?, ?, 0);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, listId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, desc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, priority);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::updateKanbanCard(std::string cardId, std::string title, std::string desc, int priority) {
    const char* sql = "UPDATE KanbanCards SET Title = ?, Description = ?, Priority = ? WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, desc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, priority);
    sqlite3_bind_text(stmt, 4, cardId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::deleteKanbanCard(std::string cardId) {
    const char* sql = "DELETE FROM KanbanCards WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, cardId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::moveCard(std::string cardId, std::string newListId, int newPosition) {
    const char* sql = "UPDATE KanbanCards SET ListID = ?, Position = ? WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, newListId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, newPosition);
    sqlite3_bind_text(stmt, 3, cardId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

// =============================================================
// ARKADAŞLIK
// =============================================================

std::vector<User> DatabaseManager::searchUsers(const std::string& searchQuery) {
    std::vector<User> users;
    std::string sql = "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL FROM Users WHERE Name LIKE ? OR Email LIKE ? LIMIT 20;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        std::string likeTerm = "%" + searchQuery + "%";
        sqlite3_bind_text(stmt, 1, likeTerm.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, likeTerm.c_str(), -1, SQLITE_TRANSIENT);

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

bool DatabaseManager::sendFriendRequest(std::string myId, std::string targetUserId) {
    if (myId == targetUserId) return false;
    const char* sql = "INSERT INTO Friends (RequesterID, TargetID, Status) VALUES (?, ?, 0);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, myId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, targetUserId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::acceptFriendRequest(std::string requesterId, std::string myId) {
    const char* sql = "UPDATE Friends SET Status=1 WHERE RequesterID = ? AND TargetID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, requesterId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, myId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::rejectOrRemoveFriend(std::string otherUserId, std::string myId) {
    const char* sql = "DELETE FROM Friends WHERE (RequesterID = ? AND TargetID = ?) OR (RequesterID = ? AND TargetID = ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, otherUserId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, myId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, myId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, otherUserId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

std::vector<FriendRequest> DatabaseManager::getPendingRequests(std::string myId) {
    std::vector<FriendRequest> reqs;
    std::string sql = "SELECT U.ID, U.Name, U.AvatarURL, F.CreatedAt FROM Users U JOIN Friends F ON U.ID=F.RequesterID WHERE F.TargetID = ? AND F.Status=0;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, myId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            reqs.push_back({ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), SAFE_TEXT(3) });
        }
    }
    sqlite3_finalize(stmt);
    return reqs;
}

std::vector<User> DatabaseManager::getFriendsList(std::string myId) {
    std::vector<User> friends;
    std::string sql = "SELECT U.ID, U.Name, U.Email, U.Status, U.IsSystemAdmin, U.AvatarURL FROM Users U JOIN Friends F ON (U.ID=F.RequesterID OR U.ID=F.TargetID) WHERE (F.RequesterID = ? OR F.TargetID = ?) AND F.Status=1 AND U.ID != ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, myId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, myId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, myId.c_str(), -1, SQLITE_TRANSIENT);
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
    std::string sql = "SELECT SubscriptionLevel FROM Users WHERE ID = ?;";
    sqlite3_stmt* stmt;
    bool active = false;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) if (sqlite3_column_int(stmt, 0) > 0) active = true;
    }
    sqlite3_finalize(stmt);
    return active;
}

int DatabaseManager::getUserServerCount(std::string userId) {
    std::string sql = "SELECT COUNT(*) FROM Servers WHERE OwnerID = ?;";
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

bool DatabaseManager::updateUserSubscription(std::string userId, int level, int durationDays) {
    std::string sql = "UPDATE Users SET SubscriptionLevel=?, SubscriptionExpiresAt=datetime('now', '+" + std::to_string(durationDays) + " days') WHERE ID=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, level);
    sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::createPaymentRecord(std::string userId, const std::string& providerId, float amount, const std::string& currency) {
    std::string id = Security::generateId(15);
    const char* sql = "INSERT INTO Payments (ID, UserID, ProviderPaymentID, Amount, Currency) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, providerId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, amount);
    sqlite3_bind_text(stmt, 5, currency.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

bool DatabaseManager::updatePaymentStatus(const std::string& providerId, const std::string& status) {
    const char* sql = "UPDATE Payments SET Status = ? WHERE ProviderPaymentID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, providerId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

std::vector<PaymentTransaction> DatabaseManager::getUserPayments(std::string userId) {
    std::vector<PaymentTransaction> payments;
    std::string sql = "SELECT ID, ProviderPaymentID, Amount, Currency, Status, CreatedAt FROM Payments WHERE UserID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);
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
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, reporterId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, contentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, reason.c_str(), -1, SQLITE_TRANSIENT);
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

bool DatabaseManager::resolveReport(std::string reportId) {
    const char* sql = "UPDATE Reports SET Status='RESOLVED' WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, reportId.c_str(), -1, SQLITE_TRANSIENT);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return s;
}

std::string DatabaseManager::authenticateUser(const std::string& email, const std::string& password) {
    std::string query = "SELECT ID, PasswordHash FROM Users WHERE Email = ?;";
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

    if (userId.empty()) {
        std::cout << "[GIRIS HATASI] Veritabaninda bu e-posta kayitli degil: " << email << std::endl;
        return "";
    }

    if (Security::verifyPassword(password, dbPasswordHash)) {
        std::cout << "[GIRIS BASARILI] Kullanici dogrulandi: " << email << std::endl;
        return userId;
    }
    else {
        std::cout << "[GIRIS HATASI] Yanlis sifre girildi! E-posta: " << email << std::endl;
        return "";
    }
}

bool DatabaseManager::updateLastSeen(const std::string& userId) {
    std::string query = "UPDATE Users SET LastSeen = CURRENT_TIMESTAMP, Status = 'Online' WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

void DatabaseManager::markInactiveUsersOffline(int timeoutSeconds) {
    std::string sql = "UPDATE Users SET Status = 'Offline' WHERE Status = 'Online' AND (julianday('now') - julianday(LastSeen)) * 86400 > " + std::to_string(timeoutSeconds) + ";";
    executeQuery(sql);
}

bool DatabaseManager::updateUserStatus(const std::string& userId, const std::string& newStatus) {
    if (newStatus != "Online" && newStatus != "Offline" && newStatus != "Away") {
        return false;
    }
    std::string query = "UPDATE Users SET Status = ? WHERE ID = ?;";
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

std::vector<Server> DatabaseManager::getAllServers() {
    std::vector<Server> servers;
    std::string sql = "SELECT S.ID, S.Name, S.OwnerID, S.InviteCode, S.IconURL, S.CreatedAt, "
        "(SELECT COUNT(*) FROM ServerMembers SM WHERE SM.ServerID = S.ID) FROM Servers S;";
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

bool DatabaseManager::sendServerInvite(std::string serverId, std::string inviterId, std::string inviteeId) {
    if (inviterId == inviteeId) return false;
    std::string sql = "INSERT OR IGNORE INTO ServerInvites (ServerID, InviterID, InviteeID) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, inviterId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, inviteeId.c_str(), -1, SQLITE_TRANSIENT);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

std::vector<DatabaseManager::ServerInviteDTO> DatabaseManager::getPendingServerInvites(std::string userId) {
    std::vector<ServerInviteDTO> invites;
    std::string sql = "SELECT I.ServerID, S.Name, U.Name, I.CreatedAt FROM ServerInvites I "
        "JOIN Servers S ON I.ServerID = S.ID "
        "JOIN Users U ON I.InviterID = U.ID WHERE I.InviteeID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            invites.push_back({ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), SAFE_TEXT(3) });
        }
    }
    sqlite3_finalize(stmt);
    return invites;
}

bool DatabaseManager::resolveServerInvite(std::string serverId, std::string inviteeId, bool accept) {
    std::string sql = "DELETE FROM ServerInvites WHERE ServerID = ? AND InviteeID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, inviteeId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    if (accept) {
        return addMemberToServer(serverId, inviteeId);
    }
    return true;
}

std::vector<DatabaseManager::ServerMemberDetail> DatabaseManager::getServerMembersDetails(const std::string& serverId) {
    std::vector<ServerMemberDetail> members;
    std::string sql = "SELECT U.ID, U.Name, U.Status FROM ServerMembers SM JOIN Users U ON SM.UserID = U.ID WHERE SM.ServerID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            members.push_back({ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2) });
        }
    }
    sqlite3_finalize(stmt);
    return members;
}

bool DatabaseManager::logServerAction(const std::string& serverId, const std::string& action, const std::string& details) {
    std::string sql = "INSERT INTO ServerLogs (ServerID, Action, Details) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, action.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, details.c_str(), -1, SQLITE_TRANSIENT);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

std::vector<DatabaseManager::ServerLog> DatabaseManager::getServerLogs(const std::string& serverId) {
    std::vector<ServerLog> logs;
    std::string sql = "SELECT CreatedAt, Action, Details FROM ServerLogs WHERE ServerID = ? ORDER BY CreatedAt DESC LIMIT 50;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            logs.push_back({ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2) });
        }
    }
    sqlite3_finalize(stmt);
    return logs;
}

std::string DatabaseManager::getChannelServerId(const std::string& channelId) {
    std::string srvId = "";
    std::string sql = "SELECT ServerID FROM Channels WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, channelId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) srvId = SAFE_TEXT(0);
    }
    sqlite3_finalize(stmt); return srvId;
}

std::string DatabaseManager::getChannelName(const std::string& channelId) {
    std::string name = "";
    std::string sql = "SELECT Name FROM Channels WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, channelId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) name = SAFE_TEXT(0);
    }
    sqlite3_finalize(stmt); return name;
}

void DatabaseManager::processKanbanNotifications() {
    std::string sqlWarning = "SELECT ID, Title, AssigneeID FROM KanbanCards WHERE IsCompleted = 0 AND WarningSent = 0 AND DueDate IS NOT NULL AND (julianday(DueDate) - julianday('now', 'localtime')) <= (1.0/24.0) AND (julianday(DueDate) - julianday('now', 'localtime')) > 0 AND AssigneeID != '';";
    std::string sqlExpired = "SELECT ID, Title, AssigneeID FROM KanbanCards WHERE IsCompleted = 0 AND ExpiredSent = 0 AND DueDate IS NOT NULL AND (julianday(DueDate) - julianday('now', 'localtime')) <= 0 AND AssigneeID != '';";

    sqlite3_stmt* stmt;

    std::string insertNotif = "INSERT INTO Notifications (UserID, Message, Type) VALUES (?, ?, ?);";
    sqlite3_stmt* insertStmt;
    sqlite3_prepare_v2(db, insertNotif.c_str(), -1, &insertStmt, nullptr);

    std::string updateWarnCard = "UPDATE KanbanCards SET WarningSent = 1 WHERE ID = ?;";
    sqlite3_stmt* updateWarnStmt;
    sqlite3_prepare_v2(db, updateWarnCard.c_str(), -1, &updateWarnStmt, nullptr);

    std::string updateExpCard = "UPDATE KanbanCards SET ExpiredSent = 1 WHERE ID = ?;";
    sqlite3_stmt* updateExpStmt;
    sqlite3_prepare_v2(db, updateExpCard.c_str(), -1, &updateExpStmt, nullptr);

    // UYARILARI GÖNDER
    if (sqlite3_prepare_v2(db, sqlWarning.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string cardId = SAFE_TEXT(0); std::string title = SAFE_TEXT(1); std::string assignee = SAFE_TEXT(2);
            std::string msg = "Yaklasan Gorev: '" + title + "' icin son 1 saatiniz kaldi!";

            sqlite3_bind_text(insertStmt, 1, assignee.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(insertStmt, 2, msg.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(insertStmt, 3, "WARNING", -1, SQLITE_TRANSIENT);
            sqlite3_step(insertStmt);
            sqlite3_reset(insertStmt);

            sqlite3_bind_text(updateWarnStmt, 1, cardId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(updateWarnStmt);
            sqlite3_reset(updateWarnStmt);
        }
    }
    sqlite3_finalize(stmt);

    // SÜRESİ BİTENLERİ GÖNDER
    if (sqlite3_prepare_v2(db, sqlExpired.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string cardId = SAFE_TEXT(0); std::string title = SAFE_TEXT(1); std::string assignee = SAFE_TEXT(2);
            std::string msg = "Suresi Doldu: '" + title + "' adli gorevin teslim suresi gecti!";

            sqlite3_bind_text(insertStmt, 1, assignee.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(insertStmt, 2, msg.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(insertStmt, 3, "EXPIRED", -1, SQLITE_TRANSIENT);
            sqlite3_step(insertStmt);
            sqlite3_reset(insertStmt);

            sqlite3_bind_text(updateExpStmt, 1, cardId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(updateExpStmt);
            sqlite3_reset(updateExpStmt);
        }
    }
    sqlite3_finalize(stmt);

    sqlite3_finalize(insertStmt);
    sqlite3_finalize(updateWarnStmt);
    sqlite3_finalize(updateExpStmt);
}

std::vector<DatabaseManager::NotificationDTO> DatabaseManager::getUserNotifications(const std::string& userId) {
    std::vector<NotificationDTO> notifs;
    std::string sql = "SELECT ID, Message, Type, CreatedAt FROM Notifications WHERE UserID = ? AND IsRead = 0 ORDER BY CreatedAt DESC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            notifs.push_back({ sqlite3_column_int(stmt, 0), SAFE_TEXT(1), SAFE_TEXT(2), SAFE_TEXT(3) });
        }
    }
    sqlite3_finalize(stmt);
    return notifs;
}

bool DatabaseManager::markNotificationAsRead(int notifId) {
    std::string sql = "UPDATE Notifications SET IsRead = 1 WHERE ID = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, notifId);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}