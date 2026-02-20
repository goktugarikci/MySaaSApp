#include "DatabaseManager.h"
#include "../utils/Security.h"
#include <iostream>
#include <algorithm> 

#define SAFE_TEXT(col) (reinterpret_cast<const char*>(sqlite3_column_text(stmt, col)) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, col)) : "")

DatabaseManager::DatabaseManager(const std::string& path) : db_path(path), db(nullptr) {}
DatabaseManager::~DatabaseManager() { close(); }

bool DatabaseManager::open() {
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc) return false;
    executeQuery("PRAGMA foreign_keys = ON;");
    return true;
}

void DatabaseManager::close() {
    if (db) { sqlite3_close(db); db = nullptr; }
}

bool DatabaseManager::executeQuery(const std::string& sql) {
    char* zErrMsg = 0;
    int rc = sqlite3_exec(db, sql.c_str(), 0, 0, &zErrMsg);
    if (rc != SQLITE_OK) { sqlite3_free(zErrMsg); return false; }
    return true;
}

bool DatabaseManager::initTables() {
    std::string sql =
        "CREATE TABLE IF NOT EXISTS Users (ID TEXT PRIMARY KEY, Name TEXT NOT NULL, Email TEXT UNIQUE NOT NULL, PasswordHash TEXT, GoogleID TEXT UNIQUE, IsSystemAdmin INTEGER DEFAULT 0, Status TEXT DEFAULT 'Offline', AvatarURL TEXT, SubscriptionLevel INTEGER DEFAULT 0, SubscriptionExpiresAt DATETIME, LastSeen DATETIME DEFAULT CURRENT_TIMESTAMP, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP);"
        "CREATE TABLE IF NOT EXISTS Servers (ID TEXT PRIMARY KEY, OwnerID TEXT, Name TEXT NOT NULL, InviteCode TEXT UNIQUE, IconURL TEXT, Settings TEXT DEFAULT '{}', CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(OwnerID) REFERENCES Users(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Roles (ID TEXT PRIMARY KEY, ServerID TEXT, RoleName TEXT NOT NULL, Color TEXT DEFAULT '#FFFFFF', Hierarchy INTEGER DEFAULT 0, Permissions INTEGER DEFAULT 0, FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS ServerMembers (ServerID TEXT, UserID TEXT, Nickname TEXT, JoinedAt DATETIME DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (ServerID, UserID), FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE, FOREIGN KEY(UserID) REFERENCES Users(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Channels (ID TEXT PRIMARY KEY, ServerID TEXT, Name TEXT NOT NULL, Type INTEGER NOT NULL, IsPrivate INTEGER DEFAULT 0, FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS ChannelMembers (ChannelID TEXT, UserID TEXT, PRIMARY KEY(ChannelID, UserID), FOREIGN KEY(ChannelID) REFERENCES Channels(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Messages (ID TEXT PRIMARY KEY, ChannelID TEXT, SenderID TEXT, Content TEXT, AttachmentURL TEXT, Timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(ChannelID) REFERENCES Channels(ID) ON DELETE CASCADE, FOREIGN KEY(SenderID) REFERENCES Users(ID));"
        "CREATE TABLE IF NOT EXISTS MessageReactions (MessageID TEXT, UserID TEXT, Reaction TEXT, PRIMARY KEY(MessageID, UserID, Reaction), FOREIGN KEY(MessageID) REFERENCES Messages(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS ThreadReplies (ID TEXT PRIMARY KEY, MessageID TEXT, SenderID TEXT, Content TEXT, Timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(MessageID) REFERENCES Messages(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Friends (RequesterID TEXT, TargetID TEXT, Status INTEGER DEFAULT 0, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (RequesterID, TargetID), FOREIGN KEY(RequesterID) REFERENCES Users(ID), FOREIGN KEY(TargetID) REFERENCES Users(ID));"
        "CREATE TABLE IF NOT EXISTS KanbanLists (ID TEXT PRIMARY KEY, ChannelID TEXT, Title TEXT, Position INTEGER, FOREIGN KEY(ChannelID) REFERENCES Channels(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS KanbanCards (ID TEXT PRIMARY KEY, ListID TEXT, Title TEXT, Description TEXT, Priority INTEGER, Position INTEGER, AssigneeID TEXT, IsCompleted INTEGER DEFAULT 0, AttachmentURL TEXT, DueDate DATETIME, WarningSent INTEGER DEFAULT 0, ExpiredSent INTEGER DEFAULT 0, FOREIGN KEY(ListID) REFERENCES KanbanLists(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS KanbanComments (ID TEXT PRIMARY KEY, CardID TEXT, UserID TEXT, Content TEXT, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(CardID) REFERENCES KanbanCards(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS KanbanTags (CardID TEXT, Tag TEXT, PRIMARY KEY(CardID, Tag), FOREIGN KEY(CardID) REFERENCES KanbanCards(ID) ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS Payments (ID TEXT PRIMARY KEY, UserID TEXT, ProviderPaymentID TEXT, Amount REAL, Currency TEXT, Status TEXT DEFAULT 'pending', CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(UserID) REFERENCES Users(ID));"
        "CREATE TABLE IF NOT EXISTS Reports (ID TEXT PRIMARY KEY, ReporterID TEXT, ContentID TEXT, Type TEXT, Reason TEXT, Status TEXT DEFAULT 'OPEN', CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(ReporterID) REFERENCES Users(ID));"
        "CREATE TABLE IF NOT EXISTS ServerLogs (ID INTEGER PRIMARY KEY AUTOINCREMENT, ServerID TEXT, Action TEXT, Details TEXT, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP);"
        "CREATE TABLE IF NOT EXISTS ServerInvites (ServerID TEXT, InviterID TEXT, InviteeID TEXT, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY(ServerID, InviteeID));"
        "CREATE TABLE IF NOT EXISTS BlockedUsers (UserID TEXT, BlockedID TEXT, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY(UserID, BlockedID));"
        "CREATE TABLE IF NOT EXISTS ServerMemberRoles (ServerID TEXT, UserID TEXT, RoleID TEXT, PRIMARY KEY(ServerID, UserID, RoleID));"
        "CREATE TABLE IF NOT EXISTS Notifications (ID INTEGER PRIMARY KEY AUTOINCREMENT, UserID TEXT, Message TEXT, Type TEXT, IsRead INTEGER DEFAULT 0, CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(UserID) REFERENCES Users(ID) ON DELETE CASCADE);";
    return executeQuery(sql);
}

