#pragma once
#include <string>
#include <vector>
#include "crow.h"

struct Role {
    std::string id; std::string server_id; std::string name;
    std::string color; int hierarchy; int permissions;

    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id; json["name"] = name; json["color"] = color;
        json["hierarchy"] = hierarchy; json["permissions"] = permissions;
        return json;
    }
};

struct Channel {
    std::string id; std::string server_id; std::string name;
    int type; bool is_private;

    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id; json["server_id"] = server_id; json["name"] = name;
        json["type"] = type; json["is_private"] = is_private;
        return json;
    }
};

struct Server {
    std::string id;
    std::string owner_id; // Rotalardaki hata buradan kaynaklanıyordu
    std::string name;
    std::string invite_code;
    std::string icon_url;
    std::string created_at;
    int member_count;
    std::vector<int> member_ids;

    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id; json["owner_id"] = owner_id; json["name"] = name;
        json["invite_code"] = invite_code; json["icon_url"] = icon_url;
        json["created_at"] = created_at; json["member_count"] = member_count;
        return json;
    }
};