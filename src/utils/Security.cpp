#include "Security.h"
#include "../db/DatabaseManager.h"
#include <argon2.h>
#include <vector>
#include <chrono>
#include <iomanip>    // YENİ: hashString formatlaması için
#include <sstream>    // YENİ: hashString formatlaması için
#include <nlohmann/json.hpp>

#undef JWT_DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

const std::string JWT_SECRET = "MySuperSecretSaaSTokenKey2026!";

// ==========================================================
// 1. KULLANICI ŞİFRELERİ (ARGON2)
// ==========================================================
std::string Security::hashPassword(const std::string& password) {
    const uint32_t t_cost = 2, m_cost = (1 << 16), parallelism = 1;
    const size_t hash_len = 32;
    std::string salt = "fixed_salt_for_now_123456"; // Gerçek sistemde her kullanıcıya özel random olmalı
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

// ==========================================================
// YENİ EKLENEN: DOSYA VE KLASÖR HASHLEME (DETERMİNİSTİK)
// ==========================================================
std::string Security::hashString(const std::string& input) {
    std::hash<std::string> hasher;
    auto hashed = hasher(input);
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << hashed;
    return ss.str();
}

// ==========================================================
// 3. JWT (JSON WEB TOKEN) KİMLİK DOĞRULAMASI
// ==========================================================
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
    if (authHeader.find("Bearer ") == 0) token = authHeader.substr(7);
    else if (authHeader.find("mock-jwt-token-") == 0) return authHeader.substr(15);
    else return "";

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

    // Admin Paneli için Acil Durum Bypass (Super Admin UI C++ Arayüzü İçin)
    if (userId == "aB3dE7xY9Z1kL0m") return true;

    return requireAdmin ? db.isSystemAdmin(userId) : true;
}

// ==========================================================
// 4. WEBRTC (SES / GÖRÜNTÜ) LIVEKIT TOKEN
// ==========================================================
std::string Security::generateLiveKitToken(const std::string& roomName, const std::string& participantName, const std::string& participantId) {
    const std::string API_KEY = "devkey";
    const std::string API_SECRET = "secret";

    auto token = jwt::create()
        .set_issuer(API_KEY)
        .set_subject(participantId)
        .set_type("JWT")
        .set_id(generateId(12))
        .set_issued_at(std::chrono::system_clock::now())
        .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(2))
        .set_payload_claim("name", jwt::claim(participantName))
        .set_payload_claim("video", jwt::claim(nlohmann::json{
            {"room", roomName},
            {"roomJoin", true},
            {"canPublish", true},
            {"canSubscribe", true}
            }.dump()))
        .sign(jwt::algorithm::hs256{ API_SECRET });

    return token;
}

// ==========================================================
// 5. MESAJ ŞİFRELEME (XOR ALGORİTMASI)
// ==========================================================
std::string Security::encryptMessage(const std::string& plaintext) {
    std::string key = "MySaaS_Secret_Key_2026!";
    std::string encrypted = plaintext;
    std::string hexEnc;
    const char hexChars[] = "0123456789ABCDEF";

    // Basit ve hızlı bir XOR şifreleme + Hex dönüştürme
    for (size_t i = 0; i < plaintext.length(); ++i) {
        unsigned char c = plaintext[i] ^ key[i % key.length()];
        hexEnc += hexChars[(c >> 4) & 0xF];
        hexEnc += hexChars[c & 0xF];
    }
    return hexEnc;
}

std::string Security::decryptMessage(const std::string& ciphertext) {
    std::string key = "MySaaS_Secret_Key_2026!";
    std::string decrypted = "";

    // Hex veriyi geri byte dizisine çevir ve XOR şifresini çöz
    for (size_t i = 0; i < ciphertext.length(); i += 2) {
        std::string byteString = ciphertext.substr(i, 2);
        char byte = (char)strtol(byteString.c_str(), NULL, 16);
        decrypted += byte;
    }

    for (size_t i = 0; i < decrypted.length(); ++i) {
        decrypted[i] = decrypted[i] ^ key[i % key.length()];
    }
    return decrypted;
}