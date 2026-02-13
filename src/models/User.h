#pragma once
#include <string>
#include "crow.h"

enum class UserRole {
    USER = 0,
    PREMIUM = 1,
    MODERATOR = 2,
    SYSTEM_ADMIN = 3
};

struct User {
    std::string id;
    std::string name;
    std::string email;
    std::string password_hash; // JSON'a gitmemeli (Güvenlik)
    bool is_system_admin;
    std::string status;
    std::string avatar_url;
    int subscription_level;
    std::string subscription_expires_at;
    std::string google_id; // Google OAuth ID'si

    // Kullanıcı nesnesini JSON formatına çevirir (API Cevabı için)
    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id;
        json["name"] = name;
        json["email"] = email;
        json["avatar_url"] = avatar_url;
        json["status"] = status;
        json["is_system_admin"] = is_system_admin;
        json["subscription_level"] = subscription_level;

        // Eğer google_id varsa onu da ekle (boşsa ekleme)
        if (!google_id.empty()) {
            json["google_id"] = google_id;
        }

        return json;
    }
};

