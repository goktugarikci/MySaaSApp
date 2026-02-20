#pragma once
#include <string>

struct Message {
    std::string id;
    std::string channelId;
    std::string senderId;     // Eksikti eklendi
    std::string senderName;   // Eksikti eklendi
    std::string senderAvatar; // Eksikti eklendi
    std::string content;
    std::string attachmentUrl;// Eksikti eklendi
    std::string timestamp;
};