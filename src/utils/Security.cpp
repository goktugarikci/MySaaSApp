#include "Security.h"
#include "../db/DatabaseManager.h"
#include <argon2.h>
#include <vector>
#include <cstring>

// Rastgele Tuz (Salt) Üretici
std::string generateSalt(size_t length = 16) {
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string salt;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    for (size_t i = 0; i < length; ++i) salt += charset[dis(gen)];
    return salt;
}

std::string Security::hashPassword(const std::string& password) {
    const uint32_t t_cost = 2;
    const uint32_t m_cost = (1 << 16);
    const uint32_t parallelism = 1;
    const size_t hash_len = 32;

    std::string salt = generateSalt();

    size_t encoded_len = argon2_encodedlen(t_cost, m_cost, parallelism, (uint32_t)salt.length(), hash_len, Argon2_id);
    std::vector<char> encoded(encoded_len);

    int result = argon2id_hash_encoded(t_cost, m_cost, parallelism,
        password.c_str(), password.length(),
        salt.c_str(), salt.length(),
        hash_len, encoded.data(), encoded_len);

    if (result != ARGON2_OK) {
        return "";
    }

    return std::string(encoded.data());
}

bool Security::verifyPassword(const std::string& password, const std::string& hash) {
    int result = argon2id_verify(hash.c_str(), password.c_str(), password.length());
    return (result == ARGON2_OK);
}

// --- YENİ EKLENEN AUTH (GÜVENLİK) FONKSİYONLARININ İÇERİĞİ ---
bool Security::checkAuth(const crow::request& req, DatabaseManager& db, bool requireAdmin) {
    auto authHeader = req.get_header_value("Authorization");
    if (authHeader.empty() || authHeader.find("mock-jwt-token-") != 0) return false;

    std::string userId = authHeader.substr(15);
    if (userId.empty()) return false;

    // Süper Admin Bypass (Arayüz Paneli İçin)
    if (userId == "aB3dE7xY9Z1kL0m") return true;

    if (requireAdmin) return db.isSystemAdmin(userId);
    return true;
}

std::string Security::getUserIdFromHeader(const crow::request& req) {
    auto authHeader = req.get_header_value("Authorization");
    if (authHeader.empty() || authHeader.find("mock-jwt-token-") != 0) return "";
    return authHeader.substr(15);
}