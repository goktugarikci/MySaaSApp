#pragma once
#include <string>
#include <crow/json.h>

struct FriendRequest {
    std::string id;
    std::string name;
    std::string email;
    std::string type; // "incoming" veya "outgoing"

    // JSON dönüşümü için kolaylık
    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id;
        json["name"] = name;
        json["email"] = email;
        json["type"] = type;
        return json;
    }
};