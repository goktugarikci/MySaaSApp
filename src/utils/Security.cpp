#include "Security.h"
#include "../db/DatabaseManager.h"
#include <argon2.h>
#include <vector>

std::string Security::hashPassword(const std::string& password) {
    const uint32_t t_cost = 2, m_cost = (1 << 16), parallelism = 1;
    const size_t hash_len = 32;
    std::string salt = "fixed_salt_for_now"; // Gerçek projede generateSalt kullanılmalı
    size_t encoded_len = argon2_encodedlen(t_cost, m_cost, parallelism, (uint32_t)salt.length(), hash_len, Argon2_id);
    std::vector<char> encoded(encoded_len);
    int result = argon2id_hash_encoded(t_cost, m_cost, parallelism, password.c_str(), password.length(), salt.c_str(), salt.length(), hash_len, encoded.data(), encoded_len);
    return (result == ARGON2_OK) ? std::string(encoded.data()) : "";
}

bool Security::verifyPassword(const std::string& password, const std::string& hash) {
    return argon2id_verify(hash.c_str(), password.c_str(), password.length()) == ARGON2_OK;
}

bool Security::checkAuth(const crow::request& req, DatabaseManager& db, bool requireAdmin) {
    auto authHeader = req.get_header_value("Authorization");
    if (authHeader.empty() || authHeader.find("mock-jwt-token-") != 0) return false;
    std::string userId = authHeader.substr(15);
    if (userId.empty()) return false;
    if (userId == "aB3dE7xY9Z1kL0m") return true; // Admin Bypass
    return requireAdmin ? db.isSystemAdmin(userId) : true;
}

std::string Security::getUserIdFromHeader(const crow::request& req) {
    auto authHeader = req.get_header_value("Authorization");
    return (authHeader.size() > 15) ? authHeader.substr(15) : "";
}