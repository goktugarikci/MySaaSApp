#pragma once
#include <string>
#include <crow.h> // JSON dönüşümleri için eklendi

// Ödeme İşlemi (Transaction) Veri Modeli
struct PaymentTransaction {
    std::string id;
    std::string userId;
    std::string providerPaymentId; // Stripe, iyzico, mPos vb. tarafından dönen ID
    float amount;
    std::string currency;
    std::string status; // "pending", "success", "failed" vb.
    std::string createdAt;

    // Yönlendirme (Router) sınıfları için JSON'a dönüştürme metodu
    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id;
        json["user_id"] = userId;
        json["provider_payment_id"] = providerPaymentId;
        json["amount"] = amount;
        json["currency"] = currency;
        json["status"] = status;
        json["created_at"] = createdAt;

        return json;
    }
};