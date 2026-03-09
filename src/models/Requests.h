#pragma once
#include <string>
#include <crow/json.h>

struct FriendRequest {
    std::string id;      // <-- EKSİKLER
    std::string name;    // <-- EKSİKLER
    std::string email;   // <-- EKSİKLER
    std::string type;    // <-- incoming veya outgoing

    crow::json::wvalue toJson() const {
        crow::json::wvalue j;
        j["id"] = id;
        j["name"] = name;
        j["email"] = email;
        j["type"] = type;
        return j;
    }
};