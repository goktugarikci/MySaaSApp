#include "DatabaseManager.h"
#include "../utils/Security.h"
#include <iostream>
#include <algorithm>
#include <sstream>

// Güvenli veri çekme makrosu
#define SAFE_TEXT(col) (reinterpret_cast<const char*>(sqlite3_column_text(stmt, col)) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, col)) : "")

// ==========================================================
// 1. TEMEL VERİTABANI İŞLEMLERİ (TEK KİLİT MOTORU)
// ==========================================================
DatabaseManager::DatabaseManager(const std::string& path) : db_path(path), db(nullptr) {}

DatabaseManager::~DatabaseManager() { close(); }

sqlite3* DatabaseManager::getDb() { return db; }

bool DatabaseManager::open() {
    std::lock_guard<std::mutex> lock(dbMutex); // 1. Kilidi aldık
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) return false;

    char* errMsg = nullptr;
    if (sqlite3_exec(db, "PRAGMA foreign_keys = ON; PRAGMA journal_mode = WAL;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    return true;
}

void DatabaseManager::close() {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool DatabaseManager::executeQuery(const std::string& sql) {
    std::lock_guard<std::mutex> lock(dbMutex);
    char* errMsg = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "SQL Hatasi: " << (errMsg ? errMsg : "Bilinmiyor") << "\nSorgu: " << sql << std::endl;
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool DatabaseManager::initTables() {
    std::string tables = R"(
        CREATE TABLE IF NOT EXISTS users (id TEXT PRIMARY KEY, username TEXT, email TEXT UNIQUE, password_hash TEXT, status TEXT DEFAULT 'Offline', avatar_url TEXT, is_admin INTEGER DEFAULT 0, last_seen DATETIME DEFAULT CURRENT_TIMESTAMP, two_factor_secret TEXT);
        CREATE TABLE IF NOT EXISTS servers (id TEXT PRIMARY KEY, name TEXT, owner_id TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP);
        CREATE TABLE IF NOT EXISTS channels (id TEXT PRIMARY KEY, server_id TEXT, name TEXT, type INTEGER, position INTEGER DEFAULT 0);
        CREATE TABLE IF NOT EXISTS roles (id TEXT PRIMARY KEY, server_id TEXT, name TEXT, color TEXT, permissions INTEGER);
        CREATE TABLE IF NOT EXISTS user_roles (server_id TEXT, user_id TEXT, role_id TEXT, UNIQUE(server_id, user_id, role_id));
        CREATE TABLE IF NOT EXISTS server_members (server_id TEXT, user_id TEXT, joined_at DATETIME DEFAULT CURRENT_TIMESTAMP, UNIQUE(server_id, user_id));
        
        -- Hibrit Mesajlaşma Tabloları (Sadece Meta Veriler)
        CREATE TABLE IF NOT EXISTS saved_messages (user_id TEXT, message_id TEXT, UNIQUE(user_id, message_id));
        CREATE TABLE IF NOT EXISTS message_metadata (message_id TEXT PRIMARY KEY, is_pinned INTEGER DEFAULT 0);
        CREATE TABLE IF NOT EXISTS message_reactions (message_id TEXT, user_id TEXT, reaction TEXT, UNIQUE(message_id, user_id, reaction));
        CREATE TABLE IF NOT EXISTS read_cursors (user_id TEXT, channel_id TEXT, message_id TEXT, UNIQUE(user_id, channel_id));
        
        -- Sistem & Log Tabloları
        CREATE TABLE IF NOT EXISTS audit_logs (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id TEXT, action_type TEXT, target_id TEXT, details TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP);
        CREATE TABLE IF NOT EXISTS banned_users (user_id TEXT PRIMARY KEY, reason TEXT, date DATETIME DEFAULT CURRENT_TIMESTAMP);
        CREATE TABLE IF NOT EXISTS call_quality_metrics (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id TEXT, channel_id TEXT, latency INTEGER, packet_loss REAL, resolution TEXT, recorded_at DATETIME DEFAULT CURRENT_TIMESTAMP);
        
        -- SaaS & Ödeme Tabloları
        CREATE TABLE IF NOT EXISTS subscriptions (user_id TEXT PRIMARY KEY, plan_id TEXT, end_date DATETIME);
        CREATE TABLE IF NOT EXISTS payments (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id TEXT, provider_id TEXT, amount REAL, currency TEXT, status TEXT, date DATETIME DEFAULT CURRENT_TIMESTAMP);
    )";
    return executeQuery(tables);
}

// ==========================================================
// 2. YENİ HİBRİT MESAJLAŞMA (HIZLI ETKİLEŞİMLER)
// ==========================================================

bool DatabaseManager::saveFavoriteMessage(const std::string& userId, const std::string& messageId) {
    return executeQuery("INSERT OR IGNORE INTO saved_messages (user_id, message_id) VALUES ('" + userId + "', '" + messageId + "');");
}

bool DatabaseManager::removeSavedMessage(const std::string& userId, const std::string& messageId) {
    return executeQuery("DELETE FROM saved_messages WHERE user_id = '" + userId + "' AND message_id = '" + messageId + "';");
}

bool DatabaseManager::toggleMessagePin(const std::string& messageId, bool isPinned) {
    return executeQuery("INSERT OR REPLACE INTO message_metadata (message_id, is_pinned) VALUES ('" + messageId + "', " + (isPinned ? "1" : "0") + ");");
}

bool DatabaseManager::addMessageReaction(const std::string& messageId, const std::string& userId, const std::string& reaction) {
    return executeQuery("INSERT OR IGNORE INTO message_reactions (message_id, user_id, reaction) VALUES ('" + messageId + "', '" + userId + "', '" + reaction + "');");
}

bool DatabaseManager::removeMessageReaction(const std::string& messageId, const std::string& userId, const std::string& reaction) {
    return executeQuery("DELETE FROM message_reactions WHERE message_id = '" + messageId + "' AND user_id = '" + userId + "' AND reaction = '" + reaction + "';");
}

bool DatabaseManager::setChannelReadCursor(const std::string& userId, const std::string& channelId, const std::string& messageId) {
    return executeQuery("INSERT OR REPLACE INTO read_cursors (user_id, channel_id, message_id) VALUES ('" + userId + "', '" + channelId + "', '" + messageId + "');");
}

bool DatabaseManager::clearChatForUser(std::string userId, std::string channelId) {
    return executeQuery("INSERT INTO audit_logs (user_id, action_type, target_id) VALUES ('" + userId + "', 'CLEAR_CHAT', '" + channelId + "');");
}

// ==========================================================
// 3. KULLANICI VE KİMLİK DOĞRULAMA (AUTH)
// ==========================================================

bool DatabaseManager::createUser(std::string name, std::string email, std::string password, bool is_system_admin, std::string username, std::string phone_number) {
    std::string id = Security::generateId(15);
    std::string hash = Security::hashPassword(password);
    return executeQuery("INSERT INTO users (id, username, email, password_hash, is_admin) VALUES ('" + id + "', '" + (username.empty() ? name : username) + "', '" + email + "', '" + hash + "', " + (is_system_admin ? "1" : "0") + ");");
}

bool DatabaseManager::createGoogleUser(const std::string& name, const std::string& email, const std::string& googleId, const std::string& avatarUrl) {
    return executeQuery("INSERT OR IGNORE INTO users (id, username, email, password_hash, avatar_url) VALUES ('" + googleId + "', '" + name + "', '" + email + "', 'OAUTH', '" + avatarUrl + "');");
}

std::optional<User> DatabaseManager::getUser(const std::string& email) {
    std::lock_guard<std::mutex> lock(dbMutex);
    sqlite3_stmt* stmt;
    std::string sql = "SELECT id, username, email, password_hash, status, avatar_url, is_admin FROM users WHERE email = '" + email + "';";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        User u;
        u.id = SAFE_TEXT(0); u.name = SAFE_TEXT(1); u.email = SAFE_TEXT(2); u.password = SAFE_TEXT(3); u.status = SAFE_TEXT(4); u.avatarUrl = SAFE_TEXT(5); u.isSystemAdmin = sqlite3_column_int(stmt, 6) != 0;
        sqlite3_finalize(stmt);
        return u;
    }
    if (stmt) sqlite3_finalize(stmt);
    return std::nullopt;
}

std::optional<User> DatabaseManager::getUserById(std::string id) {
    std::lock_guard<std::mutex> lock(dbMutex);
    sqlite3_stmt* stmt;
    std::string sql = "SELECT id, username, email, status, avatar_url FROM users WHERE id = '" + id + "';";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        User u;
        u.id = SAFE_TEXT(0); u.name = SAFE_TEXT(1); u.email = SAFE_TEXT(2); u.status = SAFE_TEXT(3); u.avatarUrl = SAFE_TEXT(4);
        sqlite3_finalize(stmt);
        return u;
    }
    if (stmt) sqlite3_finalize(stmt);
    return std::nullopt;
}

