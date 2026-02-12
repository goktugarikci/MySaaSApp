#pragma once
#include <string>
#include <vector>
#include "crow.h"

struct Role {
    int id;
    int server_id;
    std::string name;
    std::string color;
    int hierarchy;
    int permissions;

    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id;
        json["name"] = name;
        json["color"] = color;
        json["hierarchy"] = hierarchy;
        json["permissions"] = permissions;
        return json;
    }
};

struct Channel {
    int id;
    int server_id;
    std::string name;
    int type; // 0: Text, 1: Voice, 2: Video, 3: Kanban
    bool is_private;

    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id;
        json["server_id"] = server_id;
        json["name"] = name;
        json["type"] = type;
        json["is_private"] = is_private;
        return json;
    }
};

struct Server {
    int id;
    int owner_id;
    std::string name;
    std::string invite_code;
    std::string icon_url;
    std::string created_at;
    int member_count;
    std::vector<int> member_ids; // Detay görünümünde dolar

    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id;
        json["owner_id"] = owner_id;
        json["name"] = name;
        json["invite_code"] = invite_code;
        json["icon_url"] = icon_url;
        json["created_at"] = created_at;
        json["member_count"] = member_count;
        return json;
    }
};