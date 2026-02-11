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
    std::string password_hash; // Güvenlik için hash saklanmalı
    bool is_system_admin;      // "God Mode" yetkisi
    std::string status;        // Online, Offline
    std::string avatar_url;
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

struct Role {
    int id;
    int server_id;
    std::string name;
    std::string color;
    int hierarchy;  // 100: Admin, 1: Üye
    int permissions; // Bitmask (Örn: 8 = Ban yetkisi)
};

struct KanbanCard {
    int id;
    int list_id;
    std::string title;
    std::string description;
    int priority; // 0: Düşük, 1: Orta, 2: Yüksek
    int position; // Sıralama için
};

// --- DATABASE MANAGER SINIFI ---

class DatabaseManager {
private:
    sqlite3* db;
    std::string db_path;

    // Yardımcı: Basit sorgular için (Tablo oluşturma vb.)
    bool executeQuery(const std::string& sql);

public:
    DatabaseManager(const std::string& path);
    ~DatabaseManager();

    // --- BAĞLANTI VE KURULUM ---
    bool open();
    void close();
    bool initTables(); // Tüm tablo şemasını (Friends dahil) kurar

    // --- GÜVENLİ KULLANICI İŞLEMLERİ (Argon2 & Prepared Stmt) ---
    bool createUser(const std::string& name, const std::string& email, const std::string& rawPassword, bool isAdmin = false);
    bool loginUser(const std::string& email, const std::string& rawPassword); // Giriş kontrolü
    std::optional<User> getUser(const std::string& email);
    std::optional<User> getUserById(int id);

    // --- ARKADAŞLIK SİSTEMİ (YENİ) ---
    bool sendFriendRequest(int myId, int targetUserId);
    bool acceptFriendRequest(int requesterId, int myId);
    bool rejectOrRemoveFriend(int otherUserId, int myId);
    std::vector<FriendRequest> getPendingRequests(int myId); // Bana gelen istekler
    std::vector<User> getFriendsList(int myId);              // Arkadaş listem

    // --- SUNUCU & KANAL İŞLEMLERİ ---
    int createServer(const std::string& name, int ownerId);
    bool createChannel(int serverId, std::string name, int type);

    // --- KANBAN / TRELLO İŞLEMLERİ ---
    bool createKanbanList(int boardChannelId, std::string title);
    bool createKanbanCard(int listId, std::string title, std::string desc, int priority);
    bool moveCard(int cardId, int newListId, int newPosition);
};