bool DatabaseManager::loginUser(const std::string& email, const std::string& rawPassword) {
    auto u = getUser(email);
    if (!u) return false;
    return Security::verifyPassword(rawPassword, u->password);
}

std::string DatabaseManager::authenticateUser(const std::string& email, const std::string& password) {
    if (loginUser(email, password)) {
        auto u = getUser(email);
        return Security::generateJwt(u->id);
    }
    return "";
}

bool DatabaseManager::updateUserStatus(const std::string& userId, const std::string& newStatus) {
    return executeQuery("UPDATE users SET status = '" + newStatus + "' WHERE id = '" + userId + "';");
}

bool DatabaseManager::updateLastSeen(const std::string& userId) {
    return executeQuery("UPDATE users SET last_seen = CURRENT_TIMESTAMP WHERE id = '" + userId + "';");
}

void DatabaseManager::markInactiveUsersOffline(int timeoutSeconds) {
    executeQuery("UPDATE users SET status = 'Offline' WHERE strftime('%s', 'now') - strftime('%s', last_seen) > " + std::to_string(timeoutSeconds) + ";");
}

bool DatabaseManager::updateUserAvatar(std::string userId, const std::string& avatarUrl) {
    return executeQuery("UPDATE users SET avatar_url = '" + avatarUrl + "' WHERE id = '" + userId + "';");
}

bool DatabaseManager::deleteUser(std::string userId) {
    return executeQuery("DELETE FROM users WHERE id = '" + userId + "';");
}

std::vector<User> DatabaseManager::getAllUsers() {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<User> users;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT id, username, email, status, avatar_url FROM users;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            User u; u.id = SAFE_TEXT(0); u.name = SAFE_TEXT(1); u.email = SAFE_TEXT(2); u.status = SAFE_TEXT(3); u.avatarUrl = SAFE_TEXT(4);
            users.push_back(u);
        }
    }
    sqlite3_finalize(stmt);
    return users;
}

// ==========================================================
// 4. SUNUCU, KANAL VE ROLLER (DISCORD MİMARİSİ)
// ==========================================================

std::string DatabaseManager::createServer(const std::string& name, std::string ownerId) {
    std::string id = Security::generateId(18);
    if (executeQuery("INSERT INTO servers (id, name, owner_id) VALUES ('" + id + "', '" + name + "', '" + ownerId + "');")) {
        addMemberToServer(id, ownerId);
        return id;
    }
    return "";
}

bool DatabaseManager::deleteServer(const std::string& serverId, const std::string& ownerId) {
    return executeQuery("DELETE FROM servers WHERE id = '" + serverId + "' AND owner_id = '" + ownerId + "';");
}

bool DatabaseManager::deleteServer(std::string serverId) {
    return executeQuery("DELETE FROM servers WHERE id = '" + serverId + "';");
}

bool DatabaseManager::updateServerName(const std::string& serverId, const std::string& ownerId, const std::string& newName) {
    return executeQuery("UPDATE servers SET name = '" + newName + "' WHERE id = '" + serverId + "' AND owner_id = '" + ownerId + "';");
}

bool DatabaseManager::addMemberToServer(std::string serverId, std::string userId) {
    return executeQuery("INSERT OR IGNORE INTO server_members (server_id, user_id) VALUES ('" + serverId + "', '" + userId + "');");
}

bool DatabaseManager::removeMemberFromServer(std::string serverId, std::string userId) {
    return executeQuery("DELETE FROM server_members WHERE server_id = '" + serverId + "' AND user_id = '" + userId + "';");
}

bool DatabaseManager::leaveServer(const std::string& serverId, const std::string& userId) {
    return removeMemberFromServer(serverId, userId);
}

bool DatabaseManager::kickMember(const std::string& serverId, const std::string& ownerId, const std::string& targetId) {
    // Sadece sunucu sahibi veya yetkilisi atabilir mantığı API tarafında (Security::checkAuth) halledilmeli
    return removeMemberFromServer(serverId, targetId);
}
bool DatabaseManager::kickMember(std::string serverId, std::string userId) {
    return removeMemberFromServer(serverId, userId);
}

bool DatabaseManager::createChannel(std::string serverId, std::string name, int type) {
    std::string id = Security::generateId(18);
    return executeQuery("INSERT INTO channels (id, server_id, name, type) VALUES ('" + id + "', '" + serverId + "', '" + name + "', " + std::to_string(type) + ");");
}

bool DatabaseManager::createChannel(std::string serverId, std::string name, int type, bool isPrivate) {
    return createChannel(serverId, name, type); // Private kanal sistemi API tarafında rollerle yönetilir
}

bool DatabaseManager::deleteChannel(std::string channelId) {
    return executeQuery("DELETE FROM channels WHERE id = '" + channelId + "';");
}

std::string DatabaseManager::createServerRole(const std::string& serverId, const std::string& name, const std::string& color, int permissions) {
    std::string id = Security::generateId(18);
    if (executeQuery("INSERT INTO roles (id, server_id, name, color, permissions) VALUES ('" + id + "', '" + serverId + "', '" + name + "', '" + color + "', " + std::to_string(permissions) + ");")) {
        return id;
    }
    return "";
}

bool DatabaseManager::assignRoleToUser(const std::string& serverId, const std::string& userId, const std::string& roleId) {
    return executeQuery("INSERT OR IGNORE INTO user_roles (server_id, user_id, role_id) VALUES ('" + serverId + "', '" + userId + "', '" + roleId + "');");
}

// ==========================================================
// 5. GÜVENLİK, AUDIT LOG VE BAN İŞLEMLERİ
// ==========================================================

bool DatabaseManager::logAction(const std::string& userId, const std::string& actionType, const std::string& targetId, const std::string& details) {
    std::string sql = "INSERT INTO audit_logs (user_id, action_type, target_id, details) VALUES ('" + userId + "', '" + actionType + "', '" + targetId + "', '" + details + "');";
    return executeQuery(sql);
}

std::vector<DatabaseManager::AuditLogRecord> DatabaseManager::getAuditLogs(int limit) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<AuditLogRecord> logs;
    sqlite3_stmt* stmt;
    std::string sql = "SELECT id, user_id, action_type, target_id, details, created_at FROM audit_logs ORDER BY created_at DESC LIMIT " + std::to_string(limit) + ";";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AuditLogRecord l;
            l.id = SAFE_TEXT(0); l.user_id = SAFE_TEXT(1); l.action_type = SAFE_TEXT(2);
            l.target_id = SAFE_TEXT(3); l.details = SAFE_TEXT(4); l.created_at = SAFE_TEXT(5);
            logs.push_back(l);
        }
    }
    sqlite3_finalize(stmt);
    return logs;
}

