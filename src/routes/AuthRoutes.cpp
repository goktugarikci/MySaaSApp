#include "AuthRoutes.h"
#include "../utils/Security.h"
#include <crow/json.h>

void AuthRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    CROW_ROUTE(app, "/api/auth/login").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "Gecersiz JSON");
        if (!body.has("email") || !body.has("password")) return crow::response(400, "Eksik bilgi (email ve password gerekli)");
        std::string email = body["email"].s();
        std::string password = body["password"].s();
        std::string userId = db.authenticateUser(email, password);
        if (!userId.empty()) {
            db.updateLastSeen(userId);

            crow::json::wvalue res;
            res["token"] = Security::generateJwt(userId);
            res["user_id"] = userId;
            res["message"] = std::string("Giris basarili. Durum: Online"); // Explicit cast eklendi
            return crow::response(200, res);
        }

        return crow::response(401, "Gecersiz e-posta veya sifre");
            });

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

    CROW_ROUTE(app, "/api/auth/google/callback").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("email") || !body.has("google_id")) {
            return crow::response(400, "Eksik bilgi (email ve google_id gerekli)");
        }

        std::string email = body["email"].s();
        std::string googleId = body["google_id"].s();
        std::string name = body.has("name") ? std::string(body["name"].s()) : std::string("Google User");
        std::string avatarUrl = body.has("avatar_url") ? std::string(body["avatar_url"].s()) : std::string("");

        auto user = db.getUserByGoogleId(googleId);
        std::string userId;

        if (!user) {
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
            res["message"] = std::string("Google ile giris basarili."); // Explicit cast eklendi
            return crow::response(200, res);
        }

        return crow::response(500, "Google Auth Hatasi: Kullanici olusturulamadi veya bulunamadi.");
            });

    // ==========================================================
    // YENİ EKLENENLER: ŞİFRE SIFIRLAMA (Şifremi Unuttum)
    // ==========================================================

    // 3. ŞİFRE SIFIRLAMA KODU GÖNDER (FORGOT PASSWORD)
    CROW_ROUTE(app, "/api/auth/forgot-password").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("email")) return crow::response(400);

        std::string resetToken = Security::generateId(10); // Rastgele bir kod üret

        // Veritabanında bu mail varsa kodu kaydet
        if (db.createPasswordResetToken(std::string(x["email"].s()), resetToken)) {
            // Normalde burada SMTP ile mail atılır. Biz frontend'e simüle ediyoruz.
            crow::json::wvalue res;
            res["message"] = "Sifirlama kodu e-posta adresinize gonderildi.";
            res["debug_token"] = resetToken; // Test edebilmeniz için tokeni dönüyoruz
            return crow::response(200, res);
        }
        return crow::response(404, "Kullanici bulunamadi.");
            });

    // 4. YENİ ŞİFREYİ BELİRLE (RESET PASSWORD)
    CROW_ROUTE(app, "/api/auth/reset-password").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("token") || !x.has("new_password")) return crow::response(400);

        if (db.resetPasswordWithToken(std::string(x["token"].s()), std::string(x["new_password"].s()))) {
            return crow::response(200, "Sifreniz basariyla degistirildi. Lutfen yeni sifrenizle giris yapin.");
        }
        return crow::response(400, "Gecersiz veya suresi dolmus dogrulama kodu.");
            });

}