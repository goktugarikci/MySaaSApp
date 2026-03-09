#pragma once
#include <string>
#include <vector>
#include <sqlite3.h>
#include <optional>
#include <mutex>
#include <crow/middlewares/cors.h>

#include "../models/User.h"
#include "../models/Server.h"
#include "../models/Message.h"
#include "../models/Kanban.h"
#include "../models/Payment.h"
#include "../models/Requests.h"
#include "../models/DTOs.h"

class DatabaseManager {
private:
    // ==========================================================
    // OPTİMİZE EDİLMİŞ TEKİL VERİTABANI MOTORU
    // ==========================================================
    sqlite3* db;
    std::string db_path;
    std::mutex dbMutex; // Sistem kilitlenmelerini önleyen tek ana kilit

public:
    DatabaseManager(const std::string& path);
    ~DatabaseManager();

    sqlite3* getDb();
    bool open();
    void close();
    bool initTables();
    bool executeQuery(const std::string& sql);

    // ==========================================================
    // KULLANICI VE KİMLİK DOĞRULAMA
    // ==========================================================
    bool createGoogleUser(const std::string& name, const std::string& email, const std::string& googleId, const std::string& avatarUrl);
    std::optional<User> getUserByGoogleId(const std::string& googleId);
    bool createUser(std::string name, std::string email, std::string password, bool is_system_admin = false, std::string username = "", std::string phone_number = "");
    std::optional<User> getUser(const std::string& email);
    std::optional<User> getUserById(std::string id);
    std::vector<User> getAllUsers();
    std::vector<User> searchUsers(const std::string& searchQuery);

    std::string authenticateUser(const std::string& email, const std::string& password);
    bool loginUser(const std::string& email, const std::string& rawPassword);

    bool updateUserAvatar(std::string userId, const std::string& avatarUrl);
    bool updateUserDetails(std::string userId, const std::string& name, const std::string& status);
    bool updateUserStatus(const std::string& userId, const std::string& newStatus);
    bool updateUserSettings(const std::string& userId, const std::string& theme, bool emailNotifs);
    bool updateLastSeen(const std::string& userId);
    void markInactiveUsersOffline(int timeoutSeconds);
    bool deleteUser(std::string userId);

    // ==========================================================
    // SUNUCU (SERVER) VE KATEGORİ YÖNETİMİ
    // ==========================================================
    std::string createServer(const std::string& name, std::string ownerId);
    bool deleteServer(std::string serverId);
    bool deleteServer(const std::string& serverId, const std::string& ownerId);
    bool updateServerName(const std::string& serverId, const std::string& ownerId, const std::string& newName);

    std::vector<Server> getAllServers();
    std::vector<Server> getUserServers(std::string userId);
    std::optional<Server> getServerDetails(std::string serverId);
    int getUserServerCount(std::string userId);

    std::string getServerSettings(std::string serverId);
    bool updateServerSettings(std::string serverId, const std::string& settingsJson);

    struct ServerCategory { std::string id, server_id, name; int position = 0; };
    std::string createServerCategory(const std::string& serverId, const std::string& name, int position);
    std::vector<ServerCategory> getServerCategories(const std::string& serverId);

    // ==========================================================
    // SUNUCU ÜYELERİ VE DAVETLER
    // ==========================================================
    bool addMemberToServer(std::string serverId, std::string userId);
    bool removeMemberFromServer(std::string serverId, std::string userId);
    bool leaveServer(const std::string& serverId, const std::string& userId);
    bool kickMember(std::string serverId, std::string userId);
    bool kickMember(const std::string& serverId, const std::string& ownerId, const std::string& targetId);
    bool isUserInServer(std::string serverId, std::string userId);
    std::vector<ServerMemberDetail> getServerMembersDetails(const std::string& serverId);

    bool createServerInvite(const std::string& serverId, const std::string& inviterId, const std::string& code);
    bool sendServerInvite(std::string serverId, std::string inviterId, std::string inviteeId);
    bool resolveServerInvite(std::string serverId, std::string inviteeId, bool accept);
    std::vector<ServerInviteDTO> getPendingServerInvites(std::string userId);
    bool joinServerByInvite(const std::string& userId, const std::string& inviteCode);
    bool joinServerByCode(std::string userId, const std::string& inviteCode);