bool DatabaseManager::isSystemAdmin(std::string userId) {
    auto u = getUserById(userId);
    return u && u->isSystemAdmin;
}

bool DatabaseManager::banUser(std::string userId, const std::string& reason) {
    if (executeQuery("INSERT OR REPLACE INTO banned_users (user_id, reason) VALUES ('" + userId + "', '" + reason + "');")) {
        return updateUserStatus(userId, "Banned");
    }
    return false;
}

bool DatabaseManager::unbanUser(std::string userId) {
    if (executeQuery("DELETE FROM banned_users WHERE user_id = '" + userId + "';")) {
        return updateUserStatus(userId, "Offline");
    }
    return false;
}

bool DatabaseManager::timeoutUser(const std::string& serverId, const std::string& userId, int durationMinutes) {
    return logAction(userId, "TIMEOUT", serverId, "Süre: " + std::to_string(durationMinutes) + " dakika");
}

// ==========================================================
// 6. KANBAN VE GÖREV YÖNETİMİ
// ==========================================================

bool DatabaseManager::createKanbanCard(std::string listId, std::string title, std::string desc, int priority) {
    return executeQuery("INSERT INTO kanban_cards (id, list_id, title, description, priority) VALUES ('" + Security::generateId(18) + "', '" + listId + "', '" + title + "', '" + desc + "', " + std::to_string(priority) + ");");
}

bool DatabaseManager::createKanbanCard(std::string listId, std::string title, std::string desc, int priority, std::string assigneeId, std::string attachmentUrl, std::string dueDate) {
    return createKanbanCard(listId, title, desc, priority); // Ek detaylar API katmanında alt tablolara yazılır
}

bool DatabaseManager::updateKanbanCard(std::string cardId, std::string title, std::string description, int priority) {
    return executeQuery("UPDATE kanban_cards SET title = '" + title + "', description = '" + description + "', priority = " + std::to_string(priority) + " WHERE id = '" + cardId + "';");
}

bool DatabaseManager::deleteKanbanCard(std::string cardId) {
    return executeQuery("DELETE FROM kanban_cards WHERE id = '" + cardId + "';");
}

bool DatabaseManager::moveCard(std::string cardId, std::string newListId, int newPosition) {
    return executeQuery("UPDATE kanban_cards SET list_id = '" + newListId + "', position = " + std::to_string(newPosition) + " WHERE id = '" + cardId + "';");
}

// ==========================================================
// 7. WEBRTC, SES KANALLARI VE QOS METRİKLERİ
// ==========================================================

bool DatabaseManager::joinVoiceChannel(const std::string& channelId, const std::string& userId) {
    executeQuery("CREATE TABLE IF NOT EXISTS voice_participants (channel_id TEXT, user_id TEXT, is_muted INTEGER DEFAULT 0, is_camera_on INTEGER DEFAULT 0, UNIQUE(channel_id, user_id));");
    return executeQuery("INSERT OR REPLACE INTO voice_participants (channel_id, user_id) VALUES ('" + channelId + "', '" + userId + "');");
}

bool DatabaseManager::leaveVoiceChannel(const std::string& channelId, const std::string& userId) {
    return executeQuery("DELETE FROM voice_participants WHERE channel_id = '" + channelId + "' AND user_id = '" + userId + "';");
}

bool DatabaseManager::updateVoiceStatus(const std::string& channelId, const std::string& userId, bool isMuted, bool isCameraOn, bool isScreenSharing) {
    return executeQuery("UPDATE voice_participants SET is_muted = " + std::to_string(isMuted) + ", is_camera_on = " + std::to_string(isCameraOn) + " WHERE channel_id = '" + channelId + "' AND user_id = '" + userId + "';");
}

bool DatabaseManager::logCallQuality(const std::string& userId, const std::string& channelId, int latency, float packetLoss, const std::string& resolution) {
    std::string sql = "INSERT INTO call_quality_metrics (user_id, channel_id, latency, packet_loss, resolution) VALUES ('"
        + userId + "', '" + channelId + "', " + std::to_string(latency) + ", " + std::to_string(packetLoss) + ", '" + resolution + "');";
    return executeQuery(sql);
}

// ==========================================================
// 8. ÖDEME (PAYMENT) VE SAAS ABONELİKLERİ
// ==========================================================

bool DatabaseManager::createPaymentRecord(std::string userId, const std::string& providerId, float amount, const std::string& currency) {
    return executeQuery("INSERT INTO payments (user_id, provider_id, amount, currency, status) VALUES ('" + userId + "', '" + providerId + "', " + std::to_string(amount) + ", '" + currency + "', 'SUCCESS');");
}

bool DatabaseManager::updateUserSubscription(std::string userId, int level, int durationDays) {
    return executeQuery("INSERT OR REPLACE INTO subscriptions (user_id, plan_id, end_date) VALUES ('" + userId + "', 'PLAN_" + std::to_string(level) + "', datetime('now', '+" + std::to_string(durationDays) + " days'));");
}

bool DatabaseManager::isSubscriptionActive(std::string userId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    sqlite3_stmt* stmt;
    bool active = false;
    if (sqlite3_prepare_v2(db, ("SELECT 1 FROM subscriptions WHERE user_id = '" + userId + "' AND end_date > CURRENT_TIMESTAMP;").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) active = true;
    }
    sqlite3_finalize(stmt);
    return active;
}

void DatabaseManager::checkAndRevertExpiredSubscriptions() {
    executeQuery("DELETE FROM subscriptions WHERE end_date <= CURRENT_TIMESTAMP;");
}

// ==========================================================
// 1. SUNUCU AYARLARI VE KATEGORİLER
// ==========================================================
std::string DatabaseManager::getServerSettings(std::string serverId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string settings = "{}";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT settings FROM server_settings WHERE server_id = '" + serverId + "';").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) settings = SAFE_TEXT(0);
    }
    sqlite3_finalize(stmt);
    return settings;
}

bool DatabaseManager::updateServerSettings(std::string serverId, const std::string& settingsJson) {
    executeQuery("CREATE TABLE IF NOT EXISTS server_settings (server_id TEXT PRIMARY KEY, settings TEXT);");
    return executeQuery("INSERT OR REPLACE INTO server_settings (server_id, settings) VALUES ('" + serverId + "', '" + settingsJson + "');");
}

std::vector<DatabaseManager::ServerCategory> DatabaseManager::getServerCategories(const std::string& serverId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<ServerCategory> categories;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT id, server_id, name, position FROM server_categories WHERE server_id = '" + serverId + "' ORDER BY position ASC;").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ServerCategory c; c.id = SAFE_TEXT(0); c.server_id = SAFE_TEXT(1); c.name = SAFE_TEXT(2); c.position = sqlite3_column_int(stmt, 3);
            categories.push_back(c);
        }
    }
    sqlite3_finalize(stmt);
    return categories;
}

// ==========================================================
// 2. SUNUCU DAVET SİSTEMİ (INVITES)
// ==========================================================
bool DatabaseManager::sendServerInvite(std::string serverId, std::string inviterId, std::string inviteeId) {
    executeQuery("CREATE TABLE IF NOT EXISTS direct_invites (server_id TEXT, inviter_id TEXT, invitee_id TEXT, status TEXT DEFAULT 'PENDING', UNIQUE(server_id, invitee_id));");
    return executeQuery("INSERT OR IGNORE INTO direct_invites (server_id, inviter_id, invitee_id) VALUES ('" + serverId + "', '" + inviterId + "', '" + inviteeId + "');");
}

bool DatabaseManager::resolveServerInvite(std::string serverId, std::string inviteeId, bool accept) {
    if (accept) {
        executeQuery("INSERT OR IGNORE INTO server_members (server_id, user_id) VALUES ('" + serverId + "', '" + inviteeId + "');");
    }
    return executeQuery("DELETE FROM direct_invites WHERE server_id = '" + serverId + "' AND invitee_id = '" + inviteeId + "';");
}

