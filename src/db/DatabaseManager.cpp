#include "DatabaseManager.h"
#include "../utils/Security.h" // Argon2 hashing için
#include <iostream>

DatabaseManager::DatabaseManager(const std::string& path) : db_path(path), db(nullptr) {}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::open() {
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc) {
        std::cerr << "DB Hatasi: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    // Foreign Key desteğini aç
    executeQuery("PRAGMA foreign_keys = ON;");
    return true;
}

void DatabaseManager::close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool DatabaseManager::executeQuery(const std::string& sql) {
    char* zErrMsg = 0;
    int rc = sqlite3_exec(db, sql.c_str(), 0, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL Exec Hatasi: " << zErrMsg << "\nSorgu: " << sql << std::endl;
        sqlite3_free(zErrMsg);
        return false;
    }
    return true;
}

// --- TABLO OLUŞTURMA (SCHEMA) ---
bool DatabaseManager::initTables() {
    std::string sql =
        "CREATE TABLE IF NOT EXISTS Users ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "Name TEXT NOT NULL, "
        "Email TEXT UNIQUE NOT NULL, "
        "PasswordHash TEXT NOT NULL, "
        "IsSystemAdmin INTEGER DEFAULT 0, "
        "Status TEXT DEFAULT 'Offline', "
        "AvatarURL TEXT, "
        "CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP);"

        "CREATE TABLE IF NOT EXISTS Servers ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "OwnerID INTEGER, "
        "Name TEXT NOT NULL, "
        "InviteCode TEXT UNIQUE, "
        "FOREIGN KEY(OwnerID) REFERENCES Users(ID));"

        "CREATE TABLE IF NOT EXISTS Channels ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "ServerID INTEGER, "
        "Name TEXT NOT NULL, "
        "Type INTEGER NOT NULL, " // 0:Text, 1:Voice, 2:Video, 3:Kanban
        "FOREIGN KEY(ServerID) REFERENCES Servers(ID) ON DELETE CASCADE);"

        // --- ARKADAŞLIK TABLOSU ---
        "CREATE TABLE IF NOT EXISTS Friends ("
        "RequesterID INTEGER, "
        "TargetID INTEGER, "
        "Status INTEGER DEFAULT 0, " // 0:Bekliyor, 1:Arkadaş
        "CreatedAt DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "PRIMARY KEY (RequesterID, TargetID), "
        "FOREIGN KEY(RequesterID) REFERENCES Users(ID), "
        "FOREIGN KEY(TargetID) REFERENCES Users(ID));"

        // --- KANBAN SİSTEMİ ---
        "CREATE TABLE IF NOT EXISTS KanbanLists ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "ChannelID INTEGER, "
        "Title TEXT, "
        "Position INTEGER, "
        "FOREIGN KEY(ChannelID) REFERENCES Channels(ID));"

        "CREATE TABLE IF NOT EXISTS KanbanCards ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "ListID INTEGER, "
        "Title TEXT, "
        "Description TEXT, "
        "Priority INTEGER, "
        "Position INTEGER, "
        "FOREIGN KEY(ListID) REFERENCES KanbanLists(ID));";

    return executeQuery(sql);
}

// --- GÜVENLİ KULLANICI İŞLEMLERİ ---

bool DatabaseManager::createUser(const std::string& name, const std::string& email, const std::string& rawPassword, bool isAdmin) {
    // 1. Şifreyi Argon2 ile Hashle
    std::string passwordHash = Security::hashPassword(rawPassword);
    if (passwordHash.empty()) return false;

    // 2. Prepared Statement ile Kaydet
    const char* sql = "INSERT INTO Users (Name, Email, PasswordHash, IsSystemAdmin) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, passwordHash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, isAdmin ? 1 : 0);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool DatabaseManager::loginUser(const std::string& email, const std::string& rawPassword) {
    const char* sql = "SELECT PasswordHash FROM Users WHERE Email = ?;";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_STATIC);

    bool loginSuccess = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* dbHash = sqlite3_column_text(stmt, 0);
        // Argon2 Doğrulaması
        if (Security::verifyPassword(rawPassword, reinterpret_cast<const char*>(dbHash))) {
            loginSuccess = true;
        }
    }
    sqlite3_finalize(stmt);
    return loginSuccess;
}

