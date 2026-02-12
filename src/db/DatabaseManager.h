#pragma once
#include <string>
#include <vector>
#include <sqlite3.h>
#include <iostream>
#include <optional>

// --- MODELLERİ DAHİL ET (HATA ÇÖZÜMÜ BURADA) ---
// Struct tanımlarını sildik, yerine dosya yollarını ekledik.
#include "../models/User.h"
#include "../models/Server.h"
#include "../models/Message.h"
#include "../models/Kanban.h" 

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

    // --- ABONELİK & LİMİT ---
    bool isSubscriptionActive(int userId);
    int getUserServerCount(int userId);
    int getServerKanbanCount(int serverId);
    bool updateUserSubscription(int userId, int level, int durationDays);

    // --- KULLANICI ---
    bool createUser(const std::string& name, const std::string& email, const std::string& rawPassword, bool isAdmin = false);
    bool loginUser(const std::string& email, const std::string& rawPassword);
    std::optional<User> getUser(const std::string& email);
    std::optional<User> getUserById(int id);
    bool updateUserAvatar(int userId, const std::string& avatarUrl);

    // --- ARKADAŞLIK ---
    bool sendFriendRequest(int myId, int targetUserId);
    bool acceptFriendRequest(int requesterId, int myId);
    bool rejectOrRemoveFriend(int otherUserId, int myId);
    std::vector<FriendRequest> getPendingRequests(int myId);
    std::vector<User> getFriendsList(int myId);

    // --- SUNUCU YÖNETİMİ ---
    int createServer(const std::string& name, int ownerId);
    std::vector<Server> getUserServers(int userId);
    std::optional<Server> getServerDetails(int serverId);
    bool addMemberToServer(int serverId, int userId);

    // --- KANAL & ROL ---
    std::vector<Channel> getServerChannels(int serverId);
    bool createChannel(int serverId, std::string name, int type);
    bool createRole(int serverId, std::string roleName, int hierarchy, int permissions);

    // --- MESAJLAŞMA ---
    bool sendMessage(int channelId, int senderId, const std::string& content, const std::string& attachmentUrl = "");
    std::vector<Message> getChannelMessages(int channelId, int limit = 50);

    // --- KANBAN / TRELLO ---
    // DİKKAT: Dönüş tipi 'KanbanList' oldu (Eski adı KanbanListWithCards idi)
    std::vector<KanbanList> getKanbanBoard(int channelId);

    bool createKanbanList(int boardChannelId, std::string title);
    bool createKanbanCard(int listId, std::string title, std::string description, int priority);
    bool moveCard(int cardId, int newListId, int newPosition);
};