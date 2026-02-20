#include "Security.h"
#include <argon2.h>
#include <jwt-cpp/jwt.h>
#include <random>
#include <vector>
#include <cstring>
#include <chrono>

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

    if (result != ARGON2_OK) return "";
    return std::string(encoded.data());
}

bool Security::verifyPassword(const std::string& password, const std::string& hash) {
    int result = argon2id_verify(hash.c_str(), password.c_str(), password.length());
    return (result == ARGON2_OK);
}

// --- YENİ: JWT İŞLEMLERİ ---

// Gerçek bir senaryoda bu gizli anahtar (secret key) .env dosyasından çekilmelidir!
const std::string JWT_SECRET = "MYSaaS_Cok_Gizli_Ve_Guvenli_Uretilmis_Anahtar_2026!";

std::string Security::generateJwt(const std::string& userId) {
    auto token = jwt::create()
        .set_issuer("MySaaSApp")
        .set_type("JWS")
        .set_payload_claim("user_id", jwt::claim(userId))
        .set_issued_at(std::chrono::system_clock::now())
        .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(24)) // Token 24 saat geçerli
        .sign(jwt::algorithm::hs256{ JWT_SECRET });

    return token;
}

bool Security::verifyJwt(const std::string& token, std::string& outUserId) {
    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{ JWT_SECRET })
            .with_issuer("MySaaSApp");

        verifier.verify(decoded); // Süresi dolmuşsa veya imza yanlışsa exception fırlatır

        outUserId = decoded.get_payload_claim("user_id").as_string();
        return true;
    }
    catch (const std::exception& e) {
        // Token manipüle edilmiş veya süresi (24 saat) dolmuş
        return false;
    }
}