#include "Security.h"
#include <argon2.h>
#include <random>
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
    // Argon2 Ayarları (Güvenli Standartlar)
    const uint32_t t_cost = 2;         // İşlem tekrarı (Zaman maliyeti)
    const uint32_t m_cost = (1 << 16); // Bellek kullanımı (64 MB)
    const uint32_t parallelism = 1;    // Thread sayısı
    const size_t hash_len = 32;

    // Rastgele tuz oluştur
    std::string salt = generateSalt();

    // Hashlenmiş çıktıyı tutacak buffer
    size_t encoded_len = argon2_encodedlen(t_cost, m_cost, parallelism, (uint32_t)salt.length(), hash_len, Argon2_id);
    std::vector<char> encoded(encoded_len);

    int result = argon2id_hash_encoded(t_cost, m_cost, parallelism,
        password.c_str(), password.length(),
        salt.c_str(), salt.length(),
        hash_len, encoded.data(), encoded_len);

    if (result != ARGON2_OK) {
        return ""; // Hata durumu
    }

    return std::string(encoded.data());
}

bool Security::verifyPassword(const std::string& password, const std::string& hash) {
    // Şifreyi doğrula
    int result = argon2id_verify(hash.c_str(), password.c_str(), password.length());
    return (result == ARGON2_OK);
}