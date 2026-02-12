#pragma once
#include <string>
#include "crow.h"

struct Message {
    int id;
    int channel_id;
    int sender_id;
    std::string sender_name;
    std::string sender_avatar;
    std::string content;
    std::string attachment_url;
    std::string timestamp;

    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id;
        json["channel_id"] = channel_id;
        json["sender_id"] = sender_id;
        json["sender_name"] = sender_name;
        json["sender_avatar"] = sender_avatar;
        json["content"] = content;
        json["attachment_url"] = attachment_url;
        json["timestamp"] = timestamp;
        return json;
    }
};