#pragma once
#include <string>
#include <vector>

struct Role {
    std::string id;
    std::string serverId;
    std::string roleName;
    std::string color;
    int hierarchy;
    int permissions;
};

struct Channel {
    std::string id;
    std::string serverId;
    std::string name;
    int type;
    bool isPrivate; // Eksikti eklendi
};

struct Server {
    std::string id;
    std::string name;
    std::string ownerId;    // Eksikti eklendi
    std::string inviteCode; // Eksikti eklendi
    std::string iconUrl;    // Eksikti eklendi
    std::string createdAt;  // Eksikti eklendi
    int memberCount;        // Eksikti eklendi
    std::vector<Channel> channels;
};