std::optional<User> DatabaseManager::getUser(const std::string& email) {
    const char* sql = "SELECT ID, Name, Email, Status, IsSystemAdmin FROM Users WHERE Email = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_STATIC);

    std::optional<User> user = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user = User{
            sqlite3_column_int(stmt, 0),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)),
            "", // Avatar URL şimdilik boş
            sqlite3_column_int(stmt, 4) != 0
        };
    }
    sqlite3_finalize(stmt);
    return user;
}

// --- ARKADAŞLIK SİSTEMİ FONKSİYONLARI ---

bool DatabaseManager::sendFriendRequest(int myId, int targetUserId) {
    // Kendine istek atamazsın
    if (myId == targetUserId) return false;

    // Status 0: Bekliyor
    std::string sql = "INSERT INTO Friends (RequesterID, TargetID, Status) VALUES (" +
        std::to_string(myId) + ", " + std::to_string(targetUserId) + ", 0);";
    return executeQuery(sql);
}

bool DatabaseManager::acceptFriendRequest(int requesterId, int myId) {
    // Sadece hedef kişi (ben) kabul edebilirim
    std::string sql = "UPDATE Friends SET Status = 1 WHERE RequesterID = " +
        std::to_string(requesterId) + " AND TargetID = " + std::to_string(myId) + ";";
    return executeQuery(sql);
}

// Bekleyen İstekleri Getir (Bana gelenler)
std::vector<FriendRequest> DatabaseManager::getPendingRequests(int myId) {
    std::vector<FriendRequest> requests;
    std::string sql = "SELECT U.ID, U.Name, F.CreatedAt FROM Users U "
        "JOIN Friends F ON U.ID = F.RequesterID "
        "WHERE F.TargetID = " + std::to_string(myId) + " AND F.Status = 0;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            requests.push_back({
                sqlite3_column_int(stmt, 0),
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
                "", // Avatar
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))
                });
        }
    }
    sqlite3_finalize(stmt);
    return requests;
}

// Arkadaş Listesini Getir (Karışık Sorgu)
std::vector<User> DatabaseManager::getFriendsList(int myId) {
    std::vector<User> friends;
    // Hem benim eklediklerim (Requester=Ben) hem beni ekleyenler (Target=Ben)
    // VE Durumu 1 olanlar.
    std::string sql = "SELECT U.ID, U.Name, U.Email, U.Status FROM Users U "
        "JOIN Friends F ON (U.ID = F.RequesterID OR U.ID = F.TargetID) "
        "WHERE (F.RequesterID = " + std::to_string(myId) + " OR F.TargetID = " + std::to_string(myId) + ") "
        "AND F.Status = 1 AND U.ID != " + std::to_string(myId) + ";";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            friends.push_back({
                sqlite3_column_int(stmt, 0),
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)),
                "",
                false
                });
        }
    }
    sqlite3_finalize(stmt);
    return friends;
}

// --- DİĞER FONKSİYONLAR (BASİT İMPLEMENTASYONLAR) ---

int DatabaseManager::createServer(const std::string& name, int ownerId) {
    std::string sql = "INSERT INTO Servers (Name, OwnerID, InviteCode) VALUES ('" + name + "', " + std::to_string(ownerId) + ", 'INV-" + name + "');";
    if (!executeQuery(sql)) return -1;
    return (int)sqlite3_last_insert_rowid(db);
}

bool DatabaseManager::createChannel(int serverId, std::string name, int type) {
    std::string sql = "INSERT INTO Channels (ServerID, Name, Type) VALUES (" + std::to_string(serverId) + ", '" + name + "', " + std::to_string(type) + ");";
    return executeQuery(sql);
}

// Kanban (Trello) İşlemleri
bool DatabaseManager::createKanbanList(int boardChannelId, std::string title) {
    std::string sql = "INSERT INTO KanbanLists (ChannelID, Title, Position) VALUES (" + std::to_string(boardChannelId) + ", '" + title + "', 0);";
    return executeQuery(sql);
}

bool DatabaseManager::createKanbanCard(int listId, std::string title, std::string desc, int priority) {
    std::string sql = "INSERT INTO KanbanCards (ListID, Title, Description, Priority, Position) VALUES (" +
        std::to_string(listId) + ", '" + title + "', '" + desc + "', " + std::to_string(priority) + ", 0);";
    return executeQuery(sql);
}

bool DatabaseManager::moveCard(int cardId, int newListId, int newPosition) {
    std::string sql = "UPDATE KanbanCards SET ListID=" + std::to_string(newListId) + ", Position=" + std::to_string(newPosition) + " WHERE ID=" + std::to_string(cardId) + ";";
    return executeQuery(sql);
}