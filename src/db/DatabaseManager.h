#pragma once
#include <string>
#include <vector>
#include <sqlite3.h>
#include <optional>

// MODELLERİ DAHİL ET
#include "../models/User.h"
#include "../models/Server.h"
#include "../models/Message.h"
#include "../models/Kanban.h"
#include "../models/Payment.h"
#include "../models/Requests.h"

// --- YARDIMCI YAPILAR (Global Scope) ---
struct UserReport {
    std::string id;
    std::string reporter_id;
    std::string content_id;
    std::string type;
    std::string reason;
    std::string status;
};

struct SystemStats {
    int user_count;
    int server_count;
    int message_count;
};

// --- DATABASE MANAGER SINIFI ---
class DatabaseManager {
private:
    sqlite3* db;
    std::string db_path;
    bool executeQuery(const std::string& sql);

public:
    DatabaseManager(const std::string& path);
    ~DatabaseManager();

    bool open();
    void close();
    bool initTables();

    // --- KULLANICI & KİMLİK DOĞRULAMA (AUTH) ---
    bool createGoogleUser(const std::string& name, const std::string& email, const std::string& googleId, const std::string& avatarUrl);
    std::optional<User> getUserByGoogleId(const std::string& googleId);
    bool createUser(const std::string& name, const std::string& email, const std::string& rawPassword, bool isAdmin = false);
    bool loginUser(const std::string& email, const std::string& rawPassword);
    std::optional<User> getUser(const std::string& email);
    std::optional<User> getUserById(std::string id);
    bool updateUserAvatar(std::string userId, const std::string& avatarUrl);
    bool updateUserDetails(std::string userId, const std::string& name, const std::string& status);
    bool deleteUser(std::string userId);
    bool isSystemAdmin(std::string userId);
    bool updateUserStatus(const std::string& userId, const std::string& newStatus);

    bool updateLastSeen(const std::string& userId);
    void markInactiveUsersOffline(int timeoutSeconds);
    std::vector<User> searchUsers(const std::string& searchQuery);

    // Kullanıcı girişini doğrular, başarılıysa ID'yi döndürür, başarısızsa boş string döndürür.
    std::string authenticateUser(const std::string& email, const std::string& password);

    // --- SUNUCU YÖNETİMİ ---
    std::string createServer(const std::string& name, std::string ownerId);
    bool updateServer(std::string serverId, const std::string& name, const std::string& iconUrl);
    bool deleteServer(std::string serverId);
    std::vector<Server> getUserServers(std::string userId);
    std::optional<Server> getServerDetails(std::string serverId);
    bool addMemberToServer(std::string serverId, std::string userId);
    bool removeMemberFromServer(std::string serverId, std::string userId);
    bool joinServerByCode(std::string userId, const std::string& inviteCode);
    bool kickMember(std::string serverId, std::string userId);

    // --- ROL VE YETKİLER ---
    bool createRole(std::string serverId, std::string roleName, int hierarchy, int permissions);
    std::vector<Role> getServerRoles(std::string serverId);
    bool assignRole(std::string serverId, std::string userId, std::string roleId);

    // --- KANAL YÖNETİMİ ---
    bool createChannel(std::string serverId, std::string name, int type);
    bool updateChannel(std::string channelId, const std::string& name);
    bool deleteChannel(std::string channelId);
    std::vector<Channel> getServerChannels(std::string serverId);
    int getServerKanbanCount(std::string serverId);

    // --- MESAJLAŞMA VE DM ---
    bool sendMessage(std::string channelId, std::string senderId, const std::string& content, const std::string& attachmentUrl = "");
    bool updateMessage(std::string messageId, const std::string& newContent);
    bool deleteMessage(std::string messageId);
    std::vector<Message> getChannelMessages(std::string channelId, int limit = 50);
    std::string getOrCreateDMChannel(std::string user1Id, std::string user2Id);

    // --- KANBAN SİSTEMİ ---
    std::vector<KanbanList> getKanbanBoard(std::string channelId);
    bool createKanbanList(std::string boardChannelId, std::string title);
    bool updateKanbanList(std::string listId, const std::string& title, int position);
    bool deleteKanbanList(std::string listId);
    bool createKanbanCard(std::string listId, std::string title, std::string desc, int priority);
    bool updateKanbanCard(std::string cardId, std::string title, std::string description, int priority);
    bool deleteKanbanCard(std::string cardId);
    bool moveCard(std::string cardId, std::string newListId, int newPosition);

    // --- ARKADAŞLIK SİSTEMİ ---
    bool sendFriendRequest(std::string myId, std::string targetUserId);
    bool acceptFriendRequest(std::string requesterId, std::string myId);
    bool rejectOrRemoveFriend(std::string otherUserId, std::string myId);
    std::vector<FriendRequest> getPendingRequests(std::string myId);
    std::vector<User> getFriendsList(std::string myId);

    // --- ÖDEME SİSTEMİ ---
    bool createPaymentRecord(std::string userId, const std::string& providerId, float amount, const std::string& currency);
    bool updatePaymentStatus(const std::string& providerId, const std::string& status);
    std::vector<PaymentTransaction> getUserPayments(std::string userId);

    // --- RAPORLAMA VE DENETİM ---
    bool createReport(std::string reporterId, std::string contentId, const std::string& type, const std::string& reason);
    std::vector<UserReport> getOpenReports();
    bool resolveReport(std::string reportId);

    // --- YÖNETİCİ VE ABONELİK YARDIMCILARI ---
    SystemStats getSystemStats();
    std::vector<User> getAllUsers();
    bool banUser(std::string userId);
    bool isSubscriptionActive(std::string userId);
    int getUserServerCount(std::string userId);
    bool updateUserSubscription(std::string userId, int level, int durationDays);
};