// -------------------------------------------------------------
// SİSTEM / ADMIN İŞLEMLERİ (YENİ EKLENENLER)
// -------------------------------------------------------------
SystemStats DatabaseManager::getSystemStats() {
    SystemStats stats = { 0, 0, 0 }; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM Users;", -1, &stmt, nullptr) == SQLITE_OK) { if (sqlite3_step(stmt) == SQLITE_ROW) stats.user_count = sqlite3_column_int(stmt, 0); } sqlite3_finalize(stmt);
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM Servers;", -1, &stmt, nullptr) == SQLITE_OK) { if (sqlite3_step(stmt) == SQLITE_ROW) stats.server_count = sqlite3_column_int(stmt, 0); } sqlite3_finalize(stmt);
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM Messages;", -1, &stmt, nullptr) == SQLITE_OK) { if (sqlite3_step(stmt) == SQLITE_ROW) stats.message_count = sqlite3_column_int(stmt, 0); } sqlite3_finalize(stmt);
    return stats;
}

std::vector<ServerLog> DatabaseManager::getSystemLogs(int limit) {
    std::vector<ServerLog> logs; std::string sql = "SELECT CreatedAt, Action, Details FROM ServerLogs ORDER BY CreatedAt DESC LIMIT " + std::to_string(limit) + ";"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) logs.push_back({ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2) });
    } sqlite3_finalize(stmt); return logs;
}

std::vector<Message> DatabaseManager::getArchivedMessages(int limit) {
    std::vector<Message> msgs; std::string sql = "SELECT M.ID, M.SenderID, U.Name, U.AvatarURL, M.Content, M.AttachmentURL, M.Timestamp FROM Messages M JOIN Users U ON M.SenderID = U.ID ORDER BY M.Timestamp DESC LIMIT " + std::to_string(limit) + ";"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) msgs.push_back({ SAFE_TEXT(0), "SYSTEM", SAFE_TEXT(1), SAFE_TEXT(2), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5), SAFE_TEXT(6) });
    } sqlite3_finalize(stmt); return msgs;
}

// -------------------------------------------------------------
// KANAL YÖNETİMİ & PRIVATE (ÖZEL) KANALLAR
// -------------------------------------------------------------
bool DatabaseManager::createChannel(std::string serverId, std::string name, int type) { return createChannel(serverId, name, type, false); }
bool DatabaseManager::createChannel(std::string serverId, std::string name, int type, bool isPrivate) {
    if (type == 3 && getServerKanbanCount(serverId) >= 1) return false;
    std::string id = Security::generateId(15); std::string sql = "INSERT INTO Channels (ID, ServerID, Name, Type, IsPrivate) VALUES (?, ?, ?, ?, ?);"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, serverId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_int(stmt, 4, type); sqlite3_bind_int(stmt, 5, isPrivate ? 1 : 0);
        bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s;
    } return false;
}

std::vector<Channel> DatabaseManager::getServerChannels(std::string serverId) { return getServerChannels(serverId, ""); }
std::vector<Channel> DatabaseManager::getServerChannels(std::string serverId, std::string userId) {
    std::vector<Channel> channels; std::string sql = "SELECT ID, Name, Type, IsPrivate FROM Channels WHERE ServerID = ?;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string chId = SAFE_TEXT(0); int isPrivate = sqlite3_column_int(stmt, 3);
            if (isPrivate == 0 || (!userId.empty() && hasChannelAccess(chId, userId))) {
                channels.push_back(Channel{ chId, serverId, SAFE_TEXT(1), sqlite3_column_int(stmt, 2), (isPrivate == 1) });
            }
        }
    } sqlite3_finalize(stmt); return channels;
}

bool DatabaseManager::hasChannelAccess(std::string channelId, std::string userId) {
    std::string sql = "SELECT IsPrivate FROM Channels WHERE ID = ?;"; sqlite3_stmt* stmt; bool hasAccess = true;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, channelId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            if (sqlite3_column_int(stmt, 0) == 1) {
                sqlite3_stmt* stmt2; std::string sql2 = "SELECT 1 FROM ChannelMembers WHERE ChannelID = ? AND UserID = ?;";
                if (sqlite3_prepare_v2(db, sql2.c_str(), -1, &stmt2, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt2, 1, channelId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt2, 2, userId.c_str(), -1, SQLITE_TRANSIENT); hasAccess = (sqlite3_step(stmt2) == SQLITE_ROW); } sqlite3_finalize(stmt2);
            }
        }
    } sqlite3_finalize(stmt); return hasAccess;
}
bool DatabaseManager::addMemberToChannel(std::string channelId, std::string userId) {
    std::string sql = "INSERT OR IGNORE INTO ChannelMembers (ChannelID, UserID) VALUES (?, ?);"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, channelId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_TRANSIENT); bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s; } return false;
}
bool DatabaseManager::removeMemberFromChannel(std::string channelId, std::string userId) { return executeQuery("DELETE FROM ChannelMembers WHERE ChannelID='" + channelId + "' AND UserID='" + userId + "'"); }

// -------------------------------------------------------------
// MESAJ TEPKİLERİ (REACTIONS) & THREADS
// -------------------------------------------------------------
bool DatabaseManager::addMessageReaction(std::string messageId, std::string userId, std::string reaction) {
    std::string sql = "INSERT OR IGNORE INTO MessageReactions (MessageID, UserID, Reaction) VALUES (?, ?, ?);"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, messageId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 3, reaction.c_str(), -1, SQLITE_TRANSIENT); bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s; } return false;
}
bool DatabaseManager::removeMessageReaction(std::string messageId, std::string userId, std::string reaction) { return executeQuery("DELETE FROM MessageReactions WHERE MessageID='" + messageId + "' AND UserID='" + userId + "' AND Reaction='" + reaction + "'"); }

bool DatabaseManager::addThreadReply(std::string messageId, std::string senderId, std::string content) {
    std::string id = Security::generateId(15); std::string sql = "INSERT INTO ThreadReplies (ID, MessageID, SenderID, Content) VALUES (?, ?, ?, ?);"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, messageId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 3, senderId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 4, content.c_str(), -1, SQLITE_TRANSIENT); bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s; } return false;
}
std::vector<Message> DatabaseManager::getThreadReplies(std::string messageId) {
    std::vector<Message> replies; std::string sql = "SELECT T.ID, T.SenderID, U.Name, U.AvatarURL, T.Content, T.Timestamp FROM ThreadReplies T JOIN Users U ON T.SenderID = U.ID WHERE T.MessageID = ? ORDER BY T.Timestamp ASC;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, messageId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) replies.push_back({ SAFE_TEXT(0), messageId, SAFE_TEXT(1), SAFE_TEXT(2), SAFE_TEXT(3), SAFE_TEXT(4), "", SAFE_TEXT(5) });
    } sqlite3_finalize(stmt); return replies;
}

