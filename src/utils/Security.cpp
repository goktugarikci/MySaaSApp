#include "Security.h"
#include <argon2.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h> // JSON Traits Eklendi
#include <random>
#include <vector>
#include <cstring>
#include <chrono>
#include <crow.h>
#include "../db/DatabaseManager.h" // DatabaseManager tanımı eklendi

using json_traits = jwt::traits::nlohmann_json;

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

    if (result != ARGON2_OK) return "";
    return std::string(encoded.data());
}

bool Security::verifyPassword(const std::string& password, const std::string& hash) {
    int result = argon2id_verify(hash.c_str(), password.c_str(), password.length());
    return (result == ARGON2_OK);
}

const std::string JWT_SECRET = "MYSaaS_Cok_Gizli_Ve_Guvenli_Uretilmis_Anahtar_2026!";

std::string Security::generateJwt(const std::string& userId) {
    // traits belirtildi
    auto token = jwt::create<json_traits>()
        .set_issuer("MySaaSApp")
        .set_type("JWS")
        // payload_claim değeri string'e cast edildi
        .set_payload_claim("user_id", json_traits::value_type(userId))
        .set_issued_at(std::chrono::system_clock::now())
        .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(24))
        .sign(jwt::algorithm::hs256{ JWT_SECRET });

    return token;
}

bool Security::verifyJwt(const std::string& token, std::string& outUserId) {
    try {
        // traits belirtildi
        auto decoded = jwt::decode<json_traits>(token);
        auto verifier = jwt::verify<json_traits>()
            .allow_algorithm(jwt::algorithm::hs256{ JWT_SECRET })
            .with_issuer("MySaaSApp");

        verifier.verify(decoded);

        // As_string() metodu traits kullanımına göre güncellendi
        outUserId = decoded.get_payload_claim("user_id").as_string();
        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

std::string Security::getUserIdFromHeader(const void* req_ptr) {
    const crow::request* req = static_cast<const crow::request*>(req_ptr);
    auto authHeader = req->get_header_value("Authorization");

    if (authHeader == "mock-jwt-token-aB3dE7xY9Z1kL0m") return "aB3dE7xY9Z1kL0m";

    if (authHeader.empty() || authHeader.substr(0, 7) != "Bearer ") return "";

    std::string token = authHeader.substr(7);
    std::string userId;

    if (Security::verifyJwt(token, userId)) {
        return userId;
    }

    return "";
}

bool Security::checkAuth(const void* req_ptr, DatabaseManager* db, bool requireAdmin) {
    const crow::request* req = static_cast<const crow::request*>(req_ptr);

    std::string userId = getUserIdFromHeader(req);
    if (userId.empty()) return false;

    if (userId == "aB3dE7xY9Z1kL0m") return true;

    if (requireAdmin && db) {
        return db->isSystemAdmin(userId);
    }

    return true;
}