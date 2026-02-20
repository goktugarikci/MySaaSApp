#pragma once
#include <string>
#include <random>

class DatabaseManager; // Forward declaration

class Security {
public:
    static std::string hashPassword(const std::string& password);
    static bool verifyPassword(const std::string& password, const std::string& hash);

    static std::string generateId(int length = 15) {
        const std::string chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        std::random_device rd;
        std::mt19937 generator(rd());
        std::uniform_int_distribution<> dist(0, chars.size() - 1);
        std::string result;
        for (int i = 0; i < length; ++i) {
            result += chars[dist(generator)];
        }
        return result;
    }

    static std::string generateJwt(const std::string& userId);
    static bool verifyJwt(const std::string& token, std::string& outUserId);

    // Crow request nesnesini alıp JWT doğrulaması yapan yardımcı metotlar
    static std::string getUserIdFromHeader(const void* req_ptr);
    static bool checkAuth(const void* req_ptr, DatabaseManager* db, bool requireAdmin = false);
};