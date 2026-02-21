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
#include "../models/DTOs.h" 
#include "../utils/Security.h"

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

    // ==========================================
    // KİMLİK DOĞRULAMA & KULLANICI
    // ==========================================
    bool createGoogleUser(const std::string& name, const std::string& email, const std::string& googleId, const std::string& avatarUrl);
    std::optional<User> getUserByGoogleId(const std::string& googleId);
    bool createUser(const std::string& name, const std::string& email, const std::string& rawPassword, bool isAdmin = false);
    std::optional<User> getUser(const std::string& email);
    std::optional<User> getUserById(std::string id);
    std::string authenticateUser(const std::string& email, const std::string& password);
    bool loginUser(const std::string& email, const std::string& rawPassword);

    bool updateUserAvatar(std::string userId, const std::string& avatarUrl);
    bool updateUserDetails(std::string userId, const std::string& name, const std::string& status);
    bool deleteUser(std::string userId);
    bool isSystemAdmin(std::string userId);
    bool updateUserStatus(const std::string& userId, const std::string& newStatus);
    bool updateLastSeen(const std::string& userId);
    void markInactiveUsersOffline(int timeoutSeconds);
    std::vector<User> searchUsers(const std::string& searchQuery);

    // ==========================================
    // SUNUCU VE ROL YÖNETİMİ
    // ==========================================
    std::string createServer(const std::string& name, std::string ownerId);
    bool updateServer(std::string serverId, const std::string& name, const std::string& iconUrl);
    bool deleteServer(std::string serverId);
    std::vector<Server> getUserServers(std::string userId);
    std::vector<Server> getAllServers();
    std::optional<Server> getServerDetails(std::string serverId);

    std::string getServerSettings(std::string serverId);
    bool updateServerSettings(std::string serverId, const std::string& settingsJson);
    bool hasServerPermission(std::string serverId, std::string userId, std::string permissionType);
    bool isUserInServer(std::string serverId, std::string userId);

    bool addMemberToServer(std::string serverId, std::string userId);
    bool removeMemberFromServer(std::string serverId, std::string userId);
    bool joinServerByCode(std::string userId, const std::string& inviteCode);
    bool kickMember(std::string serverId, std::string userId);
    std::vector<ServerMemberDetail> getServerMembersDetails(const std::string& serverId);

    bool sendServerInvite(std::string serverId, std::string inviterId, std::string inviteeId);
    bool resolveServerInvite(std::string serverId, std::string inviteeId, bool accept);
    std::vector<ServerInviteDTO> getPendingServerInvites(std::string userId);

    bool createRole(std::string serverId, std::string roleName, int hierarchy, int permissions);
    std::vector<Role> getServerRoles(std::string serverId);
    std::string getServerIdByRoleId(std::string roleId);
    bool updateRole(std::string roleId, std::string name, int hierarchy, int permissions);
    bool deleteRole(std::string roleId);
    bool assignRole(std::string serverId, std::string userId, std::string roleId);
    bool assignRoleToMember(std::string serverId, std::string userId, std::string roleId);

    // ==========================================
    // KANAL YÖNETİMİ
    // ==========================================
    bool createChannel(std::string serverId, std::string name, int type);
    bool createChannel(std::string serverId, std::string name, int type, bool isPrivate);
    bool updateChannel(std::string channelId, const std::string& name);
    bool deleteChannel(std::string channelId);
    std::vector<Channel> getServerChannels(std::string serverId);
    std::vector<Channel> getServerChannels(std::string serverId, std::string userId);
    int getServerKanbanCount(std::string serverId);

    bool hasChannelAccess(std::string channelId, std::string userId);
    bool addMemberToChannel(std::string channelId, std::string userId);
    bool removeMemberFromChannel(std::string channelId, std::string userId);
    std::string getChannelServerId(const std::string& channelId);
    std::string getChannelName(const std::string& channelId);

    // ==========================================
    // MESAJLAŞMA & TEPKİLER
    // ==========================================
    std::string getOrCreateDMChannel(std::string user1Id, std::string user2Id);
    bool sendMessage(std::string channelId, std::string senderId, const std::string& content, const std::string& attachmentUrl = "");
    bool updateMessage(std::string messageId, const std::string& newContent);
    bool deleteMessage(std::string messageId);
    std::vector<Message> getChannelMessages(std::string channelId, int limit = 50);

    bool addMessageReaction(std::string messageId, std::string userId, std::string reaction);
    bool removeMessageReaction(std::string messageId, std::string userId, std::string reaction);
    bool addThreadReply(std::string messageId, std::string senderId, std::string content);
    std::vector<Message> getThreadReplies(std::string messageId);

    // ==========================================
    // KANBAN SİSTEMİ
    // ==========================================
    std::vector<KanbanList> getKanbanBoard(std::string channelId);
    bool createKanbanList(std::string boardChannelId, std::string title);
    bool updateKanbanList(std::string listId, const std::string& title, int position);
    bool deleteKanbanList(std::string listId);

    bool createKanbanCard(std::string listId, std::string title, std::string desc, int priority);
    bool createKanbanCard(std::string listId, std::string title, std::string desc, int priority, std::string assigneeId, std::string attachmentUrl, std::string dueDate);
    bool updateKanbanCard(std::string cardId, std::string title, std::string description, int priority);
    bool deleteKanbanCard(std::string cardId);
    bool moveCard(std::string cardId, std::string newListId, int newPosition);
    std::string getServerIdByCardId(std::string cardId);
    bool assignUserToCard(std::string cardId, std::string assigneeId);
    bool updateCardCompletion(std::string cardId, bool isCompleted);

    std::vector<CardComment> getCardComments(std::string cardId);
    bool addCardComment(std::string cardId, std::string userId, std::string content);
    bool deleteCardComment(std::string commentId, std::string userId);

    std::vector<CardTag> getCardTags(std::string cardId);
    bool addCardTag(std::string cardId, std::string tagName, std::string color);
    bool removeCardTag(std::string tagId);

    // ==========================================
    // ARKADAŞLIK & ENGELLEME
    // ==========================================
    bool sendFriendRequest(std::string myId, std::string targetUserId);
    bool acceptFriendRequest(std::string requesterId, std::string myId);
    bool rejectOrRemoveFriend(std::string otherUserId, std::string myId);
    std::vector<FriendRequest> getPendingRequests(std::string myId);
    std::vector<User> getFriendsList(std::string myId);

    std::vector<User> getBlockedUsers(std::string userId);
    bool blockUser(std::string userId, std::string targetId);
    bool unblockUser(std::string userId, std::string targetId);

    // ==========================================
    // ÖDEMELER, RAPORLAR VE ABONELİKLER
    // ==========================================
    bool createPaymentRecord(std::string userId, const std::string& providerId, float amount, const std::string& currency);
    bool updatePaymentStatus(const std::string& providerId, const std::string& status);
    std::vector<PaymentTransaction> getUserPayments(std::string userId);

    bool createReport(std::string reporterId, std::string contentId, const std::string& type, const std::string& reason);

    std::vector<UserReport> getOpenReports();

    bool resolveReport(std::string reportId);

    bool isSubscriptionActive(std::string userId);
    int getUserServerCount(std::string userId);
    bool updateUserSubscription(std::string userId, int level, int durationDays);

    // ==========================================
    // SİSTEM LOGLARI VE BİLDİRİMLER
    // ==========================================
    SystemStats getSystemStats();
    bool logServerAction(const std::string& serverId, const std::string& action, const std::string& details);
    std::vector<ServerLog> getSystemLogs(int limit = 100);
    std::vector<ServerLog> getServerLogs(const std::string& serverId);
    std::vector<Message> getArchivedMessages(int limit = 100);
    bool banUser(std::string userId);

    void processKanbanNotifications();
    std::vector<NotificationDTO> getUserNotifications(const std::string& userId);
    bool markNotificationAsRead(int notifId);
    std::vector<User> getAllUsers();
};