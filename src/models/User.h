#pragma once
#include <string>
#include <crow/json.h>

struct User {
    std::string id;
    std::string name;
    std::string email;
    std::string password; // <-- HATA VEREN EKSİK BUYDU! (VEYA passwordHash)
    std::string status;
    bool isSystemAdmin = false;
    std::string avatarUrl;
    std::string subscriptionLevel;
    int subscriptionLevelInt = 0;
    std::string subscriptionExpiresAt;
    std::string googleId;
    std::string username = "";
    std::string phone_number = "";

    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id;
        json["name"] = name;
        json["email"] = email;
        // Güvenlik: password JSON'a eklenmez
        json["status"] = status;
        json["is_system_admin"] = isSystemAdmin;
        json["avatar_url"] = avatarUrl;
        json["username"] = username;
        return json;
    }
};