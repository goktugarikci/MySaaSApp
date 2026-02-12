#pragma once
#include <string>
#include "crow.h"

struct PaymentTransaction {
    int id;
    int user_id;
    std::string provider_payment_id; // Stripe veya Iyzico'dan dönen işlem ID'si
    float amount;
    std::string currency;
    std::string status; // 'pending', 'success', 'failed'
    std::string created_at;

    // API yanıtı için JSON formatına çevirme
    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id;
        json["user_id"] = user_id;
        json["provider_payment_id"] = provider_payment_id;
        json["amount"] = amount;
        json["currency"] = currency;
        json["status"] = status;
        json["created_at"] = created_at;
        return json;
    }
};