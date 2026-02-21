#pragma once
#include <string>
#include <crow.h> // JSON dönüşümü için eklendi

struct User {
    std::string id;
    std::string name;
    std::string email;
    std::string status;
    bool isSystemAdmin;
    std::string avatarUrl;
    std::string subscriptionLevel;
    int subscriptionLevelInt;
    std::string subscriptionExpiresAt;
    std::string googleId;

    // KIRMIZI ÇİZGİYİ ÇÖZEN FONKSİYON
    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id;
        json["name"] = name;
        json["email"] = email;
        json["status"] = status;
        json["is_system_admin"] = isSystemAdmin;
        json["avatar_url"] = avatarUrl;
        json["subscription_level"] = subscriptionLevel;
        json["subscription_expires_at"] = subscriptionExpiresAt;
        return json;
    }
};