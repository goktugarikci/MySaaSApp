    #pragma once
#include <string>
#include <random>

class Security {
public:
    // Şifreyi tuzlar ve hashler (Kayıt olurken kullanılır)
    // Örn Çıktı: $2a$12$R9h/cIPz0gi.URNNX3kh2OPST9/PgBkqquii.V3...
    static std::string hashPassword(const std::string& password);

    // Girilen şifrenin doğruluğunu kontrol eder (Giriş yaparken kullanılır)
    static bool verifyPassword(const std::string& password, const std::string& hash);


    // [YENİ] 15 Karakterlik Güvenli Rastgele ID Üretici
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
};