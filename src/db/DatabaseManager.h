#pragma once
#include <string>
#include <vector>
#include <sqlite3.h>
#include <optional>

// MODELLERİ DAHİL ET (DİKKAT: Bu dosyalar src/models/ klasöründe olmalı)
#include "../models/User.h"
#include "../models/Server.h"
#include "../models/Message.h"
#include "../models/Kanban.h"
#include "../models/Requests.h" // Eğer oluşturmadıysanız bu satırı silebilirsiniz ama önerilir.

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

    // --- GOOGLE AUTH & KULLANICI ---
    bool createGoogleUser(const std::string& name, const std::string& email, const std::string& googleId, const std::string& avatarUrl);
    std::optional<User> getUserByGoogleId(const std::string& googleId);

    bool createUser(const std::string& name, const std::string& email, const std::string& rawPassword, bool isAdmin = false);
    bool loginUser(const std::string& email, const std::string& rawPassword);
    std::optional<User> getUser(const std::string& email);
    std::optional<User> getUserById(int id);
    bool updateUserAvatar(int userId, const std::string& avatarUrl);
    bool updateUserDetails(int userId, const std::string& name, const std::string& status);
    bool deleteUser(int userId);

    // --- ABONELİK ---
    bool isSubscriptionActive(int userId);
    int getUserServerCount(int userId);
    bool updateUserSubscription(int userId, int level, int durationDays);

    // --- SUNUCU YÖNETİMİ ---
    int createServer(const std::string& name, int ownerId);
    bool updateServer(int serverId, const std::string& name, const std::string& iconUrl);
    bool deleteServer(int serverId);
    std::vector<Server> getUserServers(int userId);
    std::optional<Server> getServerDetails(int serverId);
    bool addMemberToServer(int serverId, int userId);
    bool removeMemberFromServer(int serverId, int userId);

    // --- KANAL YÖNETİMİ ---
    bool createChannel(int serverId, std::string name, int type);
    bool updateChannel(int channelId, const std::string& name);
    bool deleteChannel(int channelId);
    std::vector<Channel> getServerChannels(int serverId);
    int getServerKanbanCount(int serverId);

    // --- ROL YÖNETİMİ (HATA ALAN KISIM BURASIYDI) ---
    bool createRole(int serverId, std::string roleName, int hierarchy, int permissions);

    // --- MESAJLAŞMA ---
    bool sendMessage(int channelId, int senderId, const std::string& content, const std::string& attachmentUrl = "");
    bool updateMessage(int messageId, const std::string& newContent);
    bool deleteMessage(int messageId);
    std::vector<Message> getChannelMessages(int channelId, int limit = 50);

    // --- KANBAN / TRELLO ---
    // DİKKAT: 'KanbanListWithCards' yerine artık 'KanbanList' kullanıyoruz.
    std::vector<KanbanList> getKanbanBoard(int channelId);
    bool createKanbanList(int boardChannelId, std::string title);
    bool updateKanbanList(int listId, const std::string& title, int position);
    bool deleteKanbanList(int listId);

    bool createKanbanCard(int listId, std::string title, std::string desc, int priority);
    bool updateKanbanCard(int cardId, std::string title, std::string description, int priority);
    bool deleteKanbanCard(int cardId);
    bool moveCard(int cardId, int newListId, int newPosition);

    // --- ARKADAŞLIK ---
    bool sendFriendRequest(int myId, int targetUserId);
    bool acceptFriendRequest(int requesterId, int myId);
    bool rejectOrRemoveFriend(int otherUserId, int myId);
    std::vector<FriendRequest> getPendingRequests(int myId);
    std::vector<User> getFriendsList(int myId);
};