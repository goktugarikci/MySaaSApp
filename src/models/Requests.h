#pragma once
#include <string>
#include "crow.h"

// Arkadaşlık İsteği Modeli
struct FriendRequest {
    std::string requester_id;
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

// Kayıt Olma İsteği (POST /api/auth/register)
struct RegisterRequest {
    std::string name;
    std::string email;
    std::string password;

    static RegisterRequest fromJson(const crow::json::rvalue& json) {
        RegisterRequest req;
        if (json.has("name")) req.name = json["name"].s();
        if (json.has("email")) req.email = json["email"].s();
        if (json.has("password")) req.password = json["password"].s();
        return req;
    }
};

// Giriş Yapma İsteği (POST /api/auth/login)
struct LoginRequest {
    std::string email;
    std::string password;

    static LoginRequest fromJson(const crow::json::rvalue& json) {
        LoginRequest req;
        if (json.has("email")) req.email = json["email"].s();
        if (json.has("password")) req.password = json["password"].s();
        return req;
    }
};

// Profil Güncelleme İsteği (PUT /api/users/me)
struct UpdateProfileRequest {
    std::string name;
    std::string status;

    static UpdateProfileRequest fromJson(const crow::json::rvalue& json) {
        UpdateProfileRequest req;
        if (json.has("name")) req.name = json["name"].s();
        if (json.has("status")) req.status = json["status"].s();
        return req;
    }
};

// Kanal Oluşturma İsteği
struct CreateChannelRequest {
    std::string name;
    int type;

    static CreateChannelRequest fromJson(const crow::json::rvalue& json) {
        CreateChannelRequest req;
        if (json.has("name")) req.name = json["name"].s();
        if (json.has("type")) req.type = json["type"].i();
        return req;
    }
};
