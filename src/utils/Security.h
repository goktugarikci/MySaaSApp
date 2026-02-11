#pragma once
#include <string>

class Security {
public:
    // Şifreyi tuzlar ve hashler (Kayıt olurken kullanılır)
    // Örn Çıktı: $2a$12$R9h/cIPz0gi.URNNX3kh2OPST9/PgBkqquii.V3...
    static std::string hashPassword(const std::string& password);

    // Girilen şifrenin doğruluğunu kontrol eder (Giriş yaparken kullanılır)
    static bool verifyPassword(const std::string& password, const std::string& hash);
};