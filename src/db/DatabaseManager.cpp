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
    std::lock_guard<std::mutex> lock(dbMutex);
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) return false;

    // Yüksek performanslı WAL modu ve Yabancı Anahtar (Foreign Key) aktivasyonu
    return executeQuery("PRAGMA foreign_keys = ON; PRAGMA journal_mode = WAL;");
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

std::vector<Channel> DatabaseManager::getServerChannels(std::string serverId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<Channel> channels;
    sqlite3_stmt* stmt;
    std::string sql = "SELECT id, name, type FROM channels WHERE server_id = '" + serverId + "' ORDER BY position ASC;";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Channel c; c.id = SAFE_TEXT(0); c.name = SAFE_TEXT(1); c.type = sqlite3_column_int(stmt, 2);
            channels.push_back(c);
        }
    }
    sqlite3_finalize(stmt);
    return channels;
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
// BOŞ/STUB METOTLAR (Diğer Modüller İçin Hata Vermemesi Adına)
// ==========================================================
// Proje derlemesinde "Unresolved External Symbol" (LNK2019) hatası almamak için 
// .h dosyasında olan ancak henüz içeriği doldurulmamış diğer tüm fonksiyonları döndürüyoruz.

bool DatabaseManager::updateUserSettings(const std::string& userId, const std::string& theme, bool emailNotifs) { return true; }

std::optional<Server> DatabaseManager::getServerDetails(std::string serverId) { return std::nullopt; }
int DatabaseManager::getUserServerCount(std::string userId) { return 0; }
std::string DatabaseManager::getServerSettings(std::string serverId) { return "{}"; }
bool DatabaseManager::updateServerSettings(std::string serverId, const std::string& settingsJson) { return true; }

std::string DatabaseManager::createServerCategory(const std::string& serverId, const std::string& name, int position) { return "CAT_ID"; }
std::vector<DatabaseManager::ServerCategory> DatabaseManager::getServerCategories(const std::string& serverId) { return {}; }
bool DatabaseManager::isUserInServer(std::string serverId, std::string userId) { return true; }
bool DatabaseManager::createServerInvite(const std::string& serverId, const std::string& inviterId, const std::string& code) { return true; }
bool DatabaseManager::sendServerInvite(std::string serverId, std::string inviterId, std::string inviteeId) { return true; }
bool DatabaseManager::resolveServerInvite(std::string serverId, std::string inviteeId, bool accept) { return true; }
std::vector<ServerInviteDTO> DatabaseManager::getPendingServerInvites(std::string userId) { return {}; }
bool DatabaseManager::joinServerByInvite(const std::string& userId, const std::string& inviteCode) { return true; }
bool DatabaseManager::joinServerByCode(std::string userId, const std::string& inviteCode) { return true; }

bool DatabaseManager::createRole(std::string serverId, std::string roleName, int hierarchy, int permissions) { return true; }
std::vector<Role> DatabaseManager::getServerRoles(std::string serverId) { return {}; }
std::string DatabaseManager::getServerIdByRoleId(std::string roleId) { return ""; }
bool DatabaseManager::updateRole(std::string roleId, std::string name, int hierarchy, int permissions) { return true; }
bool DatabaseManager::updateServerRole(const std::string& roleId, const std::string& name, const std::string& color, int permissions) { return true; }
bool DatabaseManager::deleteRole(std::string roleId) { return true; }
bool DatabaseManager::deleteServerRole(const std::string& roleId) { return true; }
bool DatabaseManager::assignRole(std::string serverId, std::string userId, std::string roleId) { return true; }
bool DatabaseManager::assignRoleToMember(std::string serverId, std::string userId, std::string roleId) { return true; }
bool DatabaseManager::removeRoleFromUser(const std::string& serverId, const std::string& userId, const std::string& roleId) { return true; }
bool DatabaseManager::hasServerPermission(std::string serverId, std::string userId, std::string permissionType) { return true; }

bool DatabaseManager::updateChannel(std::string channelId, const std::string& name) { return true; }
bool DatabaseManager::updateChannelName(const std::string& channelId, const std::string& newName) { return true; }
bool DatabaseManager::updateChannelPosition(const std::string& channelId, int newPosition) { return true; }
std::vector<Channel> DatabaseManager::getServerChannels(std::string serverId, std::string userId) { return {}; }
std::string DatabaseManager::getChannelServerId(const std::string& channelId) { return ""; }
std::string DatabaseManager::getChannelName(const std::string& channelId) { return ""; }
bool DatabaseManager::hasChannelAccess(std::string channelId, std::string userId) { return true; }
bool DatabaseManager::addMemberToChannel(std::string channelId, std::string userId) { return true; }
bool DatabaseManager::removeMemberFromChannel(std::string channelId, std::string userId) { return true; }

