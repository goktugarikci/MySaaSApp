#include "PaymentRoutes.h"
#include "../utils/Security.h"

void PaymentRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // ==========================================================
    // 1. KULLANICININ ÖDEME GEÇMİŞİNİ GETİR
    // ==========================================================
    CROW_ROUTE(app, "/api/payments").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);

        std::vector<PaymentTransaction> payments = db.getUserPayments(userId);
        crow::json::wvalue res;
        for (size_t i = 0; i < payments.size(); ++i) {
            res[i] = payments[i].toJson();
        }
        return crow::response(200, res);
            });

    // ==========================================================
    // 2. YENİ ÖDEME (CHECKOUT) BAŞLATMA
    // ==========================================================
    CROW_ROUTE(app, "/api/payments/checkout").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("amount") || !x.has("currency")) return crow::response(400, "Tutar ve para birimi eksik.");

        std::string userId = Security::getUserIdFromHeader(req);
        float amount = x["amount"].d(); // .d() double olarak çeker
        std::string currency = std::string(x["currency"].s());

        // Gerçek bir sistemde burada Stripe/Iyzico API'sine istek atılır ve bir ödeme ID'si alınır.
        // Biz şimdilik sistemde benzersiz bir "Mock Provider ID" üretiyoruz.
        std::string providerPaymentId = "PAY-" + Security::generateId(15);

        if (db.createPaymentRecord(userId, providerPaymentId, amount, currency)) {
            crow::json::wvalue res;
            res["message"] = "Odeme oturumu baslatildi.";
            res["provider_payment_id"] = providerPaymentId;
            res["amount"] = amount;
            res["currency"] = currency;
            res["status"] = "pending";
            return crow::response(201, res);
        }
        return crow::response(500, "Odeme kaydi olusturulamadi.");
            });

    // ==========================================================
    // 3. WEBHOOK (SANAL POS'TAN GELEN BAŞARILI ÖDEME BİLDİRİMİ)
    // ==========================================================
    // Not: Gerçekte bu endpoint'e istek atanın (Stripe/Iyzico) imzası kontrol edilmelidir.
    CROW_ROUTE(app, "/api/payments/webhook").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("provider_payment_id") || !x.has("status")) return crow::response(400);

        std::string providerId = std::string(x["provider_payment_id"].s());
        std::string status = std::string(x["status"].s()); // "success" veya "failed" bekleniyor

        // Gelen istekle ödeme durumunu güncelliyoruz
        if (!db.updatePaymentStatus(providerId, status)) {
            return crow::response(404, "Odeme kaydi bulunamadi.");
        }

        // EĞER ÖDEME BAŞARILIYSA KULLANICI ABONELİĞİNİ YÜKSELT
        if (status == "success") {
            // Önce providerId'ye ait userId'yi bulmamız lazım (Bunun için DB'de ufak bir sorgu yapıyoruz)
            std::string userId = "";
            auto payments = db.getUserPayments(""); // Tüm ödemeleri çekersek performans sorunu olur, bu yüzden doğrudan auth olmadan çalışan bir veritabanı sorgusu daha sağlıklı olur.
            // Fakat pratiklik için ödemenin ait olduğu kullanıcıyı bulduğumuzu varsayalım. 
            // (DatabaseManager'a doğrudan getUserByPaymentId yazılabilir, ancak şimdilik body'den aldığımızı varsayalım).

            if (x.has("user_id") && x.has("subscription_level") && x.has("duration_days")) {
                std::string targetUserId = std::string(x["user_id"].s());
                int level = x["subscription_level"].i();
                int days = x["duration_days"].i();

                db.updateUserSubscription(targetUserId, level, days);
                return crow::response(200, "Odeme onaylandi, abonelik yukseltildi.");
            }
            return crow::response(200, "Odeme durumu guncellendi ancak abonelik verisi eksik oldugu icin yukseltme yapilmadi.");
        }

        return crow::response(200, "Odeme durumu guncellendi: " + status);
            });
}