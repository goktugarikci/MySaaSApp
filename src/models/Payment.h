#pragma once
#include <string>
#include <crow/json.h>

struct PaymentTransaction {
    std::string id;
    std::string userId;
    std::string providerId; // <-- HATA VEREN BÜYÜK/KÜÇÜK HARF UYUMSUZLUĞU
    double amount;
    std::string currency;
    std::string status;
    std::string date;

    crow::json::wvalue toJson() const {
        crow::json::wvalue j;
        j["id"] = id;
        j["provider_id"] = providerId;
        j["amount"] = amount;
        j["currency"] = currency;
        j["status"] = status;
        j["date"] = date;
        return j;
    }
};