// -------------------------------------------------------------
// KANBAN KARTLARI YORUMLAR VE ETİKETLER
// -------------------------------------------------------------
bool DatabaseManager::createKanbanCard(std::string listId, std::string title, std::string desc, int priority) { return createKanbanCard(listId, title, desc, priority, "", "", ""); }
bool DatabaseManager::createKanbanCard(std::string listId, std::string title, std::string desc, int priority, std::string assigneeId, std::string attachmentUrl, std::string dueDate) {
    std::string id = Security::generateId(15); sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "INSERT INTO KanbanCards (ID, ListID, Title, Description, Priority, Position, AssigneeID, AttachmentURL, DueDate) VALUES (?, ?, ?, ?, ?, 0, ?, ?, ?);", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, listId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 4, desc.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_int(stmt, 5, priority); sqlite3_bind_text(stmt, 6, assigneeId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 7, attachmentUrl.c_str(), -1, SQLITE_TRANSIENT);
        if (dueDate.empty()) sqlite3_bind_null(stmt, 8); else sqlite3_bind_text(stmt, 8, dueDate.c_str(), -1, SQLITE_TRANSIENT);
        bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s;
    } return false;
}
std::vector<CardComment> DatabaseManager::getCardComments(std::string cardId) {
    std::vector<CardComment> comments; std::string sql = "SELECT ID, UserID, Content, CreatedAt FROM KanbanComments WHERE CardID = ? ORDER BY CreatedAt ASC;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, cardId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) comments.push_back({ SAFE_TEXT(0), cardId, SAFE_TEXT(1), SAFE_TEXT(2), SAFE_TEXT(3) });
    } sqlite3_finalize(stmt); return comments;
}
bool DatabaseManager::addCardComment(std::string cardId, std::string userId, std::string content) {
    std::string id = Security::generateId(15); std::string sql = "INSERT INTO KanbanComments (ID, CardID, UserID, Content) VALUES (?, ?, ?, ?);"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, cardId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 3, userId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 4, content.c_str(), -1, SQLITE_TRANSIENT); bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s; } return false;
}
bool DatabaseManager::deleteCardComment(std::string commentId) { return executeQuery("DELETE FROM KanbanComments WHERE ID='" + commentId + "'"); }
std::vector<std::string> DatabaseManager::getCardTags(std::string cardId) {
    std::vector<std::string> tags; std::string sql = "SELECT Tag FROM KanbanTags WHERE CardID = ?;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, cardId.c_str(), -1, SQLITE_TRANSIENT); while (sqlite3_step(stmt) == SQLITE_ROW) tags.push_back(SAFE_TEXT(0)); } sqlite3_finalize(stmt); return tags;
}
bool DatabaseManager::addCardTag(std::string cardId, std::string tag) {
    std::string sql = "INSERT OR IGNORE INTO KanbanTags (CardID, Tag) VALUES (?, ?);"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, cardId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, tag.c_str(), -1, SQLITE_TRANSIENT); bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s; } return false;
}
bool DatabaseManager::removeCardTag(std::string cardId, std::string tag) { return executeQuery("DELETE FROM KanbanTags WHERE CardID='" + cardId + "' AND Tag='" + tag + "'"); }


