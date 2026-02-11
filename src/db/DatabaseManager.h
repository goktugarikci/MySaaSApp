#pragma once
#include <string>
#include <vector>
#include <sqlite3.h>
#include <iostream>
#include <optional>

// --- VERİ MODELLERİ (STRUCTS) ---

struct User {
    int id;
    std::string name;
    std::string email;
    std::string password_hash; // Veritabanından çekmezsek boş string verelim
    bool is_system_admin;
    std::string status;
    std::string avatar_url;
};

// EKSİK OLAN STRUCT EKLENDİ:
struct FriendRequest {
    int requester_id;
    std::string requester_name;
    std::string requester_avatar;
    std::string sent_at;
};

struct Server {
    int id;
    int owner_id;
    std::string name;
    std::string invite_code;
};

struct Channel {
    int id;
    int server_id;
    std::string name;
    int type; // 0: Text, 1: Voice, 2: Video, 3: Kanban Board
    bool is_private;
};

struct KanbanCard {
    int id;
    int list_id;
    std::string title;
    std::string description;
    int priority;
    int position;
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

    // Kullanıcı İşlemleri
    bool createUser(const std::string& name, const std::string& email, const std::string& rawPassword, bool isAdmin = false);
    bool loginUser(const std::string& email, const std::string& rawPassword);
    std::optional<User> getUser(const std::string& email);
    std::optional<User> getUserById(int id);
    bool updateUserAvatar(int userId, const std::string& avatarUrl);

    // Arkadaşlık İşlemleri
    bool sendFriendRequest(int myId, int targetUserId);
    bool acceptFriendRequest(int requesterId, int myId);
    bool rejectOrRemoveFriend(int otherUserId, int myId); // EKSİK OLAN BU FONKSİYONDU
    std::vector<FriendRequest> getPendingRequests(int myId);
    std::vector<User> getFriendsList(int myId);

    // Sunucu & Diğerleri
    int createServer(const std::string& name, int ownerId);
    bool createChannel(int serverId, std::string name, int type);
    bool createKanbanList(int boardChannelId, std::string title);
    bool createKanbanCard(int listId, std::string title, std::string desc, int priority);
    bool moveCard(int cardId, int newListId, int newPosition);
};