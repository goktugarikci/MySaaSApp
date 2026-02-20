#pragma once
#include <string>

struct User {
    std::string id;
    std::string name;
    std::string email;
    std::string status;
    bool isSystemAdmin;
    std::string avatarUrl;      // Eksikti eklendi
    std::string subscriptionLevel; // Eksikti eklendi (String olarak tutulabilir veya int)
    int subscriptionLevelInt;   // DB'den int geliyor
    std::string subscriptionExpiresAt; // Eksikti eklendi
    std::string googleId;       // Eksikti eklendi
};