std::vector<Message> DatabaseManager::getSavedMessages(const std::string& userId) { return {}; }
bool DatabaseManager::addThreadReply(const std::string& messageId, const std::string& userId, const std::string& content) { return true; }
std::vector<Message> DatabaseManager::getThreadReplies(const std::string& messageId) { return {}; }

int DatabaseManager::getServerKanbanCount(std::string serverId) { return 0; }
std::vector<KanbanList> DatabaseManager::getKanbanBoard(std::string channelId) { return {}; }
std::string DatabaseManager::getServerIdByCardId(std::string cardId) { return ""; }
std::vector<CardComment> DatabaseManager::getCardComments(std::string cardId) { return {}; }
bool DatabaseManager::addCardComment(std::string cardId, std::string userId, std::string content) { return true; }
bool DatabaseManager::deleteCardComment(std::string commentId, std::string userId) { return true; }
std::vector<CardTag> DatabaseManager::getCardTags(std::string cardId) { return {}; }
bool DatabaseManager::addCardTag(std::string cardId, std::string tagName, std::string color) { return true; }
bool DatabaseManager::removeCardTag(std::string tagId) { return true; }

std::string DatabaseManager::addChecklistItem(const std::string& cardId, const std::string& content) { return Security::generateId(18); }
bool DatabaseManager::toggleChecklistItem(const std::string& itemId, bool isCompleted) { return true; }
std::vector<DatabaseManager::ChecklistItem> DatabaseManager::getCardChecklist(const std::string& cardId) { return {}; }
bool DatabaseManager::logCardActivity(const std::string& cardId, const std::string& userId, const std::string& action) { return true; }
std::vector<DatabaseManager::CardActivity> DatabaseManager::getCardActivity(const std::string& cardId) { return {}; }
void DatabaseManager::processKanbanNotifications() {}

bool DatabaseManager::respondFriendRequest(const std::string& requesterId, const std::string& targetId, const std::string& status) { return true; }
bool DatabaseManager::removeFriend(const std::string& userId, const std::string& friendId) { return true; }
std::vector<User> DatabaseManager::getFriendsList(std::string myId) { return {}; }
bool DatabaseManager::blockUser(std::string userId, std::string targetId) { return true; }
bool DatabaseManager::unblockUser(std::string userId, std::string targetId) { return true; }
std::vector<User> DatabaseManager::getBlockedUsers(std::string userId) { return {}; }
bool DatabaseManager::addUserNote(const std::string& ownerId, const std::string& targetUserId, const std::string& note) { return true; }
std::string DatabaseManager::getUserNote(const std::string& ownerId, const std::string& targetUserId) { return ""; }

std::vector<BannedUserRecord> DatabaseManager::getBannedUsers() { return {}; }
bool DatabaseManager::enable2FA(const std::string& userId, const std::string& secret) { return true; }
bool DatabaseManager::disable2FA(const std::string& userId) { return true; }
bool DatabaseManager::createPasswordResetToken(const std::string& email, const std::string& token) { return true; }
bool DatabaseManager::resetPasswordWithToken(const std::string& token, const std::string& newPassword) { return true; }

bool DatabaseManager::createReport(std::string reporterId, std::string contentId, const std::string& type, const std::string& reason) { return true; }
std::vector<UserReport> DatabaseManager::getOpenReports() { return {}; }
bool DatabaseManager::resolveReport(const std::string& reportId) { return true; }

bool DatabaseManager::updatePaymentStatus(const std::string& providerId, const std::string& status) { return true; }
std::vector<PaymentTransaction> DatabaseManager::getUserPayments(std::string userId) { return {}; }
bool DatabaseManager::cancelSubscription(const std::string& userId) { return true; }

std::vector<DatabaseManager::VoiceMember> DatabaseManager::getVoiceChannelMembers(const std::string& channelId) { return {}; }
bool DatabaseManager::createNotification(std::string userId, std::string type, std::string content, int priority) { return true; }
std::vector<crow::json::wvalue> DatabaseManager::getUserNotifications(std::string userId) { return {}; }
bool DatabaseManager::markNotificationAsRead(int notifId) { return true; }
bool DatabaseManager::logServerAction(const std::string& serverId, const std::string& action, const std::string& details) { return true; }
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