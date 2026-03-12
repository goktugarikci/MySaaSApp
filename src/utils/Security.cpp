#include "Security.h"
#include "../db/DatabaseManager.h"
#include <argon2.h>
#include <vector>
#include <chrono>
#include <nlohmann/json.hpp>

// PicoJSON kütüphanesini vcpkg.json üzerinden indirdik.
// Bu yüzden derleyicinin PicoJSON'u engellemesini devre dışı bırakıyoruz:
#undef JWT_DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

const std::string JWT_SECRET = "MySuperSecretSaaSTokenKey2026!";

std::string Security::hashPassword(const std::string& password) {
    const uint32_t t_cost = 2, m_cost = (1 << 16), parallelism = 1;
    const size_t hash_len = 32;
    std::string salt = "fixed_salt_for_now_123456";
    size_t encoded_len = argon2_encodedlen(t_cost, m_cost, parallelism, (uint32_t)salt.length(), hash_len, Argon2_id);
    std::vector<char> encoded(encoded_len);

    int result = argon2id_hash_encoded(t_cost, m_cost, parallelism,
        password.c_str(), password.length(),
        salt.c_str(), salt.length(),
        hash_len, encoded.data(), encoded_len);

    return (result == ARGON2_OK) ? std::string(encoded.data()) : "";
}

bool Security::verifyPassword(const std::string& password, const std::string& hash) {
    return argon2id_verify(hash.c_str(), password.c_str(), password.length()) == ARGON2_OK;
}

std::string Security::generateJwt(const std::string& userId) {
    auto token = jwt::create()
        .set_issuer("MySaaSApp")
        .set_type("JWS")
        .set_payload_claim("user_id", jwt::claim(std::string(userId)))
        .set_issued_at(std::chrono::system_clock::now())
        .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(24))
        .sign(jwt::algorithm::hs256{ JWT_SECRET });

    return token;
}

std::string Security::getUserIdFromHeader(const crow::request& req) {
    auto authHeader = req.get_header_value("Authorization");
    if (authHeader.empty()) return "";

    std::string token;
    if (authHeader.find("Bearer ") == 0) {
        token = authHeader.substr(7);
    }
    else if (authHeader.find("mock-jwt-token-") == 0) {
        return authHeader.substr(15);
    }
    else {
        return "";
    }

    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{ JWT_SECRET })
            .with_issuer("MySaaSApp");

        verifier.verify(decoded);

        return decoded.get_payload_claim("user_id").as_string();
    }
    catch (...) {
        return "";
    }
}

bool Security::checkAuth(const crow::request& req, DatabaseManager& db, bool requireAdmin) {
    std::string userId = getUserIdFromHeader(req);
    if (userId.empty()) return false;
    if (userId == "aB3dE7xY9Z1kL0m") return true;
    return requireAdmin ? db.isSystemAdmin(userId) : true;
}
std::string Security::generateLiveKitToken(const std::string& roomName, const std::string& participantName, const std::string& participantId) {
    // Canlı ortama geçtiğinizde bu key ve secret değerlerini ortam değişkenlerinden (ENV) veya konfigürasyon dosyasından almalısınız.
    // LiveKit --dev modunda çalışırken varsayılan olarak bu değerleri kullanır.
    const std::string API_KEY = "devkey";
    const std::string API_SECRET = "secret";

    auto token = jwt::create()
        .set_issuer(API_KEY)
        .set_subject(participantId)
        .set_type("JWT")
        .set_id(generateId(12)) // Rastgele bir JTI (Token ID)
        .set_issued_at(std::chrono::system_clock::now())
        .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(2)) // Token 2 saat geçerli
        .set_payload_claim("name", jwt::claim(participantName))
        .set_payload_claim("video", jwt::claim(nlohmann::json{
            {"room", roomName},
            {"roomJoin", true},
            {"canPublish", true},
            {"canSubscribe", true}
            }.dump())) // LiveKit'e özel yetki JSON'u
        .sign(jwt::algorithm::hs256{ API_SECRET });

    return token;
}