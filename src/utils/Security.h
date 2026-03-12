#pragma once
#include <string>
#include <random>
#include "crow.h"

class DatabaseManager;

class Security {
public:
    // --- Metin Şifreleme (Mesajlar için) ---
    static std::string encryptMessage(const std::string& plaintext);
    static std::string decryptMessage(const std::string& ciphertext);

    // --- Parola Şifreleme (Argon2) ---
    static std::string hashPassword(const std::string& password);
    static bool verifyPassword(const std::string& password, const std::string& hash);

    // --- Rastgele Benzersiz ID Üretici ---
    static std::string generateId(int length = 15) {
        const std::string chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        std::random_device rd;
        std::mt19937 generator(rd());
        std::uniform_int_distribution<> dist(0, (int)chars.size() - 1);
        std::string result;
        for (int i = 0; i < length; ++i) result += chars[dist(generator)];
        return result;
    }

    // ==========================================================
    // YENİ EKLENEN: Deterministik Hash Fonksiyonu
    // ==========================================================
    // Klasör isimlerinde gizlilik sağlamak için kullanılır (Örn: Chat_ID1_ID2 yerine Chat_Hash1_Hash2)
    static std::string hashString(const std::string& input);

    // --- JWT (Kimlik Doğrulama) ---
    static std::string generateJwt(const std::string& userId);
    static bool checkAuth(const crow::request& req, DatabaseManager& db, bool requireAdmin = false);
    static std::string getUserIdFromHeader(const crow::request& req);

    // --- WebRTC (Görüntülü/Sesli Arama) ---
    static std::string generateLiveKitToken(const std::string& roomName, const std::string& participantName, const std::string& participantId);
};