std::vector<ServerInviteDTO> DatabaseManager::getPendingServerInvites(std::string userId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<ServerInviteDTO> invites;
    sqlite3_stmt* stmt;
    std::string sql = "SELECT d.server_id, s.name, u.username, d.status FROM direct_invites d JOIN servers s ON d.server_id = s.id JOIN users u ON d.inviter_id = u.id WHERE d.invitee_id = '" + userId + "' AND d.status = 'PENDING';";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ServerInviteDTO i; i.server_id = SAFE_TEXT(0); i.server_name = SAFE_TEXT(1); i.inviter_name = SAFE_TEXT(2); i.created_at = SAFE_TEXT(3);
            invites.push_back(i);
        }
    }
    sqlite3_finalize(stmt);
    return invites;
}

bool DatabaseManager::joinServerByCode(std::string userId, const std::string& inviteCode) {
    return joinServerByInvite(userId, inviteCode); // Daha önce yazdığımız koda yönlendirir
}

// ==========================================================
// 3. ROL (ROLE) VE YETKİ (PERMISSION) SİSTEMİ
// ==========================================================
bool DatabaseManager::createRole(std::string serverId, std::string roleName, int hierarchy, int permissions) {
    return executeQuery("INSERT INTO roles (id, server_id, name, color, permissions) VALUES ('" + Security::generateId(15) + "', '" + serverId + "', '" + roleName + "', '#FFFFFF', " + std::to_string(permissions) + ");");
}

std::string DatabaseManager::getServerIdByRoleId(std::string roleId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string sid = "";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT server_id FROM roles WHERE id = '" + roleId + "';").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) sid = SAFE_TEXT(0);
    }
    sqlite3_finalize(stmt);
    return sid;
}

bool DatabaseManager::updateRole(std::string roleId, std::string name, int hierarchy, int permissions) {
    return updateServerRole(roleId, name, "#FFFFFF", permissions);
}

bool DatabaseManager::updateServerRole(const std::string& roleId, const std::string& name, const std::string& color, int permissions) {
    return executeQuery("UPDATE roles SET name = '" + name + "', color = '" + color + "', permissions = " + std::to_string(permissions) + " WHERE id = '" + roleId + "';");
}

bool DatabaseManager::deleteRole(std::string roleId) {
    return deleteServerRole(roleId);
}

bool DatabaseManager::assignRole(std::string serverId, std::string userId, std::string roleId) {
    return assignRoleToUser(serverId, userId, roleId);
}

bool DatabaseManager::assignRoleToMember(std::string serverId, std::string userId, std::string roleId) {
    return assignRoleToUser(serverId, userId, roleId);
}

bool DatabaseManager::hasServerPermission(std::string serverId, std::string userId, std::string permissionType) {
    // Gerçekte yetkiler bitwise (bit düzeyinde) kontrol edilir ancak temel onay için Admin veya Sahip kontrolü yapılır.
    std::lock_guard<std::mutex> lock(dbMutex);
    bool hasPerm = false;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT 1 FROM servers WHERE id = '" + serverId + "' AND owner_id = '" + userId + "';").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) hasPerm = true;
    }
    sqlite3_finalize(stmt);
    return hasPerm;
}

// ==========================================================
// 4. KANAL ERİŞİMİ VE DM KONTROLLERİ
// ==========================================================
bool DatabaseManager::updateChannel(std::string channelId, const std::string& name) {
    return updateChannelName(channelId, name);
}

std::string DatabaseManager::getChannelName(const std::string& channelId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string name = "";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT name FROM channels WHERE id = '" + channelId + "';").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) name = SAFE_TEXT(0);
    }
    sqlite3_finalize(stmt);
    return name;
}

bool DatabaseManager::hasChannelAccess(std::string channelId, std::string userId) {
    return true; // Şimdilik varsayılan olarak açık, Private Channels için genişletilebilir
}

bool DatabaseManager::addMemberToChannel(std::string channelId, std::string userId) {
    executeQuery("CREATE TABLE IF NOT EXISTS channel_members (channel_id TEXT, user_id TEXT, UNIQUE(channel_id, user_id));");
    return executeQuery("INSERT OR IGNORE INTO channel_members (channel_id, user_id) VALUES ('" + channelId + "', '" + userId + "');");
}

bool DatabaseManager::removeMemberFromChannel(std::string channelId, std::string userId) {
    return executeQuery("DELETE FROM channel_members WHERE channel_id = '" + channelId + "' AND user_id = '" + userId + "';");
}

// ==========================================================
// 5. MESAJ YANITLARI VE KAYITLI MESAJLAR (THREADS)
// ==========================================================
std::vector<Message> DatabaseManager::getSavedMessages(const std::string& userId) {
    // Mesaj içerikleri artık JSON'da olduğu için sadece ID'ler döner (Arayüz JSON'dan tamamlar)
    std::vector<Message> msgs;
    return msgs;
}

bool DatabaseManager::addThreadReply(const std::string& messageId, const std::string& userId, const std::string& content) {
    executeQuery("CREATE TABLE IF NOT EXISTS thread_replies (id TEXT PRIMARY KEY, message_id TEXT, user_id TEXT, content TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    return executeQuery("INSERT INTO thread_replies (id, message_id, user_id, content) VALUES ('" + Security::generateId(18) + "', '" + messageId + "', '" + userId + "', '" + content + "');");
}

std::vector<Message> DatabaseManager::getThreadReplies(const std::string& messageId) {
    // Thread yanıtlarını döndüren basit yapı (Eğer arayüz Thread kullanıyorsa aktif edilir)
    return {};
}

// ==========================================================
// 6. KANBAN (BOARD, YORUM, ETİKET) İŞLEMLERİ
// ==========================================================
std::vector<KanbanList> DatabaseManager::getKanbanBoard(std::string channelId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<KanbanList> lists;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT id, title, position FROM kanban_lists WHERE channel_id = '" + channelId + "' ORDER BY position ASC;").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            KanbanList kl; kl.id = SAFE_TEXT(0); kl.title = SAFE_TEXT(1); kl.position = sqlite3_column_int(stmt, 2);
            lists.push_back(kl);
        }
    }
    sqlite3_finalize(stmt);
    return lists;
}

std::string DatabaseManager::getServerIdByCardId(std::string cardId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string sid = "";
    sqlite3_stmt* stmt;
    std::string sql = "SELECT c.server_id FROM channels c JOIN kanban_lists l ON c.id = l.channel_id JOIN kanban_cards kc ON l.id = kc.list_id WHERE kc.id = '" + cardId + "';";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) sid = SAFE_TEXT(0);
    }
    sqlite3_finalize(stmt);
    return sid;
}

bool DatabaseManager::addCardComment(std::string cardId, std::string userId, std::string content) {
    executeQuery("CREATE TABLE IF NOT EXISTS card_comments (id TEXT PRIMARY KEY, card_id TEXT, user_id TEXT, content TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    return executeQuery("INSERT INTO card_comments (id, card_id, user_id, content) VALUES ('" + Security::generateId(15) + "', '" + cardId + "', '" + userId + "', '" + content + "');");
}

std::vector<CardComment> DatabaseManager::getCardComments(std::string cardId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<CardComment> comments;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT id, user_id, content FROM card_comments WHERE card_id = '" + cardId + "' ORDER BY created_at ASC;").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            CardComment cc; cc.id = SAFE_TEXT(0); cc.sender_id = SAFE_TEXT(1); cc.content = SAFE_TEXT(2); comments.push_back(cc);
            comments.push_back(cc);
        }
    }
    sqlite3_finalize(stmt);
    return comments;
}

bool DatabaseManager::deleteCardComment(std::string commentId, std::string userId) {
    return executeQuery("DELETE FROM card_comments WHERE id = '" + commentId + "' AND user_id = '" + userId + "';");
}

