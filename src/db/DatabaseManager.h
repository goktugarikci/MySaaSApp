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
// Hata Çözümü: Bu yapıların sınıf dışında olması .cpp dosyasındaki bildirim çakışmalarını önler.

struct UserReport {
    int id;
    int reporter_id;
    int content_id;
    std::string type; // 'MESSAGE', 'USER'
    std::string reason;
    std::string status; // 'OPEN', 'RESOLVED'
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

    // Temel İşlemler
    bool open();
    void close();
    bool initTables();

    // --- KULLANICI & KİMLİK DOĞRULAMA (AUTH) ---
    bool createGoogleUser(const std::string& name, const std::string& email, const std::string& googleId, const std::string& avatarUrl);
    std::optional<User> getUserByGoogleId(const std::string& googleId);
    bool createUser(const std::string& name, const std::string& email, const std::string& rawPassword, bool isAdmin = false);
    bool loginUser(const std::string& email, const std::string& rawPassword);
    std::optional<User> getUser(const std::string& email);
    std::optional<User> getUserById(int id);
    bool updateUserAvatar(int userId, const std::string& avatarUrl);
    bool updateUserDetails(int userId, const std::string& name, const std::string& status);
    bool deleteUser(int userId);
    bool isSystemAdmin(int userId);

    // --- SUNUCU YÖNETİMİ ---
    int createServer(const std::string& name, int ownerId);
    bool updateServer(int serverId, const std::string& name, const std::string& iconUrl);
    bool deleteServer(int serverId);
    std::vector<Server> getUserServers(int userId);
    std::optional<Server> getServerDetails(int serverId);
    bool addMemberToServer(int serverId, int userId);
    bool removeMemberFromServer(int serverId, int userId);
    bool joinServerByCode(int userId, const std::string& inviteCode);
    bool kickMember(int serverId, int userId);

    // --- ROL VE YETKİLER ---
    bool createRole(int serverId, std::string roleName, int hierarchy, int permissions);
    std::vector<Role> getServerRoles(int serverId);
    bool assignRole(int serverId, int userId, int roleId);

    // --- KANAL YÖNETİMİ ---
    bool createChannel(int serverId, std::string name, int type); // 0:Text, 1:Voice, 2:Video, 3:Kanban
    bool updateChannel(int channelId, const std::string& name);
    bool deleteChannel(int channelId);
    std::vector<Channel> getServerChannels(int serverId);
    int getServerKanbanCount(int serverId);

    // --- MESAJLAŞMA VE DM ---
    bool sendMessage(int channelId, int senderId, const std::string& content, const std::string& attachmentUrl = "");
    bool updateMessage(int messageId, const std::string& newContent);
    bool deleteMessage(int messageId);
    std::vector<Message> getChannelMessages(int channelId, int limit = 50);
    int getOrCreateDMChannel(int user1Id, int user2Id); // Birebir Sohbet

    // --- KANBAN SİSTEMİ ---
    // Hata Çözümü: KanbanListWithCards ismi KanbanList olarak güncellendi.
    std::vector<KanbanList> getKanbanBoard(int channelId);
    bool createKanbanList(int boardChannelId, std::string title);
    bool updateKanbanList(int listId, const std::string& title, int position);
    bool deleteKanbanList(int listId);
    bool createKanbanCard(int listId, std::string title, std::string desc, int priority);
    bool updateKanbanCard(int cardId, std::string title, std::string description, int priority);
    bool deleteKanbanCard(int cardId);
    bool moveCard(int cardId, int newListId, int newPosition);

    // --- ARKADAŞLIK SİSTEMİ ---
    bool sendFriendRequest(int myId, int targetUserId);
    bool acceptFriendRequest(int requesterId, int myId);
    bool rejectOrRemoveFriend(int otherUserId, int myId);
    std::vector<FriendRequest> getPendingRequests(int myId);
    std::vector<User> getFriendsList(int myId);

    // --- ÖDEME SİSTEMİ ---
    bool createPaymentRecord(int userId, const std::string& providerId, float amount, const std::string& currency);
    bool updatePaymentStatus(const std::string& providerId, const std::string& status);
    std::vector<PaymentTransaction> getUserPayments(int userId);

    // --- RAPORLAMA VE DENETİM ---
    bool createReport(int reporterId, int contentId, const std::string& type, const std::string& reason);
    std::vector<UserReport> getOpenReports();
    bool resolveReport(int reportId);

    // --- YÖNETİCİ VE ABONELİK YARDIMCILARI ---
    SystemStats getSystemStats(); // Admin istatistikleri
    std::vector<User> getAllUsers();
    bool banUser(int userId);
    bool isSubscriptionActive(int userId);
    int getUserServerCount(int userId);
    bool updateUserSubscription(int userId, int level, int durationDays);
};