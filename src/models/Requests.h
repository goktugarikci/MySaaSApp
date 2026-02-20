#pragma once
#include <string>

// Arkadaşlık İsteği Veri Modeli
struct FriendRequest {
    std::string requesterId;
    std::string requesterName;
    std::string requesterAvatar;
    std::string createdAt;
};