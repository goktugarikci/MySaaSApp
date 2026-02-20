#pragma once
#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>

// Model referansları (Mevcut .h dosyalarınızdan geliyor)
#include "../models/User.h"
#include "../models/Server.h"
#include "../models/Message.h"
#include "../models/Kanban.h"
#include "../models/Requests.h"
#include "../models/Payment.h"

// Sistem istatistiklerini tutan yapı (Admin UI için)
struct SystemStats {
    int user_count;
    int server_count;
    int message_count;
};

// Raporlama yapısı
struct UserReport {
    std::string id;
    std::string reporterId;
    std::string contentId;
    std::string type;
    std::string reason;
    std::string status;
};

class DatabaseManager {
private:
    std::string db_path;
    sqlite3* db;

    // Özel yardımcı metot: SQL sorgusu çalıştırmak için
    bool executeQuery(const std::string& sql);

public:
    // YENİ DTO (Data Transfer Object) YAPILARI
    struct ServerInviteDTO {
        std::string serverId;
        std::string serverName;
        std::string inviterName;
        std::string createdAt;
    };

    struct ServerMemberDetail {
        std::string id;
        std::string name;
        std::string status;
    };

    struct ServerLog {
        std::string createdAt;
        std::string action;
        std::string details;
    };

    struct NotificationDTO {
        int id;
        std::string message;
        std::string type;
        std::string createdAt;
    };

    DatabaseManager(const std::string& path);
    ~DatabaseManager();

    // Veritabanı Bağlantısı ve Kurulum
    bool open();
    void close();
    bool initTables();

    // ==========================================
    // SİSTEM & YÖNETİM METOTLARI
    // ==========================================
    SystemStats getSystemStats();
    std::vector<User> getAllUsers();
    bool isSystemAdmin(std::string userId);

    // ==========================================
    // KULLANICI METOTLARI
    // ==========================================
    std::string authenticateUser(const std::string& email, const std::string& password);
    bool createUser(const std::string& name, const std::string& email, const std::string& rawPassword, bool isAdmin = false);
    bool createGoogleUser(const std::string& name, const std::string& email, const std::string& googleId, const std::string& avatarUrl);

    std::optional<User> getUser(const std::string& email);
    std::optional<User> getUserById(std::string id);
    std::optional<User> getUserByGoogleId(const std::string& googleId);

    bool updateUserDetails(std::string userId, const std::string& name, const std::string& status);
    bool updateUserAvatar(std::string userId, const std::string& avatarUrl);
    bool updateUserStatus(const std::string& userId, const std::string& newStatus);
    bool updateLastSeen(const std::string& userId);

    bool deleteUser(std::string userId);
    bool banUser(std::string userId);
    bool loginUser(const std::string& email, const std::string& rawPassword);
    void markInactiveUsersOffline(int timeoutSeconds = 300); // Varsayılan 5 dakika

    // ==========================================
    // SUNUCU (SERVER) METOTLARI
    // ==========================================
    std::vector<Server> getAllServers(); // Admin UI için tüm sunucular
    std::string createServer(const std::string& name, std::string ownerId);
    bool updateServer(std::string serverId, const std::string& name, const std::string& iconUrl);
    bool deleteServer(std::string serverId);

    std::vector<Server> getUserServers(std::string userId);
    std::optional<Server> getServerDetails(std::string serverId);

    bool addMemberToServer(std::string serverId, std::string userId);
    bool removeMemberFromServer(std::string serverId, std::string userId);
    bool kickMember(std::string serverId, std::string userId);
    bool joinServerByCode(std::string userId, const std::string& inviteCode);
    std::vector<ServerMemberDetail> getServerMembersDetails(const std::string& serverId);

    // Sunucu Davet Sistemi
    bool sendServerInvite(std::string serverId, std::string inviterId, std::string inviteeId);
    std::vector<ServerInviteDTO> getPendingServerInvites(std::string userId);
    bool resolveServerInvite(std::string serverId, std::string inviteeId, bool accept);

