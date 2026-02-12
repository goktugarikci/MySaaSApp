#pragma once
#include <string>
#include "crow.h"

struct User {
    int id;
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

// Arkadaşlık İsteği Modeli
struct FriendRequest {
    int requester_id;
    std::string requester_name;
    std::string requester_avatar;
    std::string sent_at;

    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["requester_id"] = requester_id;
        json["requester_name"] = requester_name;
        json["requester_avatar"] = requester_avatar;
        json["sent_at"] = sent_at;
        return json;
    }
};