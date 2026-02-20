#include "AuthRoutes.h"
#include "../utils/Security.h"
#include <crow/json.h>

void AuthRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // =============================================================
    // API: STANDART GİRİŞ (LOGIN)
    // =============================================================
    CROW_ROUTE(app, "/api/auth/login").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "Gecersiz JSON");
        if (!body.has("email") || !body.has("password")) return crow::response(400, "Eksik bilgi (email ve password gerekli)");

        std::string email = body["email"].s();
        std::string password = body["password"].s();

        std::string userId = db.authenticateUser(email, password);

        if (!userId.empty()) {
            // Kullanıcı online yapıldı
            db.updateLastSeen(userId);

            crow::json::wvalue res;
            // Gerçek JWT Token Üretimi
            res["token"] = Security::generateJwt(userId);
            res["user_id"] = userId;
            res["message"] = "Giris basarili. Durum: Online";
            return crow::response(200, res);
        }

        return crow::response(401, "Gecersiz e-posta veya sifre");
            });

    // =============================================================
    // API: STANDART KAYIT (REGISTER)
    // =============================================================
    CROW_ROUTE(app, "/api/auth/register").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("name") || !body.has("email") || !body.has("password")) {
            return crow::response(400, "Eksik bilgi (name, email, password gerekli)");
        }

        std::string name = body["name"].s();
        std::string email = body["email"].s();
        std::string password = body["password"].s();

        bool success = db.createUser(name, email, password);

        if (success) {
            return crow::response(201, "Kullanici basariyla kaydedildi.");
        }

        return crow::response(409, "Bu e-posta adresi zaten kayitli.");
            });

    // =============================================================
    // API: GOOGLE AUTH CALLBACK (OAUTH2)
    // =============================================================
    CROW_ROUTE(app, "/api/auth/google/callback").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("email") || !body.has("google_id")) {
            return crow::response(400, "Eksik bilgi (email ve google_id gerekli)");
        }

        std::string email = body["email"].s();
        std::string googleId = body["google_id"].s();
        std::string name = body.has("name") ? body["name"].s() : "Google User";
        std::string avatarUrl = body.has("avatar_url") ? body["avatar_url"].s() : "";

        // Önce kullanıcının veritabanında olup olmadığına bak
        auto user = db.getUserByGoogleId(googleId);
        std::string userId;

        if (!user) {
            // Kullanıcı yoksa, yeni bir Google kullanıcısı olarak kaydet
            if (db.createGoogleUser(name, email, googleId, avatarUrl)) {
                user = db.getUserByGoogleId(googleId);
            }
        }

        if (user) {
            userId = user->id;
            db.updateLastSeen(userId);

            crow::json::wvalue res;
            res["token"] = Security::generateJwt(userId);
            res["user_id"] = userId;
            res["message"] = "Google ile giris basarili.";
            return crow::response(200, res);
        }

        return crow::response(500, "Google Auth Hatasi: Kullanici olusturulamadi veya bulunamadi.");
            });
}