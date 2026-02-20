#pragma once
#include <string>

// Ödeme İşlemi (Transaction) Veri Modeli
struct PaymentTransaction {
    std::string id;
    std::string userId;
    std::string providerPaymentId; // Stripe, mPos vb. tarafından dönen ID
    float amount;
    std::string currency;
    std::string status; // "pending", "completed", "failed" vb.
    std::string createdAt;
};