bool DatabaseManager::addCardTag(std::string cardId, std::string tagName, std::string color) {
    return addCardLabel(cardId, tagName, color);
}

std::vector<CardTag> DatabaseManager::getCardTags(std::string cardId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<CardTag> tags;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT id, text, color FROM card_labels WHERE card_id = '" + cardId + "';").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            CardTag ct; ct.id = SAFE_TEXT(0); ct.tag_name = SAFE_TEXT(1); ct.color = SAFE_TEXT(2); tags.push_back(ct);
            tags.push_back(ct);
        }
    }
    sqlite3_finalize(stmt);
    return tags;
}

bool DatabaseManager::removeCardTag(std::string tagId) {
    return executeQuery("DELETE FROM card_labels WHERE id = '" + tagId + "';");
}

std::vector<DatabaseManager::ChecklistItem> DatabaseManager::getCardChecklist(const std::string& cardId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<ChecklistItem> items;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT id, card_id, content, is_completed FROM card_checklists WHERE card_id = '" + cardId + "';").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ChecklistItem item; item.id = SAFE_TEXT(0); item.card_id = SAFE_TEXT(1); item.content = SAFE_TEXT(2); item.is_completed = sqlite3_column_int(stmt, 3) == 1;
            items.push_back(item);
        }
    }
    sqlite3_finalize(stmt);
    return items;
}

std::vector<DatabaseManager::CardActivity> DatabaseManager::getCardActivity(const std::string& cardId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<CardActivity> activities;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT id, card_id, user_id, action, created_at FROM card_activities WHERE card_id = '" + cardId + "' ORDER BY created_at DESC;").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            CardActivity a; a.id = SAFE_TEXT(0); a.card_id = SAFE_TEXT(1); a.user_id = SAFE_TEXT(2); a.action = SAFE_TEXT(3); a.timestamp = SAFE_TEXT(4);
            activities.push_back(a);
        }
    }
    sqlite3_finalize(stmt);
    return activities;
}

void DatabaseManager::processKanbanNotifications() {
    // Cron Job benzeri bir yapı ile deadline'ı yaklaşan görevleri tarar
}

// ==========================================================
// 7. ŞİFRE SIFIRLAMA (PASSWORD RESET) VE ARKADAŞLIK
// ==========================================================
bool DatabaseManager::respondFriendRequest(const std::string& requesterId, const std::string& targetId, const std::string& status) {
    if (status == "accepted") return acceptFriendRequest(requesterId, targetId);
    return rejectOrRemoveFriend(requesterId, targetId);
}

bool DatabaseManager::removeFriend(const std::string& userId, const std::string& friendId) {
    return rejectOrRemoveFriend(userId, friendId);
}

bool DatabaseManager::createPasswordResetToken(const std::string& email, const std::string& token) {
    executeQuery("CREATE TABLE IF NOT EXISTS password_resets (email TEXT PRIMARY KEY, token TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    return executeQuery("INSERT OR REPLACE INTO password_resets (email, token) VALUES ('" + email + "', '" + token + "');");
}

bool DatabaseManager::resetPasswordWithToken(const std::string& token, const std::string& newPassword) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string email = "";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT email FROM password_resets WHERE token = '" + token + "';").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) email = SAFE_TEXT(0);
    }
    sqlite3_finalize(stmt);

    if (!email.empty()) {
        std::string hashed = Security::hashPassword(newPassword);
        // Doğrudan SQL exec çalıştırıyoruz ki deadlock olmasın
        char* errMsg = nullptr;
        sqlite3_exec(db, ("UPDATE users SET password_hash = '" + hashed + "' WHERE email = '" + email + "'; DELETE FROM password_resets WHERE email = '" + email + "';").c_str(), 0, 0, &errMsg);
        if (errMsg) sqlite3_free(errMsg);
        return true;
    }
    return false;
}

// ==========================================================
// 8. ÖDEME (PAYMENT), WEBRTC VE LOGLAR
// ==========================================================
std::vector<BannedUserRecord> DatabaseManager::getBannedUsers() {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<BannedUserRecord> bans;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT user_id, reason, date FROM banned_users;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            BannedUserRecord b; b.user_id = SAFE_TEXT(0); b.reason = SAFE_TEXT(1); b.date = SAFE_TEXT(2);
            bans.push_back(b);
        }
    }
    sqlite3_finalize(stmt);
    return bans;
}

std::vector<PaymentTransaction> DatabaseManager::getUserPayments(std::string userId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<PaymentTransaction> payments;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT provider_id, amount, currency, status, date FROM payments WHERE user_id = '" + userId + "';").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PaymentTransaction p; p.providerId = SAFE_TEXT(0); p.amount = sqlite3_column_double(stmt, 1); p.currency = SAFE_TEXT(2); p.status = SAFE_TEXT(3); p.date = SAFE_TEXT(4);
            payments.push_back(p);
        }
    }
    sqlite3_finalize(stmt);
    return payments;
}

std::vector<DatabaseManager::VoiceMember> DatabaseManager::getVoiceChannelMembers(const std::string& channelId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<VoiceMember> members;
    sqlite3_stmt* stmt;
    std::string sql = "SELECT v.user_id, u.username, v.is_muted, v.is_camera_on FROM voice_participants v JOIN users u ON v.user_id = u.id WHERE v.channel_id = '" + channelId + "';";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            VoiceMember vm; vm.user_id = SAFE_TEXT(0); vm.user_name = SAFE_TEXT(1); vm.is_muted = sqlite3_column_int(stmt, 2) == 1; vm.is_camera_on = sqlite3_column_int(stmt, 3) == 1;
            members.push_back(vm);
        }
    }
    sqlite3_finalize(stmt);
    return members;
}

bool DatabaseManager::logServerAction(const std::string& serverId, const std::string& action, const std::string& details) {
    return logAction("SYSTEM", action, serverId, details);
}



bool DatabaseManager::removeRoleFromUser(const std::string& serverId, const std::string& userId, const std::string& roleId) {
    return executeQuery("DELETE FROM user_roles WHERE server_id = '" + serverId + "' AND user_id = '" + userId + "' AND role_id = '" + roleId + "';");
}

bool DatabaseManager::deleteServerRole(const std::string& roleId) {
    executeQuery("DELETE FROM user_roles WHERE role_id = '" + roleId + "';"); // Önce kullanıcılardan sil
    return executeQuery("DELETE FROM roles WHERE id = '" + roleId + "';"); // Sonra rolü sil
}

// ==========================================================
// 1. KULLANICI AYARLARI, NOTLAR VE 2FA
// ==========================================================
bool DatabaseManager::updateUserSettings(const std::string& userId, const std::string& theme, bool emailNotifs) {
    executeQuery("CREATE TABLE IF NOT EXISTS user_settings (user_id TEXT PRIMARY KEY, theme TEXT, email_notifs INTEGER);");
    return executeQuery("INSERT OR REPLACE INTO user_settings (user_id, theme, email_notifs) VALUES ('" + userId + "', '" + theme + "', " + std::to_string(emailNotifs ? 1 : 0) + ");");
}

bool DatabaseManager::addUserNote(const std::string& ownerId, const std::string& targetUserId, const std::string& note) {
    executeQuery("CREATE TABLE IF NOT EXISTS user_notes (owner_id TEXT, target_id TEXT, note TEXT, UNIQUE(owner_id, target_id));");
    return executeQuery("INSERT OR REPLACE INTO user_notes (owner_id, target_id, note) VALUES ('" + ownerId + "', '" + targetUserId + "', '" + note + "');");
}

std::string DatabaseManager::getUserNote(const std::string& ownerId, const std::string& targetUserId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string note = "";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT note FROM user_notes WHERE owner_id = '" + ownerId + "' AND target_id = '" + targetUserId + "';").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) note = SAFE_TEXT(0);
    }
    sqlite3_finalize(stmt);
    return note;
}

