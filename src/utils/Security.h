#pragma once
#include <string>
#include <random>
#include "crow.h"

// Döngüsel bağımlılığı önlemek için ön tanımlama
class DatabaseManager;

class Security {
public:
    static std::string hashPassword(const std::string& password);
    static bool verifyPassword(const std::string& password, const std::string& hash);

    static std::string generateId(int length = 15) {
        const std::string chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        std::random_device rd;
        std::mt19937 generator(rd());
        std::uniform_int_distribution<> dist(0, (int)chars.size() - 1);
        std::string result;
        for (int i = 0; i < length; ++i) result += chars[dist(generator)];
        return result;
    }

    // --- YENİ AUTH METOTLARI ---
    static bool checkAuth(const crow::request& req, DatabaseManager& db, bool requireAdmin = false);
    static std::string getUserIdFromHeader(const crow::request& req);
};