    // ==========================================================
    // ROL VE İZİN YÖNETİMİ
    // ==========================================================
    bool createRole(std::string serverId, std::string roleName, int hierarchy, int permissions);
    std::string createServerRole(const std::string& serverId, const std::string& name, const std::string& color, int permissions);
    std::vector<Role> getServerRoles(std::string serverId);
    std::string getServerIdByRoleId(std::string roleId);

    bool updateRole(std::string roleId, std::string name, int hierarchy, int permissions);
    bool updateServerRole(const std::string& roleId, const std::string& name, const std::string& color, int permissions);
    bool deleteRole(std::string roleId);
    bool deleteServerRole(const std::string& roleId);

    bool assignRole(std::string serverId, std::string userId, std::string roleId);
    bool assignRoleToMember(std::string serverId, std::string userId, std::string roleId);
    bool assignRoleToUser(const std::string& serverId, const std::string& userId, const std::string& roleId);
    bool removeRoleFromUser(const std::string& serverId, const std::string& userId, const std::string& roleId);
    bool hasServerPermission(std::string serverId, std::string userId, std::string permissionType);

    // ==========================================================
    // KANAL (CHANNEL) YÖNETİMİ
    // ==========================================================
    bool createChannel(std::string serverId, std::string name, int type);
    bool createChannel(std::string serverId, std::string name, int type, bool isPrivate);
    bool updateChannel(std::string channelId, const std::string& name);
    bool updateChannelName(const std::string& channelId, const std::string& newName);
    bool updateChannelPosition(const std::string& channelId, int newPosition);
    bool deleteChannel(std::string channelId);

    std::vector<Channel> getServerChannels(std::string serverId);
    std::vector<Channel> getServerChannels(std::string serverId, std::string userId);
    std::string getChannelServerId(const std::string& channelId);
    std::string getChannelName(const std::string& channelId);

    bool hasChannelAccess(std::string channelId, std::string userId);
    bool addMemberToChannel(std::string channelId, std::string userId);
    bool removeMemberFromChannel(std::string channelId, std::string userId);
    std::string getOrCreateDMChannel(std::string user1Id, std::string user2Id);

    // ==========================================================
    // YENİ NESİL HİBRİT MESAJLAŞMA (HIZLI ETKİLEŞİMLER)
    // ==========================================================
    // Mesaj içerikleri FileManager ile JSON'da tutulur. Burada sadece yan veriler var.
    bool saveFavoriteMessage(const std::string& userId, const std::string& messageId);
    bool removeSavedMessage(const std::string& userId, const std::string& messageId);
    std::vector<Message> getSavedMessages(const std::string& userId);

    bool toggleMessagePin(const std::string& messageId, bool isPinned);

    bool addMessageReaction(const std::string& messageId, const std::string& userId, const std::string& reaction);
    bool removeMessageReaction(const std::string& messageId, const std::string& userId, const std::string& reaction);

    bool addThreadReply(const std::string& messageId, const std::string& userId, const std::string& content);
    std::vector<Message> getThreadReplies(const std::string& messageId);
    bool setChannelReadCursor(const std::string& userId, const std::string& channelId, const std::string& messageId);
    bool clearChatForUser(std::string userId, std::string channelId);

    // ==========================================================
    // KANBAN VE GÖREV YÖNETİMİ (TRELLO+)
    // ==========================================================
    int getServerKanbanCount(std::string serverId);
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
    bool setCardDeadline(const std::string& cardId, const std::string& date);

    std::vector<CardComment> getCardComments(std::string cardId);
    bool addCardComment(std::string cardId, std::string userId, std::string content);
    bool deleteCardComment(std::string commentId, std::string userId);

    std::vector<CardTag> getCardTags(std::string cardId);
    bool addCardTag(std::string cardId, std::string tagName, std::string color);
    bool removeCardTag(std::string tagId);
    bool addCardLabel(const std::string& cardId, const std::string& text, const std::string& color);

    struct ChecklistItem { std::string id, card_id, content; bool is_completed = false; };
    struct CardActivity { std::string id, card_id, user_id, user_name, action, timestamp; };

    std::string addChecklistItem(const std::string& cardId, const std::string& content);
    bool toggleChecklistItem(const std::string& itemId, bool isCompleted);
    std::vector<ChecklistItem> getCardChecklist(const std::string& cardId);