    // Sunucu Rol Sistemi
    bool createRole(std::string serverId, std::string roleName, int hierarchy, int permissions);
    std::vector<Role> getServerRoles(std::string serverId);
    bool assignRole(std::string serverId, std::string userId, std::string roleId);

    // Sunucu Logları (Denetim Kaydı)
    bool logServerAction(const std::string& serverId, const std::string& action, const std::string& details);
    std::vector<ServerLog> getServerLogs(const std::string& serverId);

    // ==========================================
    // KANAL (CHANNEL) METOTLARI
    // ==========================================
    bool createChannel(std::string serverId, std::string name, int type);
    bool updateChannel(std::string channelId, const std::string& name);
    bool deleteChannel(std::string channelId);
    std::vector<Channel> getServerChannels(std::string serverId);

    std::string getChannelServerId(const std::string& channelId);
    std::string getChannelName(const std::string& channelId);
    int getServerKanbanCount(std::string serverId);

    // ==========================================
    // MESAJLAŞMA (CHAT) METOTLARI
    // ==========================================
    std::string getOrCreateDMChannel(std::string user1Id, std::string user2Id);
    bool sendMessage(std::string channelId, std::string senderId, const std::string& content, const std::string& attachmentUrl = "");
    bool updateMessage(std::string messageId, const std::string& newContent);
    bool deleteMessage(std::string messageId);
    std::vector<Message> getChannelMessages(std::string channelId, int limit = 50);

    // ==========================================
    // KANBAN (GÖREV YÖNETİMİ) METOTLARI
    // ==========================================
    std::vector<KanbanList> getKanbanBoard(std::string channelId);

    // Kanban Liste İşlemleri
    bool createKanbanList(std::string boardChannelId, std::string title);
    bool updateKanbanList(std::string listId, const std::string& title, int position);
    bool deleteKanbanList(std::string listId);

    // Kanban Kart İşlemleri
    bool createKanbanCard(std::string listId, std::string title, std::string desc, int priority);
    bool updateKanbanCard(std::string cardId, std::string title, std::string desc, int priority);
    bool deleteKanbanCard(std::string cardId);
    bool moveCard(std::string cardId, std::string newListId, int newPosition);

    // Kanban Bildirim Servisi (Arka Plan Görevi)
    void processKanbanNotifications();

    // ==========================================
    // BİLDİRİM (NOTIFICATION) METOTLARI
    // ==========================================
    std::vector<NotificationDTO> getUserNotifications(const std::string& userId);
    bool markNotificationAsRead(int notifId);

    // ==========================================
    // ARKADAŞLIK SİSTEMİ METOTLARI
    // ==========================================
    std::vector<User> searchUsers(const std::string& searchQuery);
    bool sendFriendRequest(std::string myId, std::string targetUserId);
    bool acceptFriendRequest(std::string requesterId, std::string myId);
    bool rejectOrRemoveFriend(std::string otherUserId, std::string myId);
    std::vector<FriendRequest> getPendingRequests(std::string myId);
    std::vector<User> getFriendsList(std::string myId);

    // ==========================================
    // ABONELİK VE ÖDEME METOTLARI
    // ==========================================
    bool isSubscriptionActive(std::string userId);
    int getUserServerCount(std::string userId);
    bool updateUserSubscription(std::string userId, int level, int durationDays);
    bool createPaymentRecord(std::string userId, const std::string& providerId, float amount, const std::string& currency);
    bool updatePaymentStatus(const std::string& providerId, const std::string& status);
    std::vector<PaymentTransaction> getUserPayments(std::string userId);

    // ==========================================
    // RAPORLAMA (ŞİKAYET) METOTLARI
    // ==========================================
    bool createReport(std::string reporterId, std::string contentId, const std::string& type, const std::string& reason);
    std::vector<UserReport> getOpenReports();
    bool resolveReport(std::string reportId);
};