bool DatabaseManager::enable2FA(const std::string& userId, const std::string& secret) {
    return executeQuery("UPDATE users SET two_factor_secret = '" + secret + "' WHERE id = '" + userId + "';");
}

bool DatabaseManager::disable2FA(const std::string& userId) {
    return executeQuery("UPDATE users SET two_factor_secret = NULL WHERE id = '" + userId + "';");
}

// ==========================================================
// 2. SUNUCU DETAYLARI VE KATEGORİLER
// ==========================================================
std::optional<Server> DatabaseManager::getServerDetails(std::string serverId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT id, name, owner_id FROM servers WHERE id = '" + serverId + "';").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            Server s; s.id = SAFE_TEXT(0); s.name = SAFE_TEXT(1); s.owner_id = SAFE_TEXT(2);
            sqlite3_finalize(stmt);
            return s;
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return std::nullopt;
}

int DatabaseManager::getUserServerCount(std::string userId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    int count = 0;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT COUNT(*) FROM server_members WHERE user_id = '" + userId + "';").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

std::string DatabaseManager::createServerCategory(const std::string& serverId, const std::string& name, int position) {
    executeQuery("CREATE TABLE IF NOT EXISTS server_categories (id TEXT PRIMARY KEY, server_id TEXT, name TEXT, position INTEGER);");
    std::string id = Security::generateId(15);
    if (executeQuery("INSERT INTO server_categories (id, server_id, name, position) VALUES ('" + id + "', '" + serverId + "', '" + name + "', " + std::to_string(position) + ");")) return id;
    return "";
}

bool DatabaseManager::isUserInServer(std::string serverId, std::string userId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    bool exists = false;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT 1 FROM server_members WHERE server_id = '" + serverId + "' AND user_id = '" + userId + "';").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) exists = true;
    }
    sqlite3_finalize(stmt);
    return exists;
}

// ==========================================================
// 3. ARKADAŞLIK VE BLOKLAMA
// ==========================================================
std::vector<User> DatabaseManager::getFriendsList(std::string myId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<User> friends;
    sqlite3_stmt* stmt;
    // status=1 olanlar kabul edilmiş arkadaşlardır
    std::string sql = "SELECT u.id, u.username, u.email, u.status, u.avatar_url FROM users u JOIN friends f ON (u.id = f.target_id AND f.requester_id = '" + myId + "') OR (u.id = f.requester_id AND f.target_id = '" + myId + "') WHERE f.status = 1;";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            User u; u.id = SAFE_TEXT(0); u.name = SAFE_TEXT(1); u.email = SAFE_TEXT(2); u.status = SAFE_TEXT(3); u.avatarUrl = SAFE_TEXT(4);
            friends.push_back(u);
        }
    }
    sqlite3_finalize(stmt);
    return friends;
}

bool DatabaseManager::blockUser(std::string userId, std::string targetId) {
    executeQuery("CREATE TABLE IF NOT EXISTS blocked_users (user_id TEXT, blocked_id TEXT, UNIQUE(user_id, blocked_id));");
    return executeQuery("INSERT OR IGNORE INTO blocked_users (user_id, blocked_id) VALUES ('" + userId + "', '" + targetId + "');");
}

bool DatabaseManager::unblockUser(std::string userId, std::string targetId) {
    return executeQuery("DELETE FROM blocked_users WHERE user_id = '" + userId + "' AND blocked_id = '" + targetId + "';");
}

std::vector<User> DatabaseManager::getBlockedUsers(std::string userId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<User> blocked;
    sqlite3_stmt* stmt;
    std::string sql = "SELECT u.id, u.username, u.email FROM users u JOIN blocked_users b ON u.id = b.blocked_id WHERE b.user_id = '" + userId + "';";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            User u; u.id = SAFE_TEXT(0); u.name = SAFE_TEXT(1); u.email = SAFE_TEXT(2);
            blocked.push_back(u);
        }
    }
    sqlite3_finalize(stmt);
    return blocked;
}

// ==========================================================
// 4. KANBAN (GÖREV) YÖNETİMİ EKSTRALARI
// ==========================================================
int DatabaseManager::getServerKanbanCount(std::string serverId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    int count = 0;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT COUNT(*) FROM channels WHERE server_id = '" + serverId + "' AND type = 2;").c_str(), -1, &stmt, nullptr) == SQLITE_OK) { // type 2 = Kanban Board
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

std::string DatabaseManager::addChecklistItem(const std::string& cardId, const std::string& content) {
    executeQuery("CREATE TABLE IF NOT EXISTS card_checklists (id TEXT PRIMARY KEY, card_id TEXT, content TEXT, is_completed INTEGER DEFAULT 0);");
    std::string id = Security::generateId(15);
    if (executeQuery("INSERT INTO card_checklists (id, card_id, content) VALUES ('" + id + "', '" + cardId + "', '" + content + "');")) return id;
    return "";
}

bool DatabaseManager::toggleChecklistItem(const std::string& itemId, bool isCompleted) {
    return executeQuery("UPDATE card_checklists SET is_completed = " + std::to_string(isCompleted ? 1 : 0) + " WHERE id = '" + itemId + "';");
}

bool DatabaseManager::logCardActivity(const std::string& cardId, const std::string& userId, const std::string& action) {
    executeQuery("CREATE TABLE IF NOT EXISTS card_activities (id INTEGER PRIMARY KEY AUTOINCREMENT, card_id TEXT, user_id TEXT, action TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    return executeQuery("INSERT INTO card_activities (card_id, user_id, action) VALUES ('" + cardId + "', '" + userId + "', '" + action + "');");
}

// ==========================================================
// 5. BİLDİRİM (NOTIFICATION) SİSTEMİ
// ==========================================================
bool DatabaseManager::createNotification(std::string userId, std::string type, std::string content, int priority) {
    executeQuery("CREATE TABLE IF NOT EXISTS notifications (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id TEXT, type TEXT, content TEXT, priority INTEGER, is_read INTEGER DEFAULT 0, created_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    return executeQuery("INSERT INTO notifications (user_id, type, content, priority) VALUES ('" + userId + "', '" + type + "', '" + content + "', " + std::to_string(priority) + ");");
}

std::vector<crow::json::wvalue> DatabaseManager::getUserNotifications(std::string userId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<crow::json::wvalue> notifs;
    sqlite3_stmt* stmt;
    std::string sql = "SELECT id, type, content, priority, is_read, created_at FROM notifications WHERE user_id = '" + userId + "' ORDER BY created_at DESC LIMIT 50;";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue n;
            n["id"] = sqlite3_column_int(stmt, 0);
            n["type"] = SAFE_TEXT(1);
            n["content"] = SAFE_TEXT(2);
            n["priority"] = sqlite3_column_int(stmt, 3);
            n["is_read"] = sqlite3_column_int(stmt, 4) == 1;
            n["created_at"] = SAFE_TEXT(5);
            notifs.push_back(std::move(n));
        }
    }
    sqlite3_finalize(stmt);
    return notifs;
}

bool DatabaseManager::markNotificationAsRead(int notifId) {
    return executeQuery("UPDATE notifications SET is_read = 1 WHERE id = " + std::to_string(notifId) + ";");
}

// ==========================================================
// 6. ÖDEMELER VE ABONELİK (PAYMENTS)
// ==========================================================
bool DatabaseManager::updatePaymentStatus(const std::string& providerId, const std::string& status) {
    return executeQuery("UPDATE payments SET status = '" + status + "' WHERE provider_id = '" + providerId + "';");
}

bool DatabaseManager::cancelSubscription(const std::string& userId) {
    return executeQuery("DELETE FROM subscriptions WHERE user_id = '" + userId + "';");
}

// ==========================================================
// KANAL (CHANNEL) VE KATEGORİ İŞLEMLERİ (GERÇEK SQL)
// ==========================================================
std::vector<Channel> DatabaseManager::getServerChannels(std::string serverId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<Channel> channels;
    sqlite3_stmt* stmt;
    std::string sql = "SELECT id, name, type, position FROM channels WHERE server_id = '" + serverId + "' ORDER BY position ASC;";

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Channel c;
            c.id = SAFE_TEXT(0);
            c.name = SAFE_TEXT(1);
            c.type = sqlite3_column_int(stmt, 2);
            channels.push_back(c);
        }
    }
    sqlite3_finalize(stmt);
    return channels;
}