// -------------------------------------------------------------
// STANDART / ESKİ CRUD'LAR (Aynen Kalıyor)
// -------------------------------------------------------------
std::vector<User> DatabaseManager::getAllUsers() {
    std::vector<User> users; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL FROM Users;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) users.push_back(User{ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), "", sqlite3_column_int(stmt, 4) != 0, SAFE_TEXT(3), SAFE_TEXT(5), 0, "", "" });
    } sqlite3_finalize(stmt); return users;
}
bool DatabaseManager::isSystemAdmin(std::string userId) {
    sqlite3_stmt* stmt; bool isAdmin = false;
    if (sqlite3_prepare_v2(db, "SELECT IsSystemAdmin FROM Users WHERE ID = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) isAdmin = (sqlite3_column_int(stmt, 0) == 1);
    } sqlite3_finalize(stmt); return isAdmin;
}
bool DatabaseManager::createGoogleUser(const std::string& name, const std::string& email, const std::string& googleId, const std::string& avatarUrl) {
    std::string id = Security::generateId(15); sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "INSERT INTO Users (ID, Name, Email, GoogleID, AvatarURL, IsSystemAdmin) VALUES (?, ?, ?, ?, ?, 0);", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 3, email.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 4, googleId.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 5, avatarUrl.c_str(), -1, SQLITE_STATIC);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s;
}
std::optional<User> DatabaseManager::getUserByGoogleId(const std::string& googleId) {
    sqlite3_stmt* stmt; std::optional<User> user = std::nullopt;
    if (sqlite3_prepare_v2(db, "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL, SubscriptionLevel, SubscriptionExpiresAt, GoogleID FROM Users WHERE GoogleID = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, googleId.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) user = User{ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), "", sqlite3_column_int(stmt, 4) != 0, SAFE_TEXT(3), SAFE_TEXT(5), sqlite3_column_int(stmt, 6), SAFE_TEXT(7), SAFE_TEXT(8) };
    } sqlite3_finalize(stmt); return user;
}
bool DatabaseManager::createUser(const std::string& name, const std::string& email, const std::string& rawPassword, bool isAdmin) {
    std::string hash = Security::hashPassword(rawPassword); if (hash.empty()) return false;
    std::string id = Security::generateId(15); sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "INSERT INTO Users (ID, Name, Email, PasswordHash, IsSystemAdmin) VALUES (?, ?, ?, ?, ?);", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 3, email.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 4, hash.c_str(), -1, SQLITE_STATIC); sqlite3_bind_int(stmt, 5, isAdmin ? 1 : 0);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s;
}
std::optional<User> DatabaseManager::getUser(const std::string& email) {
    sqlite3_stmt* stmt; std::optional<User> user = std::nullopt;
    if (sqlite3_prepare_v2(db, "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL, SubscriptionLevel, SubscriptionExpiresAt, GoogleID FROM Users WHERE Email = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) user = User{ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), "", sqlite3_column_int(stmt, 4) != 0, SAFE_TEXT(3), SAFE_TEXT(5), sqlite3_column_int(stmt, 6), SAFE_TEXT(7), SAFE_TEXT(8) };
    } sqlite3_finalize(stmt); return user;
}
std::optional<User> DatabaseManager::getUserById(std::string id) {
    sqlite3_stmt* stmt; std::optional<User> user = std::nullopt;
    if (sqlite3_prepare_v2(db, "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL, SubscriptionLevel, SubscriptionExpiresAt, GoogleID FROM Users WHERE ID = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) user = User{ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), "", sqlite3_column_int(stmt, 4) != 0, SAFE_TEXT(3), SAFE_TEXT(5), sqlite3_column_int(stmt, 6), SAFE_TEXT(7), SAFE_TEXT(8) };
    } sqlite3_finalize(stmt); return user;
}
std::string DatabaseManager::authenticateUser(const std::string& email, const std::string& password) {
    sqlite3_stmt* stmt; std::string userId = "", dbPasswordHash = "";
    if (sqlite3_prepare_v2(db, "SELECT ID, PasswordHash FROM Users WHERE Email = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) { userId = SAFE_TEXT(0); dbPasswordHash = SAFE_TEXT(1); }
        sqlite3_finalize(stmt);
    }
    if (userId.empty() || !Security::verifyPassword(password, dbPasswordHash)) return ""; return userId;
}
bool DatabaseManager::updateUserAvatar(std::string userId, const std::string& avatarUrl) { return executeQuery("UPDATE Users SET AvatarURL = '" + avatarUrl + "' WHERE ID = '" + userId + "';"); }
bool DatabaseManager::updateUserDetails(std::string userId, const std::string& name, const std::string& status) {
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "UPDATE Users SET Name = ?, Status = ? WHERE ID = ?;", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 2, status.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 3, userId.c_str(), -1, SQLITE_STATIC);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s;
}
bool DatabaseManager::deleteUser(std::string userId) { return executeQuery("DELETE FROM Users WHERE ID = '" + userId + "'"); }
bool DatabaseManager::banUser(std::string userId) { return deleteUser(userId); }
bool DatabaseManager::updateLastSeen(const std::string& userId) { return executeQuery("UPDATE Users SET LastSeen = CURRENT_TIMESTAMP, Status = 'Online' WHERE ID = '" + userId + "';"); }
void DatabaseManager::markInactiveUsersOffline(int timeoutSeconds) { executeQuery("UPDATE Users SET Status = 'Offline' WHERE Status = 'Online' AND (julianday('now') - julianday(LastSeen)) * 86400 > " + std::to_string(timeoutSeconds) + ";"); }
bool DatabaseManager::updateUserStatus(const std::string& userId, const std::string& newStatus) { return executeQuery("UPDATE Users SET Status = '" + newStatus + "' WHERE ID = '" + userId + "';"); }
std::string DatabaseManager::getServerSettings(std::string serverId) {
    sqlite3_stmt* stmt; std::string settings = "{}";
    if (sqlite3_prepare_v2(db, "SELECT Settings FROM Servers WHERE ID = ?;", -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_STATIC); if (sqlite3_step(stmt) == SQLITE_ROW) settings = SAFE_TEXT(0); } sqlite3_finalize(stmt); return settings;
}
bool DatabaseManager::updateServerSettings(std::string serverId, const std::string& settingsJson) {
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "UPDATE Servers SET Settings = ? WHERE ID = ?;", -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, settingsJson.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, serverId.c_str(), -1, SQLITE_TRANSIENT); bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s; } return false;
}
bool DatabaseManager::hasServerPermission(std::string serverId, std::string userId, std::string permissionType) {
    auto srv = getServerDetails(serverId); if (srv && srv->owner_id == userId) return true;
    std::string sql = "SELECT R.RoleName FROM Roles R JOIN ServerMemberRoles SMR ON R.ID = SMR.RoleID WHERE SMR.ServerID = ? AND SMR.UserID = ?;";
    sqlite3_stmt* stmt; bool hasPerm = false;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) { std::string rName = SAFE_TEXT(0); for (auto& c : rName) c = toupper(c); if (rName.find(permissionType) != std::string::npos) { hasPerm = true; break; } }
    } sqlite3_finalize(stmt); return hasPerm;
}
bool DatabaseManager::isUserInServer(std::string serverId, std::string userId) {
    sqlite3_stmt* stmt; bool inServer = false;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM ServerMembers WHERE ServerID = ? AND UserID = ?;", -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_TRANSIENT); if (sqlite3_step(stmt) == SQLITE_ROW) inServer = true; } sqlite3_finalize(stmt); return inServer;
}
std::string DatabaseManager::getServerIdByRoleId(std::string roleId) {
    sqlite3_stmt* stmt; std::string sId = "";
    if (sqlite3_prepare_v2(db, "SELECT ServerID FROM Roles WHERE ID = ?;", -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, roleId.c_str(), -1, SQLITE_STATIC); if (sqlite3_step(stmt) == SQLITE_ROW) sId = SAFE_TEXT(0); } sqlite3_finalize(stmt); return sId;
}
bool DatabaseManager::updateRole(std::string roleId, std::string name, int hierarchy, int permissions) {
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "UPDATE Roles SET RoleName=?, Hierarchy=?, Permissions=? WHERE ID=?;", -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_int(stmt, 2, hierarchy); sqlite3_bind_int(stmt, 3, permissions); sqlite3_bind_text(stmt, 4, roleId.c_str(), -1, SQLITE_TRANSIENT); bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s; } return false;
}
bool DatabaseManager::deleteRole(std::string roleId) { return executeQuery("DELETE FROM Roles WHERE ID='" + roleId + "'"); }
bool DatabaseManager::assignRoleToMember(std::string serverId, std::string userId, std::string roleId) {
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO ServerMemberRoles (ServerID, UserID, RoleID) VALUES (?, ?, ?);", -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 3, roleId.c_str(), -1, SQLITE_TRANSIENT); bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s; } return false;
}
std::vector<User> DatabaseManager::getBlockedUsers(std::string userId) {
    std::vector<User> blocked; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT U.ID, U.Name, U.Email, U.Status, U.IsSystemAdmin, U.AvatarURL FROM Users U JOIN BlockedUsers B ON U.ID = B.BlockedID WHERE B.UserID = ?;", -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT); while (sqlite3_step(stmt) == SQLITE_ROW) blocked.push_back(User{ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), "", sqlite3_column_int(stmt, 4) != 0, SAFE_TEXT(3), SAFE_TEXT(5), 0, "", "" }); } sqlite3_finalize(stmt); return blocked;
}
bool DatabaseManager::blockUser(std::string userId, std::string targetId) {
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO BlockedUsers (UserID, BlockedID) VALUES (?, ?);", -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, targetId.c_str(), -1, SQLITE_TRANSIENT); bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s; } return false;
}
bool DatabaseManager::unblockUser(std::string userId, std::string targetId) { return executeQuery("DELETE FROM BlockedUsers WHERE UserID='" + userId + "' AND BlockedID='" + targetId + "'"); }
std::string DatabaseManager::getServerIdByCardId(std::string cardId) {
    sqlite3_stmt* stmt; std::string sId = "";
    if (sqlite3_prepare_v2(db, "SELECT C.ServerID FROM KanbanCards KC JOIN KanbanLists KL ON KC.ListID = KL.ID JOIN Channels C ON KL.ChannelID = C.ID WHERE KC.ID = ?;", -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, cardId.c_str(), -1, SQLITE_STATIC); if (sqlite3_step(stmt) == SQLITE_ROW) sId = SAFE_TEXT(0); } sqlite3_finalize(stmt); return sId;
}
bool DatabaseManager::assignUserToCard(std::string cardId, std::string assigneeId) {
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "UPDATE KanbanCards SET AssigneeID = ? WHERE ID = ?;", -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, assigneeId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, cardId.c_str(), -1, SQLITE_TRANSIENT); bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s; } return false;
}
bool DatabaseManager::updateCardCompletion(std::string cardId, bool isCompleted) {
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "UPDATE KanbanCards SET IsCompleted = ? WHERE ID = ?;", -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_int(stmt, 1, isCompleted ? 1 : 0); sqlite3_bind_text(stmt, 2, cardId.c_str(), -1, SQLITE_TRANSIENT); bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s; } return false;
}
std::string DatabaseManager::createServer(const std::string& name, std::string ownerId) {
    if (!isSubscriptionActive(ownerId) && getUserServerCount(ownerId) >= 1) return "";
    std::string checkUserSql = "SELECT ID FROM Users WHERE ID = ?;"; sqlite3_stmt* checkStmt; bool userExists = false;
    if (sqlite3_prepare_v2(db, checkUserSql.c_str(), -1, &checkStmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(checkStmt, 1, ownerId.c_str(), -1, SQLITE_TRANSIENT); if (sqlite3_step(checkStmt) == SQLITE_ROW) userExists = true; } sqlite3_finalize(checkStmt);
    if (!userExists) return "";
    std::string id = Security::generateId(15); std::string inviteCode = "INV-" + id; std::string sql = "INSERT INTO Servers (ID, OwnerID, Name, InviteCode) VALUES (?, ?, ?, ?);"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return "";
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, ownerId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 4, inviteCode.c_str(), -1, SQLITE_TRANSIENT);
    bool success = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt);
    if (success) { addMemberToServer(id, ownerId); createRole(id, "Admin", 100, 9999); return id; } return "";
}
bool DatabaseManager::updateServer(std::string serverId, const std::string& name, const std::string& iconUrl) {
    const char* sql = "UPDATE Servers SET Name = ?, IconURL = ? WHERE ID = ?"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 2, iconUrl.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 3, serverId.c_str(), -1, SQLITE_STATIC);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s;
}
bool DatabaseManager::deleteServer(std::string serverId) { return executeQuery("DELETE FROM Servers WHERE ID = '" + serverId + "'"); }
std::vector<Server> DatabaseManager::getUserServers(std::string userId) {
    std::vector<Server> servers; std::string sql = "SELECT S.ID, S.Name, S.OwnerID, S.InviteCode, S.IconURL, S.CreatedAt, (SELECT COUNT(*) FROM ServerMembers SM WHERE SM.ServerID = S.ID) FROM Servers S JOIN ServerMembers SM ON S.ID = SM.ServerID WHERE SM.UserID = '" + userId + "';"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { while (sqlite3_step(stmt) == SQLITE_ROW) servers.push_back(Server{ SAFE_TEXT(0), SAFE_TEXT(2), SAFE_TEXT(1), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5), sqlite3_column_int(stmt, 6), {} }); } sqlite3_finalize(stmt); return servers;
}
std::optional<Server> DatabaseManager::getServerDetails(std::string serverId) {
    std::string sql = "SELECT ID, Name, OwnerID, InviteCode, IconURL, CreatedAt FROM Servers WHERE ID = '" + serverId + "';"; sqlite3_stmt* stmt; std::optional<Server> server = std::nullopt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) server = Server{ SAFE_TEXT(0), SAFE_TEXT(2), SAFE_TEXT(1), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5), 0, {} }; sqlite3_finalize(stmt); return server;
}
bool DatabaseManager::addMemberToServer(std::string serverId, std::string userId) { return executeQuery("INSERT INTO ServerMembers (ServerID, UserID) VALUES ('" + serverId + "', '" + userId + "');"); }
bool DatabaseManager::removeMemberFromServer(std::string serverId, std::string userId) { return executeQuery("DELETE FROM ServerMembers WHERE ServerID='" + serverId + "' AND UserID='" + userId + "'"); }
bool DatabaseManager::joinServerByCode(std::string userId, const std::string& inviteCode) {
    std::string sql = "SELECT ID FROM Servers WHERE InviteCode = ?;"; sqlite3_stmt* stmt; std::string serverId = "";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, inviteCode.c_str(), -1, SQLITE_STATIC); if (sqlite3_step(stmt) == SQLITE_ROW) serverId = SAFE_TEXT(0); } sqlite3_finalize(stmt);
    if (serverId.empty()) return false; return addMemberToServer(serverId, userId);
}
bool DatabaseManager::kickMember(std::string serverId, std::string userId) { return removeMemberFromServer(serverId, userId); }
bool DatabaseManager::createRole(std::string serverId, std::string roleName, int hierarchy, int permissions) {
    std::string id = Security::generateId(15); return executeQuery("INSERT INTO Roles (ID, ServerID, RoleName, Hierarchy, Permissions) VALUES ('" + id + "', '" + serverId + "', '" + roleName + "', " + std::to_string(hierarchy) + ", " + std::to_string(permissions) + ");");
}
std::vector<Role> DatabaseManager::getServerRoles(std::string serverId) {
    std::vector<Role> roles; std::string sql = "SELECT ID, RoleName, Color, Hierarchy, Permissions FROM Roles WHERE ServerID = '" + serverId + "';"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { while (sqlite3_step(stmt) == SQLITE_ROW) roles.push_back(Role{ SAFE_TEXT(0), serverId, SAFE_TEXT(1), SAFE_TEXT(2), sqlite3_column_int(stmt, 3), sqlite3_column_int(stmt, 4) }); } sqlite3_finalize(stmt); return roles;
}
bool DatabaseManager::updateChannel(std::string channelId, const std::string& name) { return executeQuery("UPDATE Channels SET Name = '" + name + "' WHERE ID = '" + channelId + "'"); }
bool DatabaseManager::deleteChannel(std::string channelId) { return executeQuery("DELETE FROM Channels WHERE ID = '" + channelId + "'"); }
int DatabaseManager::getServerKanbanCount(std::string serverId) {
    std::string sql = "SELECT COUNT(*) FROM Channels WHERE ServerID = '" + serverId + "' AND Type = 3;"; sqlite3_stmt* stmt; int count = 0;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0); } sqlite3_finalize(stmt); return count;
}
std::string DatabaseManager::getOrCreateDMChannel(std::string user1Id, std::string user2Id) {
    std::string u1 = std::min(user1Id, user2Id); std::string u2 = std::max(user1Id, user2Id); std::string dmName = "dm_" + u1 + "_" + u2;
    std::string sql = "SELECT ID FROM Channels WHERE Name = '" + dmName + "' AND ServerID = '0';"; sqlite3_stmt* stmt; std::string channelId = "";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { if (sqlite3_step(stmt) == SQLITE_ROW) channelId = SAFE_TEXT(0); } sqlite3_finalize(stmt);
    if (!channelId.empty()) return channelId; channelId = Security::generateId(15); sql = "INSERT INTO Channels (ID, ServerID, Name, Type) VALUES ('" + channelId + "', '0', '" + dmName + "', 0);";
    if (executeQuery(sql)) return channelId; return "";
}
bool DatabaseManager::sendMessage(std::string channelId, std::string senderId, const std::string& content, const std::string& attachmentUrl) {
    std::string id = Security::generateId(15); const char* sql = "INSERT INTO Messages (ID, ChannelID, SenderID, Content, AttachmentURL) VALUES (?, ?, ?, ?, ?);"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 2, channelId.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 3, senderId.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 4, content.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 5, attachmentUrl.c_str(), -1, SQLITE_STATIC);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s;
}
bool DatabaseManager::updateMessage(std::string messageId, const std::string& newContent) {
    const char* sql = "UPDATE Messages SET Content = ? WHERE ID = ?"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, newContent.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 2, messageId.c_str(), -1, SQLITE_STATIC); bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s;
}
bool DatabaseManager::deleteMessage(std::string messageId) { return executeQuery("DELETE FROM Messages WHERE ID = '" + messageId + "'"); }
std::vector<Message> DatabaseManager::getChannelMessages(std::string channelId, int limit) {
    std::vector<Message> messages; std::string sql = "SELECT M.ID, M.SenderID, U.Name, U.AvatarURL, M.Content, M.AttachmentURL, M.Timestamp FROM Messages M JOIN Users U ON M.SenderID = U.ID WHERE M.ChannelID = '" + channelId + "' ORDER BY M.Timestamp ASC LIMIT " + std::to_string(limit) + ";"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { while (sqlite3_step(stmt) == SQLITE_ROW) messages.push_back(Message{ SAFE_TEXT(0), channelId, SAFE_TEXT(1), SAFE_TEXT(2), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5), SAFE_TEXT(6) }); } sqlite3_finalize(stmt); return messages;
}
std::vector<KanbanList> DatabaseManager::getKanbanBoard(std::string channelId) {
    std::vector<KanbanList> board; std::string sql = "SELECT ID, Title, Position FROM KanbanLists WHERE ChannelID = '" + channelId + "' ORDER BY Position ASC;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string listId = SAFE_TEXT(0); std::vector<KanbanCard> cards;
            std::string cardSql = "SELECT ID, Title, Description, Priority, Position FROM KanbanCards WHERE ListID = '" + listId + "' ORDER BY Position ASC;"; sqlite3_stmt* cardStmt;
            if (sqlite3_prepare_v2(db, cardSql.c_str(), -1, &cardStmt, nullptr) == SQLITE_OK) { while (sqlite3_step(cardStmt) == SQLITE_ROW) cards.push_back(KanbanCard{ SAFE_TEXT(0), listId, SAFE_TEXT(1), SAFE_TEXT(2), sqlite3_column_int(cardStmt, 3), sqlite3_column_int(cardStmt, 4) }); } sqlite3_finalize(cardStmt); board.push_back(KanbanList{ listId, SAFE_TEXT(1), sqlite3_column_int(stmt, 2), cards });
        }
    } sqlite3_finalize(stmt); return board;
}
bool DatabaseManager::createKanbanList(std::string boardChannelId, std::string title) { std::string id = Security::generateId(15); return executeQuery("INSERT INTO KanbanLists (ID, ChannelID, Title, Position) VALUES ('" + id + "', '" + boardChannelId + "', '" + title + "', 0);"); }
bool DatabaseManager::updateKanbanList(std::string listId, const std::string& title, int position) { return executeQuery("UPDATE KanbanLists SET Title='" + title + "', Position=" + std::to_string(position) + " WHERE ID='" + listId + "'"); }
bool DatabaseManager::deleteKanbanList(std::string listId) { return executeQuery("DELETE FROM KanbanLists WHERE ID='" + listId + "'"); }
bool DatabaseManager::updateKanbanCard(std::string cardId, std::string title, std::string desc, int priority) { return executeQuery("UPDATE KanbanCards SET Title='" + title + "', Description='" + desc + "', Priority=" + std::to_string(priority) + " WHERE ID='" + cardId + "'"); }
bool DatabaseManager::deleteKanbanCard(std::string cardId) { return executeQuery("DELETE FROM KanbanCards WHERE ID='" + cardId + "'"); }
bool DatabaseManager::moveCard(std::string cardId, std::string newListId, int newPosition) { return executeQuery("UPDATE KanbanCards SET ListID='" + newListId + "', Position=" + std::to_string(newPosition) + " WHERE ID='" + cardId + "'"); }
std::vector<User> DatabaseManager::searchUsers(const std::string& searchQuery) {
    std::vector<User> users; std::string sql = "SELECT ID, Name, Email, Status, IsSystemAdmin, AvatarURL FROM Users WHERE Name LIKE ? OR Email LIKE ? LIMIT 20;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { std::string likeTerm = "%" + searchQuery + "%"; sqlite3_bind_text(stmt, 1, likeTerm.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, likeTerm.c_str(), -1, SQLITE_TRANSIENT); while (sqlite3_step(stmt) == SQLITE_ROW) users.push_back(User{ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), "", sqlite3_column_int(stmt, 4) != 0, SAFE_TEXT(3), SAFE_TEXT(5), 0, "", "" }); } sqlite3_finalize(stmt); return users;
}
bool DatabaseManager::sendFriendRequest(std::string myId, std::string targetUserId) { if (myId == targetUserId) return false; return executeQuery("INSERT INTO Friends (RequesterID, TargetID, Status) VALUES ('" + myId + "', '" + targetUserId + "', 0);"); }
bool DatabaseManager::acceptFriendRequest(std::string requesterId, std::string myId) { return executeQuery("UPDATE Friends SET Status=1 WHERE RequesterID='" + requesterId + "' AND TargetID='" + myId + "'"); }
bool DatabaseManager::rejectOrRemoveFriend(std::string otherUserId, std::string myId) { return executeQuery("DELETE FROM Friends WHERE (RequesterID='" + otherUserId + "' AND TargetID='" + myId + "') OR (RequesterID='" + myId + "' AND TargetID='" + otherUserId + "');"); }
std::vector<FriendRequest> DatabaseManager::getPendingRequests(std::string myId) {
    std::vector<FriendRequest> reqs; std::string sql = "SELECT U.ID, U.Name, U.AvatarURL, F.CreatedAt FROM Users U JOIN Friends F ON U.ID=F.RequesterID WHERE F.TargetID='" + myId + "' AND F.Status=0;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { while (sqlite3_step(stmt) == SQLITE_ROW) reqs.push_back({ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), SAFE_TEXT(3) }); } sqlite3_finalize(stmt); return reqs;
}
std::vector<User> DatabaseManager::getFriendsList(std::string myId) {
    std::vector<User> friends; std::string sql = "SELECT U.ID, U.Name, U.Email, U.Status, U.IsSystemAdmin, U.AvatarURL FROM Users U JOIN Friends F ON (U.ID=F.RequesterID OR U.ID=F.TargetID) WHERE (F.RequesterID='" + myId + "' OR F.TargetID='" + myId + "') AND F.Status=1 AND U.ID!='" + myId + "';"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { while (sqlite3_step(stmt) == SQLITE_ROW) friends.push_back(User{ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), "", sqlite3_column_int(stmt, 4) != 0, SAFE_TEXT(3), SAFE_TEXT(5), 0, "", "" }); } sqlite3_finalize(stmt); return friends;
}
bool DatabaseManager::isSubscriptionActive(std::string userId) {
    std::string sql = "SELECT SubscriptionLevel FROM Users WHERE ID = '" + userId + "';"; sqlite3_stmt* stmt; bool active = false;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { if (sqlite3_step(stmt) == SQLITE_ROW) if (sqlite3_column_int(stmt, 0) > 0) active = true; } sqlite3_finalize(stmt); return active;
}
int DatabaseManager::getUserServerCount(std::string userId) {
    std::string sql = "SELECT COUNT(*) FROM Servers WHERE OwnerID = '" + userId + "';"; sqlite3_stmt* stmt; int count = 0;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0); } sqlite3_finalize(stmt); return count;
}
bool DatabaseManager::updateUserSubscription(std::string userId, int level, int durationDays) { return executeQuery("UPDATE Users SET SubscriptionLevel=" + std::to_string(level) + ", SubscriptionExpiresAt=datetime('now', '+" + std::to_string(durationDays) + " days') WHERE ID='" + userId + "'"); }
bool DatabaseManager::createPaymentRecord(std::string userId, const std::string& providerId, float amount, const std::string& currency) {
    std::string id = Security::generateId(15); return executeQuery("INSERT INTO Payments (ID, UserID, ProviderPaymentID, Amount, Currency) VALUES ('" + id + "', '" + userId + "', '" + providerId + "', " + std::to_string(amount) + ", '" + currency + "');");
}
bool DatabaseManager::updatePaymentStatus(const std::string& providerId, const std::string& status) { return executeQuery("UPDATE Payments SET Status='" + status + "' WHERE ProviderPaymentID='" + providerId + "'"); }
std::vector<PaymentTransaction> DatabaseManager::getUserPayments(std::string userId) {
    std::vector<PaymentTransaction> payments; std::string sql = "SELECT ID, ProviderPaymentID, Amount, Currency, Status, CreatedAt FROM Payments WHERE UserID='" + userId + "'"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { while (sqlite3_step(stmt) == SQLITE_ROW) payments.push_back({ SAFE_TEXT(0), userId, SAFE_TEXT(1), (float)sqlite3_column_double(stmt, 2), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5) }); } sqlite3_finalize(stmt); return payments;
}
bool DatabaseManager::createReport(std::string reporterId, std::string contentId, const std::string& type, const std::string& reason) {
    std::string id = Security::generateId(15); const char* sql = "INSERT INTO Reports (ID, ReporterID, ContentID, Type, Reason) VALUES (?, ?, ?, ?, ?);"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 2, reporterId.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 3, contentId.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 4, type.c_str(), -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 5, reason.c_str(), -1, SQLITE_STATIC);
    bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s;
}
std::vector<UserReport> DatabaseManager::getOpenReports() {
    std::vector<UserReport> reports; std::string sql = "SELECT ID, ReporterID, ContentID, Type, Reason, Status FROM Reports WHERE Status='OPEN';"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { while (sqlite3_step(stmt) == SQLITE_ROW) reports.push_back({ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5) }); } sqlite3_finalize(stmt); return reports;
}
bool DatabaseManager::resolveReport(std::string reportId) { return executeQuery("UPDATE Reports SET Status='RESOLVED' WHERE ID='" + reportId + "'"); }
bool DatabaseManager::logServerAction(const std::string& serverId, const std::string& action, const std::string& details) {
    std::string sql = "INSERT INTO ServerLogs (ServerID, Action, Details) VALUES (?, ?, ?);"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, action.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 3, details.c_str(), -1, SQLITE_TRANSIENT); bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s; } return false;
}
std::vector<ServerLog> DatabaseManager::getServerLogs(const std::string& serverId) {
    std::vector<ServerLog> logs; std::string sql = "SELECT CreatedAt, Action, Details FROM ServerLogs WHERE ServerID = ? ORDER BY CreatedAt DESC LIMIT 50;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT); while (sqlite3_step(stmt) == SQLITE_ROW) logs.push_back({ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2) }); } sqlite3_finalize(stmt); return logs;
}
std::string DatabaseManager::getChannelServerId(const std::string& channelId) {
    std::string srvId = ""; std::string sql = "SELECT ServerID FROM Channels WHERE ID = ?;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, channelId.c_str(), -1, SQLITE_TRANSIENT); if (sqlite3_step(stmt) == SQLITE_ROW) srvId = SAFE_TEXT(0); } sqlite3_finalize(stmt); return srvId;
}
std::string DatabaseManager::getChannelName(const std::string& channelId) {
    std::string name = ""; std::string sql = "SELECT Name FROM Channels WHERE ID = ?;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, channelId.c_str(), -1, SQLITE_TRANSIENT); if (sqlite3_step(stmt) == SQLITE_ROW) name = SAFE_TEXT(0); } sqlite3_finalize(stmt); return name;
}
std::vector<ServerMemberDetail> DatabaseManager::getServerMembersDetails(const std::string& serverId) {
    std::vector<ServerMemberDetail> members; std::string sql = "SELECT U.ID, U.Name, U.Status FROM ServerMembers SM JOIN Users U ON SM.UserID = U.ID WHERE SM.ServerID = ?;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT); while (sqlite3_step(stmt) == SQLITE_ROW) members.push_back({ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2) }); } sqlite3_finalize(stmt); return members;
}
std::vector<Server> DatabaseManager::getAllServers() {
    std::vector<Server> servers; std::string sql = "SELECT S.ID, S.Name, S.OwnerID, S.InviteCode, S.IconURL, S.CreatedAt, (SELECT COUNT(*) FROM ServerMembers SM WHERE SM.ServerID = S.ID) FROM Servers S;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { while (sqlite3_step(stmt) == SQLITE_ROW) servers.push_back(Server{ SAFE_TEXT(0), SAFE_TEXT(2), SAFE_TEXT(1), SAFE_TEXT(3), SAFE_TEXT(4), SAFE_TEXT(5), sqlite3_column_int(stmt, 6), {} }); } sqlite3_finalize(stmt); return servers;
}
bool DatabaseManager::sendServerInvite(std::string serverId, std::string inviterId, std::string inviteeId) {
    if (inviterId == inviteeId) return false; std::string sql = "INSERT OR IGNORE INTO ServerInvites (ServerID, InviterID, InviteeID) VALUES (?, ?, ?);"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, inviterId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 3, inviteeId.c_str(), -1, SQLITE_TRANSIENT); bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s; } return false;
}
std::vector<ServerInviteDTO> DatabaseManager::getPendingServerInvites(std::string userId) {
    std::vector<ServerInviteDTO> invites; std::string sql = "SELECT I.ServerID, S.Name, U.Name, I.CreatedAt FROM ServerInvites I JOIN Servers S ON I.ServerID = S.ID JOIN Users U ON I.InviterID = U.ID WHERE I.InviteeID = ?;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT); while (sqlite3_step(stmt) == SQLITE_ROW) invites.push_back({ SAFE_TEXT(0), SAFE_TEXT(1), SAFE_TEXT(2), SAFE_TEXT(3) }); } sqlite3_finalize(stmt); return invites;
}
bool DatabaseManager::resolveServerInvite(std::string serverId, std::string inviteeId, bool accept) {
    std::string sql = "DELETE FROM ServerInvites WHERE ServerID = ? AND InviteeID = ?;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, serverId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, inviteeId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_step(stmt); sqlite3_finalize(stmt); }
    if (accept) return addMemberToServer(serverId, inviteeId); return true;
}
void DatabaseManager::processKanbanNotifications() {
    std::string sqlWarning = "SELECT ID, Title, AssigneeID FROM KanbanCards WHERE IsCompleted = 0 AND WarningSent = 0 AND DueDate IS NOT NULL AND (julianday(DueDate) - julianday('now', 'localtime')) <= (1.0/24.0) AND (julianday(DueDate) - julianday('now', 'localtime')) > 0 AND AssigneeID != '';";
    std::string sqlExpired = "SELECT ID, Title, AssigneeID FROM KanbanCards WHERE IsCompleted = 0 AND ExpiredSent = 0 AND DueDate IS NOT NULL AND (julianday(DueDate) - julianday('now', 'localtime')) <= 0 AND AssigneeID != '';";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sqlWarning.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string cardId = SAFE_TEXT(0); std::string title = SAFE_TEXT(1); std::string assignee = SAFE_TEXT(2); std::string msg = "Yaklasan Gorev: '" + title + "' icin son 1 saatiniz kaldi!";
            std::string insertNotif = "INSERT INTO Notifications (UserID, Message, Type) VALUES ('" + assignee + "', '" + msg + "', 'WARNING');";
            sqlite3_exec(db, insertNotif.c_str(), nullptr, nullptr, nullptr); sqlite3_exec(db, ("UPDATE KanbanCards SET WarningSent = 1 WHERE ID = '" + cardId + "';").c_str(), nullptr, nullptr, nullptr);
        }
    } sqlite3_finalize(stmt);
    if (sqlite3_prepare_v2(db, sqlExpired.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string cardId = SAFE_TEXT(0); std::string title = SAFE_TEXT(1); std::string assignee = SAFE_TEXT(2); std::string msg = "Suresi Doldu: '" + title + "' adli gorevin teslim suresi gecti!";
            std::string insertNotif = "INSERT INTO Notifications (UserID, Message, Type) VALUES ('" + assignee + "', '" + msg + "', 'EXPIRED');";
            sqlite3_exec(db, insertNotif.c_str(), nullptr, nullptr, nullptr); sqlite3_exec(db, ("UPDATE KanbanCards SET ExpiredSent = 1 WHERE ID = '" + cardId + "';").c_str(), nullptr, nullptr, nullptr);
        }
    } sqlite3_finalize(stmt);
}
std::vector<NotificationDTO> DatabaseManager::getUserNotifications(const std::string& userId) {
    std::vector<NotificationDTO> notifs; std::string sql = "SELECT ID, Message, Type, CreatedAt FROM Notifications WHERE UserID = ? AND IsRead = 0 ORDER BY CreatedAt DESC;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT); while (sqlite3_step(stmt) == SQLITE_ROW) notifs.push_back({ sqlite3_column_int(stmt, 0), SAFE_TEXT(1), SAFE_TEXT(2), SAFE_TEXT(3) }); } sqlite3_finalize(stmt); return notifs;
}
bool DatabaseManager::markNotificationAsRead(int notifId) {
    std::string sql = "UPDATE Notifications SET IsRead = 1 WHERE ID = ?;"; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_int(stmt, 1, notifId); bool s = (sqlite3_step(stmt) == SQLITE_DONE); sqlite3_finalize(stmt); return s; } return false;
}