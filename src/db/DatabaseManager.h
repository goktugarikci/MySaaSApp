#pragma once
#include <string>
#include <vector>
#include <sqlite3.h>
#include <optional>

// MODELLERİ DAHİL ET
#include "../models/User.h"
#include "../models/Server.h"
#include "../models/Message.H"
#include "../models/Kanban.h"
#include "../models/Payment.h"
#include "../models/Requests.h"
#include "../models/DTOs.h" // Yeni taşıdığımız modeller

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

    // --- SİSTEM & LOGLAMA (ADMIN) ---
    SystemStats getSystemStats();
    bool isSystemAdmin(std::string userId);
    bool logSystemAction(const std::string& level, const std::string& action, const std::string& details);
    std::vector<SystemLogDTO> getSystemLogs(int limit = 100);
    std::vector<ArchivedMessageDTO> getArchivedMessages(int limit = 100);

    // --- KULLANICI & KİMLİK DOĞRULAMA ---
    bool createGoogleUser(const std::string& name, const std::string& email, const std::string& googleId, const std::string& avatarUrl);
    std::optional<User> getUserByGoogleId(const std::string& googleId);
    bool createUser(const std::string& name, const std::string& email, const std::string& rawPassword, bool isAdmin = false);
    bool loginUser(const std::string& email, const std::string& rawPassword);
    std::optional<User> getUser(const std::string& email);
    std::optional<User> getUserById(std::string id);
    bool updateUserAvatar(std::string userId, const std::string& avatarUrl);
    bool updateUserDetails(std::string userId, const std::string& name, const std::string& status);
    bool deleteUser(std::string userId);
    bool banUser(std::string userId);
    bool updateUserStatus(const std::string& userId, const std::string& newStatus);
    bool updateLastSeen(const std::string& userId);
    void markInactiveUsersOffline(int timeoutSeconds);
    std::vector<User> searchUsers(const std::string& searchQuery);
    std::string authenticateUser(const std::string& email, const std::string& password);
    std::vector<User> getAllUsers();

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
    bool logServerAction(const std::string& serverId, const std::string& action, const std::string& details);
    std::vector<ServerLog> getServerLogs(const std::string& serverId);
    std::vector<ServerMemberDetail> getServerMembersDetails(const std::string& serverId);
    std::vector<Server> getAllServers();

    // --- SUNUCU DAVET SİSTEMİ ---
    bool sendServerInvite(std::string serverId, std::string inviterId, std::string inviteeId);
    bool resolveServerInvite(std::string serverId, std::string inviteeId, bool accept);
    std::vector<ServerInviteDTO> getPendingServerInvites(std::string userId);

    // --- ROL VE YETKİLER ---
    bool createRole(std::string serverId, std::string roleName, int hierarchy, int permissions);
    std::vector<Role> getServerRoles(std::string serverId);
    bool assignRole(std::string serverId, std::string userId, std::string roleId);

    // --- KANAL YÖNETİMİ ---
    // [GÜNCELLEME] isPrivate parametresi eklendi
    bool createChannel(std::string serverId, std::string name, int type, bool isPrivate = false);
    bool updateChannel(std::string channelId, const std::string& name);
    bool deleteChannel(std::string channelId);
    // [GÜNCELLEME] Yetki kontrolü için userId opsiyonel parametresi eklendi
    std::vector<Channel> getServerChannels(std::string serverId, std::string userId = "");
    int getServerKanbanCount(std::string serverId);
    std::string getChannelServerId(const std::string& channelId);
    std::string getChannelName(const std::string& channelId);

    // [YENİ] Özel Kanal (Private Channel) Metotları
    bool addMemberToChannel(std::string channelId, std::string userId);
    bool removeMemberFromChannel(std::string channelId, std::string userId);
    bool hasChannelAccess(std::string channelId, std::string userId);
    std::vector<ServerMemberDetail> getChannelMembers(std::string channelId);

    // --- MESAJLAŞMA VE DM ---
    bool sendMessage(std::string channelId, std::string senderId, const std::string& content, const std::string& attachmentUrl = "");
    bool updateMessage(std::string messageId, const std::string& newContent);
    bool deleteMessage(std::string messageId);
    std::vector<Message> getChannelMessages(std::string channelId, int limit = 50);
    std::string getOrCreateDMChannel(std::string user1Id, std::string user2Id);

    // Gelişmiş Mesajlaşma (Tepkiler ve Alt Yanıtlar)
    bool addMessageReaction(std::string messageId, std::string userId, const std::string& emoji);
    bool removeMessageReaction(std::string messageId, std::string userId, const std::string& emoji);
    std::vector<ReactionDTO> getMessageReactions(std::string messageId);

    bool addThreadReply(std::string parentMessageId, std::string senderId, const std::string& content);
    std::vector<ThreadReplyDTO> getThreadReplies(std::string parentMessageId);

    // --- KANBAN SİSTEMİ ---
    std::vector<KanbanList> getKanbanBoard(std::string channelId);
    bool createKanbanList(std::string boardChannelId, std::string title);
    bool updateKanbanList(std::string listId, const std::string& title, int position);
    bool deleteKanbanList(std::string listId);
    bool createKanbanCard(std::string listId, std::string title, std::string desc, int priority);
    bool updateKanbanCard(std::string cardId, std::string title, std::string description, int priority);
    bool deleteKanbanCard(std::string cardId);
    bool moveCard(std::string cardId, std::string newListId, int newPosition);
    void processKanbanNotifications();

    // Gelişmiş Kanban (Yorumlar ve Etiketler)
    bool addCardComment(std::string cardId, std::string senderId, const std::string& content);
    std::vector<CardCommentDTO> getCardComments(std::string cardId);
    bool deleteCardComment(std::string commentId, std::string userId); // Sadece yazan veya admin silebilir

    bool addCardTag(std::string cardId, const std::string& tagName, const std::string& color);
    bool removeCardTag(std::string tagId);
    std::vector<CardTagDTO> getCardTags(std::string cardId);

    // --- ARKADAŞLIK SİSTEMİ ---
    bool sendFriendRequest(std::string myId, std::string targetUserId);
    bool acceptFriendRequest(std::string requesterId, std::string myId);
    bool rejectOrRemoveFriend(std::string otherUserId, std::string myId);
    std::vector<FriendRequest> getPendingRequests(std::string myId);
    std::vector<User> getFriendsList(std::string myId);

    // --- ÖDEME VE ABONELİK SİSTEMİ ---
    bool createPaymentRecord(std::string userId, const std::string& providerId, float amount, const std::string& currency);
    bool updatePaymentStatus(const std::string& providerId, const std::string& status);
    std::vector<PaymentTransaction> getUserPayments(std::string userId);
    bool isSubscriptionActive(std::string userId);
    int getUserServerCount(std::string userId);
    bool updateUserSubscription(std::string userId, int level, int durationDays);

    // --- RAPORLAMA VE BİLDİRİM DENETİMİ ---
    bool createReport(std::string reporterId, std::string contentId, const std::string& type, const std::string& reason);
    std::vector<UserReport> getOpenReports();
    bool resolveReport(std::string reportId);
    std::vector<NotificationDTO> getUserNotifications(const std::string& userId);
    bool markNotificationAsRead(int notifId);
};