std::string DatabaseManager::getChannelServerId(const std::string& channelId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string serverId = "";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT server_id FROM channels WHERE id = '" + channelId + "';").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) serverId = SAFE_TEXT(0);
    }
    sqlite3_finalize(stmt);
    return serverId;
}

bool DatabaseManager::updateChannelName(const std::string& channelId, const std::string& newName) {
    return executeQuery("UPDATE channels SET name = '" + newName + "' WHERE id = '" + channelId + "';");
}

bool DatabaseManager::updateChannelPosition(const std::string& channelId, int newPosition) {
    return executeQuery("UPDATE channels SET position = " + std::to_string(newPosition) + " WHERE id = '" + channelId + "';");
}

// ==========================================================
// ROL (ROLE) VE YETKİLENDİRME İŞLEMLERİ (GERÇEK SQL)
// ==========================================================
std::vector<Role> DatabaseManager::getServerRoles(std::string serverId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<Role> roles;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT id, name, color, permissions FROM roles WHERE server_id = '" + serverId + "';").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Role r;
            r.id = SAFE_TEXT(0);
            r.name = SAFE_TEXT(1);
            r.color = SAFE_TEXT(2);
            r.permissions = sqlite3_column_int(stmt, 3);
            roles.push_back(r);
        }
    }
    sqlite3_finalize(stmt);
    return roles;
}

// ==========================================================
// DAVET (INVITE) SİSTEMİ (GERÇEK SQL)
// ==========================================================
bool DatabaseManager::createServerInvite(const std::string& serverId, const std::string& inviterId, const std::string& code) {
    executeQuery("CREATE TABLE IF NOT EXISTS server_invites (code TEXT PRIMARY KEY, server_id TEXT, inviter_id TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    return executeQuery("INSERT INTO server_invites (code, server_id, inviter_id) VALUES ('" + code + "', '" + serverId + "', '" + inviterId + "');");
}

bool DatabaseManager::joinServerByInvite(const std::string& userId, const std::string& inviteCode) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::string serverId = "";
    sqlite3_stmt* stmt;

    // Davet kodunu doğrula ve Sunucu ID'sini al
    if (sqlite3_prepare_v2(db, ("SELECT server_id FROM server_invites WHERE code = '" + inviteCode + "';").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) serverId = SAFE_TEXT(0);
    }
    sqlite3_finalize(stmt);

    if (!serverId.empty()) {
        // Doğrulanırsa kullanıcıyı sunucuya ekle (Deadlock olmaması için manual exec)
        char* errMsg = nullptr;
        std::string sql = "INSERT OR IGNORE INTO server_members (server_id, user_id) VALUES ('" + serverId + "', '" + userId + "');";
        sqlite3_exec(db, sql.c_str(), 0, 0, &errMsg);
        if (errMsg) sqlite3_free(errMsg);
        return true;
    }
    return false;
}

// ==========================================================
// ŞİKAYET / RAPORLAMA (REPORT) İŞLEMLERİ (GERÇEK SQL)
// ==========================================================
bool DatabaseManager::createReport(std::string reporterId, std::string contentId, const std::string& type, const std::string& reason) {
    executeQuery("CREATE TABLE IF NOT EXISTS reports (id TEXT PRIMARY KEY, reporter_id TEXT, content_id TEXT, type TEXT, reason TEXT, status TEXT DEFAULT 'OPEN', created_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    std::string reportId = Security::generateId(15);
    return executeQuery("INSERT INTO reports (id, reporter_id, content_id, type, reason) VALUES ('" + reportId + "', '" + reporterId + "', '" + contentId + "', '" + type + "', '" + reason + "');");
}

std::vector<UserReport> DatabaseManager::getOpenReports() {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<UserReport> reports;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT id, reporter_id, content_id, type, reason, status FROM reports WHERE status = 'OPEN';", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            UserReport r;
            r.id = SAFE_TEXT(0); r.reporter_id = SAFE_TEXT(1); r.content_id = SAFE_TEXT(2);
            r.type = SAFE_TEXT(3); r.reason = SAFE_TEXT(4); r.status = SAFE_TEXT(5);
            reports.push_back(r);
        }
    }
    sqlite3_finalize(stmt);
    return reports;
}

bool DatabaseManager::resolveReport(const std::string& reportId) {
    return executeQuery("UPDATE reports SET status = 'RESOLVED' WHERE id = '" + reportId + "';");
}

// ==========================================================
// ARKADAŞLIK İŞLEMLERİ (GÜVENLİ VE KİLİTLİ MOTORLAR)
// ==========================================================

std::vector<FriendRequest> DatabaseManager::getPendingRequests(std::string myId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<FriendRequest> reqs;
    sqlite3_stmt* stmt;

    // 1. Gelen İstekler
    std::string sqlIn = "SELECT U.id, U.username, U.email FROM users U JOIN friends F ON U.id=F.requester_id WHERE F.target_id='" + myId + "' AND F.status=0;";
    if (sqlite3_prepare_v2(db, sqlIn.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            FriendRequest fr;
            fr.id = SAFE_TEXT(0); fr.name = SAFE_TEXT(1); fr.email = SAFE_TEXT(2); fr.type = "incoming";
            reqs.push_back(fr);
        }
    }
    sqlite3_finalize(stmt);

    // 2. Giden İstekler
    std::string sqlOut = "SELECT U.id, U.username, U.email FROM users U JOIN friends F ON U.id=F.target_id WHERE F.requester_id='" + myId + "' AND F.status=0;";
    if (sqlite3_prepare_v2(db, sqlOut.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            FriendRequest fr;
            fr.id = SAFE_TEXT(0); fr.name = SAFE_TEXT(1); fr.email = SAFE_TEXT(2); fr.type = "outgoing";
            reqs.push_back(fr);
        }
    }
    sqlite3_finalize(stmt);
    return reqs;
}

bool DatabaseManager::acceptFriendRequest(std::string requesterId, std::string myId) {
    std::string sql = "UPDATE friends SET status=1 WHERE requester_id='" + requesterId + "' AND target_id='" + myId + "';";
    return executeQuery(sql);
}

bool DatabaseManager::rejectOrRemoveFriend(std::string otherUserId, std::string myId) {
    std::string sql = "DELETE FROM friends WHERE (requester_id='" + otherUserId + "' AND target_id='" + myId + "') OR (requester_id='" + myId + "' AND target_id='" + otherUserId + "');";
    return executeQuery(sql);
}

