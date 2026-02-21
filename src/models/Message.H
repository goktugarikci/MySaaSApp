#pragma once
#include <string>
#include "crow.h"

struct Message {
    std::string id;
    std::string channel_id;
    std::string sender_id;
    std::string sender_name;
    std::string sender_avatar;
    std::string content;
    std::string attachment_url;
    std::string timestamp;

    // --- Admin Arşiv Rotaları İçin ---
    std::string original_channel_id;
    std::string deleted_at;

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
        if (!original_channel_id.empty()) json["original_channel_id"] = original_channel_id;
        if (!deleted_at.empty()) json["deleted_at"] = deleted_at;
        return json;
    }
};