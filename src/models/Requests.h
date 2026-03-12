#pragma once
#include <string>
#include <crow.h>

struct FriendRequest {
    std::string requesterId;
    std::string requesterName;
    std::string requesterAvatar;
    std::string createdAt;

    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["requester_id"] = requesterId;
        json["requester_name"] = requesterName;
        json["requester_avatar"] = requesterAvatar;
        json["created_at"] = createdAt;
        return json;
    }
};