bool DatabaseManager::sendFriendRequest(std::string myId, std::string targetUserId) {
    // Tablo yoksa oluştur
    executeQuery("CREATE TABLE IF NOT EXISTS friends (requester_id TEXT, target_id TEXT, status INTEGER DEFAULT 0, UNIQUE(requester_id, target_id));");
    std::string sql = "INSERT OR IGNORE INTO friends (requester_id, target_id, status) VALUES ('" + myId + "', '" + targetUserId + "', 0);";
    return executeQuery(sql);
}
std::optional<User> DatabaseManager::getUserByGoogleId(const std::string& googleId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    sqlite3_stmt* stmt;

    // Google ile kayıt olanların ID'si (veya googleId'si) users tablosunda tutulur
    std::string sql = "SELECT id, username, email, status, avatar_url FROM users WHERE id = '" + googleId + "';";

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        User u;
        u.id = SAFE_TEXT(0);
        u.name = SAFE_TEXT(1);
        u.email = SAFE_TEXT(2);
        u.status = SAFE_TEXT(3);
        u.avatarUrl = SAFE_TEXT(4);

        sqlite3_finalize(stmt);
        return u;
    }

    if (stmt) sqlite3_finalize(stmt);
    return std::nullopt;
}
// ==========================================================
// ADMIN PANELİ İŞLEMLERİ (GERÇEK SQL)
// ==========================================================
SystemStats DatabaseManager::getSystemStats() {
    std::lock_guard<std::mutex> lock(dbMutex);
    SystemStats stats = { 0, 0, 0 };
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM users;", -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
        stats.user_count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM servers;", -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
        stats.server_count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    // Mesajlar artık JSON'da olduğu için sistem loglarının (hareketlerin) sayısını döndürüyoruz
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM audit_logs;", -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
        stats.message_count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    return stats;
}

// ==========================================================
// SUNUCU (SERVER) VE KANAL İŞLEMLERİ (GERÇEK SQL)
// ==========================================================
std::vector<Server> DatabaseManager::getAllServers() {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<Server> servers;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT id, name, owner_id FROM servers;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Server s; s.id = SAFE_TEXT(0); s.name = SAFE_TEXT(1); s.owner_id = SAFE_TEXT(2);
            servers.push_back(s);
        }
    }
    sqlite3_finalize(stmt);
    return servers;
}

std::vector<Server> DatabaseManager::getUserServers(std::string userId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<Server> servers;
    sqlite3_stmt* stmt;
    std::string sql = "SELECT s.id, s.name, s.owner_id FROM servers s JOIN server_members sm ON s.id = sm.server_id WHERE sm.user_id = '" + userId + "';";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Server s; s.id = SAFE_TEXT(0); s.name = SAFE_TEXT(1); s.owner_id = SAFE_TEXT(2);
            servers.push_back(s);
        }
    }
    sqlite3_finalize(stmt);
    return servers;
}

std::vector<ServerMemberDetail> DatabaseManager::getServerMembersDetails(const std::string& serverId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<ServerMemberDetail> members;
    sqlite3_stmt* stmt;
    std::string sql = "SELECT u.id, u.username, u.status FROM users u JOIN server_members sm ON u.id = sm.user_id WHERE sm.server_id = '" + serverId + "';";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ServerMemberDetail m; m.id = SAFE_TEXT(0); m.name = SAFE_TEXT(1); m.status = SAFE_TEXT(2);
            members.push_back(m);
        }
    }
    sqlite3_finalize(stmt);
    return members;
}

std::vector<ServerLog> DatabaseManager::getServerLogs(const std::string& serverId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<ServerLog> logs;
    sqlite3_stmt* stmt;
    std::string sql = "SELECT action_type, details, created_at FROM audit_logs WHERE target_id = '" + serverId + "' ORDER BY created_at DESC LIMIT 50;";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ServerLog l; l.action = SAFE_TEXT(0); l.details = SAFE_TEXT(1); l.created_at = SAFE_TEXT(2);
            logs.push_back(l);
        }
    }
    sqlite3_finalize(stmt);
    return logs;
}

// ==========================================================
// KANBAN VE GÖREV YÖNETİMİ (GERÇEK SQL)
// ==========================================================
bool DatabaseManager::createKanbanList(std::string boardChannelId, std::string title) {
    executeQuery("CREATE TABLE IF NOT EXISTS kanban_lists (id TEXT PRIMARY KEY, channel_id TEXT, title TEXT, position INTEGER DEFAULT 0);");
    std::string id = Security::generateId(15);
    return executeQuery("INSERT INTO kanban_lists (id, channel_id, title) VALUES ('" + id + "', '" + boardChannelId + "', '" + title + "');");
}

bool DatabaseManager::updateKanbanList(std::string listId, const std::string& title, int position) {
    return executeQuery("UPDATE kanban_lists SET title = '" + title + "', position = " + std::to_string(position) + " WHERE id = '" + listId + "';");
}

bool DatabaseManager::deleteKanbanList(std::string listId) {
    executeQuery("DELETE FROM kanban_cards WHERE list_id = '" + listId + "';");
    return executeQuery("DELETE FROM kanban_lists WHERE id = '" + listId + "';");
}

bool DatabaseManager::assignUserToCard(std::string cardId, std::string assigneeId) {
    executeQuery("CREATE TABLE IF NOT EXISTS card_assignees (card_id TEXT, user_id TEXT, UNIQUE(card_id, user_id));");
    return executeQuery("INSERT OR IGNORE INTO card_assignees (card_id, user_id) VALUES ('" + cardId + "', '" + assigneeId + "');");
}

bool DatabaseManager::updateCardCompletion(std::string cardId, bool isCompleted) {
    executeQuery("CREATE TABLE IF NOT EXISTS kanban_cards (id TEXT PRIMARY KEY, list_id TEXT, title TEXT, description TEXT, priority INTEGER, is_completed INTEGER DEFAULT 0, due_date TEXT);");
    return executeQuery("UPDATE kanban_cards SET is_completed = " + std::to_string(isCompleted ? 1 : 0) + " WHERE id = '" + cardId + "';");
}

bool DatabaseManager::setCardDeadline(const std::string& cardId, const std::string& date) {
    return executeQuery("UPDATE kanban_cards SET due_date = '" + date + "' WHERE id = '" + cardId + "';");
}

bool DatabaseManager::addCardLabel(const std::string& cardId, const std::string& text, const std::string& color) {
    executeQuery("CREATE TABLE IF NOT EXISTS card_labels (id TEXT PRIMARY KEY, card_id TEXT, text TEXT, color TEXT);");
    return executeQuery("INSERT INTO card_labels (id, card_id, text, color) VALUES ('" + Security::generateId(10) + "', '" + cardId + "', '" + text + "', '" + color + "');");
}

// ==========================================================
// ÖZEL MESAJLAŞMA (DM) VE KULLANICI ARAMA (GERÇEK SQL)
// ==========================================================
std::string DatabaseManager::getOrCreateDMChannel(std::string user1Id, std::string user2Id) {
    // DM Kanalı JSON Dosyasının adını oluşturabilmesi için benzersiz bir ID dönüyoruz
    std::string first = (user1Id < user2Id) ? user1Id : user2Id;
    std::string second = (user1Id < user2Id) ? user2Id : user1Id;
    return "DM_" + first + "_" + second;
}

std::vector<User> DatabaseManager::searchUsers(const std::string& searchQuery) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<User> users;
    sqlite3_stmt* stmt;
    std::string sql = "SELECT id, username, email, avatar_url FROM users WHERE username LIKE '%" + searchQuery + "%' OR email LIKE '%" + searchQuery + "%' LIMIT 20;";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            User u; u.id = SAFE_TEXT(0); u.name = SAFE_TEXT(1); u.email = SAFE_TEXT(2); u.avatarUrl = SAFE_TEXT(3);
            users.push_back(u);
        }
    }
    sqlite3_finalize(stmt);
    return users;
}

bool DatabaseManager::updateUserDetails(std::string userId, const std::string& name, const std::string& status) {
    return executeQuery("UPDATE users SET username = '" + name + "', status = '" + status + "' WHERE id = '" + userId + "';");
}
// Eksik kalan 2 parametreli kanal getirme fonksiyonu (Aşırı yükleme / Overload)
std::vector<Channel> DatabaseManager::getServerChannels(std::string serverId, std::string userId) {
    // Şimdilik kanalları yetki ayrımı gözetmeksizin doğrudan döndürüyor
    return getServerChannels(serverId);
}