    bool logCardActivity(const std::string& cardId, const std::string& userId, const std::string& action);
    std::vector<CardActivity> getCardActivity(const std::string& cardId);
    void processKanbanNotifications();

    // ==========================================================
    // ARKADAŞLIK VE BLOKLAMA
    // ==========================================================
    bool sendFriendRequest(std::string myId, std::string targetUserId);
    bool acceptFriendRequest(std::string requesterId, std::string myId);
    bool rejectOrRemoveFriend(std::string otherUserId, std::string myId);
    bool respondFriendRequest(const std::string& requesterId, const std::string& targetId, const std::string& status);
    bool removeFriend(const std::string& userId, const std::string& friendId);

    std::vector<FriendRequest> getPendingRequests(std::string myId);
    std::vector<User> getFriendsList(std::string myId);

    bool blockUser(std::string userId, std::string targetId);
    bool unblockUser(std::string userId, std::string targetId);
    std::vector<User> getBlockedUsers(std::string userId);

    bool addUserNote(const std::string& ownerId, const std::string& targetUserId, const std::string& note);
    std::string getUserNote(const std::string& ownerId, const std::string& targetUserId);

    // ==========================================================
    // GÜVENLİK, MODERASYON VE 2FA
    // ==========================================================
    bool isSystemAdmin(std::string userId);
    bool banUser(std::string userId, const std::string& reason = "Sistem Yasaklamasi");
    bool unbanUser(std::string userId);
    std::vector<BannedUserRecord> getBannedUsers();
    bool timeoutUser(const std::string& serverId, const std::string& userId, int durationMinutes);

    bool enable2FA(const std::string& userId, const std::string& secret);
    bool disable2FA(const std::string& userId);
    bool createPasswordResetToken(const std::string& email, const std::string& token);
    bool resetPasswordWithToken(const std::string& token, const std::string& newPassword);

    bool createReport(std::string reporterId, std::string contentId, const std::string& type, const std::string& reason);
    std::vector<UserReport> getOpenReports();
    bool resolveReport(const std::string& reportId);

    // ==========================================================
    // ÖDEME VE SAAS ABONELİKLERİ
    // ==========================================================
    bool createPaymentRecord(std::string userId, const std::string& providerId, float amount, const std::string& currency);
    bool updatePaymentStatus(const std::string& providerId, const std::string& status);
    std::vector<PaymentTransaction> getUserPayments(std::string userId);

    bool updateUserSubscription(std::string userId, int level, int durationDays);
    bool isSubscriptionActive(std::string userId);
    void checkAndRevertExpiredSubscriptions();
    bool cancelSubscription(const std::string& userId);

    // ==========================================================
    // SES KANALLARI VE WEBRTC (LIVEKIT)
    // ==========================================================
    struct VoiceMember { std::string user_id, user_name; bool is_muted, is_camera_on, is_screen_sharing = false; };

    bool joinVoiceChannel(const std::string& channelId, const std::string& userId);
    bool leaveVoiceChannel(const std::string& channelId, const std::string& userId);
    bool updateVoiceStatus(const std::string& channelId, const std::string& userId, bool isMuted, bool isCameraOn, bool isScreenSharing);
    std::vector<VoiceMember> getVoiceChannelMembers(const std::string& channelId);
    bool logCallQuality(const std::string& userId, const std::string& channelId, int latency, float packetLoss, const std::string& resolution);

    // ==========================================================
    // BİLDİRİMLER VE SİSTEM LOGLARI
    // ==========================================================
    bool createNotification(std::string userId, std::string type, std::string content, int priority = 0);
    std::vector<crow::json::wvalue> getUserNotifications(std::string userId);
    bool markNotificationAsRead(int notifId);

    bool logServerAction(const std::string& serverId, const std::string& action, const std::string& details);
    std::vector<ServerLog> getServerLogs(const std::string& serverId);

    struct AuditLogRecord { std::string id, user_id, action_type, target_id, details, created_at; };
    bool logAction(const std::string& userId, const std::string& actionType, const std::string& targetId, const std::string& details);
    std::vector<AuditLogRecord> getAuditLogs(int limit = 200);

    SystemStats getSystemStats();
};