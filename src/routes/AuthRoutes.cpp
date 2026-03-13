#include "AuthRoutes.h"
#include "../utils/Security.h"
#include <crow/json.h>
#include <cpr/cpr.h>

void AuthRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // KULLANICI GİRİŞİ (LOGIN) VE BAN KONTROLÜ
    CROW_ROUTE(app, "/api/auth/login").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("email") || !body.has("password")) {
            return crow::response(400, "Eksik bilgi (email ve password gerekli)");
        }

        std::string email = std::string(body["email"].s());
        std::string password = std::string(body["password"].s());

        // 1. Şifre doğrulamasını güvenli bir şekilde DatabaseManager'a bırakıyoruz
        if (db.loginUser(email, password)) {

            // Giriş başarılıysa kullanıcı bilgilerini çek
            auto user = db.getUser(email);
            if (!user) return crow::response(500, "Kullanici verisi alinamadi.");

            // 2. 🛡️ BAN KONTROLÜ
            if (user->status == "Banned") {
                return crow::response(403, "Hesabiniz sistem kurallarini ihlal ettiginiz icin yasaklanmistir.");
            }

            // 3. Kullanıcı banlı değilse sisteme giriş yapmasına izin ver
            db.updateLastSeen(user->id);
            db.updateUserStatus(user->id, "Online");

            crow::json::wvalue res;
            res["token"] = Security::generateJwt(user->id);
            res["user_id"] = user->id;
            res["name"] = user->name;

            // HATAYI ÇÖZEN KISIM: User modelinde avatar_url olmadığı için şimdilik boş gönderiyoruz.
            // İleride "UploadRoutes" yazdığımızda buraya gerçek resmi bağlayacağız.
            res["avatar_url"] = "";

            res["message"] = "Giris basarili.";
            return crow::response(200, res);
        }

        return crow::response(401, "Gecersiz e-posta veya sifre.");
            });

    // src/routes/AuthRoutes.cpp içindeki register rotasını bul ve bununla değiştir:

    CROW_ROUTE(app, "/api/auth/register").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("name") || !x.has("email") || !x.has("password")) {
            return crow::response(400, "Ad, email ve sifre zorunludur.");
        }

        std::string name = std::string(x["name"].s());
        std::string email = std::string(x["email"].s());
        std::string password = std::string(x["password"].s());

        // Opsiyonel alanlar (Frontend göndermezse boş kalsın)
        std::string username = x.has("username") ? std::string(x["username"].s()) : name;
        std::string phone = x.has("phone_number") ? std::string(x["phone_number"].s()) : "";

        // E-posta zaten var mı?
        if (db.getUser(email).has_value()) {
            return crow::response(409, "Bu e-posta adresi zaten kayitli.");
        }

        // Veritabanına kaydet (Admin değil olarak: false)
        if (db.createUser(name, email, password, false, username, phone)) {
            return crow::response(201, "Kullanici basariyla olusturuldu.");
        }

        // Eğer buraya düşerse veritabanı yazma hatası (500)
        return crow::response(500, "Sunucu hatasi: Kullanici olusturulamadi (Veritabani hatasi olabilir).");
            });


    // GOOGLE OAUTH2 GÜVENLİ GİRİŞ (TOKEN DOĞRULAMA)
    CROW_ROUTE(app, "/api/auth/google/callback").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("id_token")) {
            return crow::response(400, "Eksik bilgi (id_token gerekli)");
        }

        std::string idToken = body["id_token"].s();

        // 1. C++ sunucumuz gizlice Google'a bağlanıp tokeni doğruluyor
        cpr::Response r = cpr::Get(
            cpr::Url{ "https://oauth2.googleapis.com/tokeninfo" },
            cpr::Parameters{ {"id_token", idToken} }
        );

        // Google bu token geçersiz veya sahte derse:
        if (r.status_code != 200) {
            return crow::response(401, "Google Dogrulama Basarisiz: Sahte veya suresi dolmus token.");
        }

        // 2. Token gerçekse Google'ın bize verdiği kullanıcı verilerini oku
        auto googleData = crow::json::load(r.text);
        if (!googleData) return crow::response(500, "Google yaniti okunamadi.");

        std::string email = googleData["email"].s();
        std::string googleId = googleData["sub"].s(); // Google'ın benzersiz kullanıcı ID'si
        std::string name = googleData.has("name") ? std::string(googleData["name"].s()) : "Google User";
        std::string avatarUrl = googleData.has("picture") ? std::string(googleData["picture"].s()) : "";

        // 3. Kullanıcıyı Veritabanımızda Bul veya Yeni Kayıt Aç
        auto user = db.getUserByGoogleId(googleId);
        std::string userId;

        if (!user) {
            // Sistemi ilk defa Google ile kullanan biri
            if (db.createGoogleUser(name, email, googleId, avatarUrl)) {
                user = db.getUserByGoogleId(googleId);
            }
        }

        // 4. Kullanıcıya kendi sistemimizin JWT biletini ver
        if (user) {
            userId = user->id;
            db.updateLastSeen(userId);

            crow::json::wvalue res;
            res["token"] = Security::generateJwt(userId);
            res["user_id"] = userId;
            res["name"] = name;
            res["avatar_url"] = avatarUrl;
            res["message"] = std::string("Google ile guvenli giris basarili.");
            return crow::response(200, res);
        }

        return crow::response(500, "Google Auth Hatasi: Kullanici olusturulamadi.");
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
    // 2FA AKTİFLEŞTİRME (İki Aşamalı Doğrulama)
    CROW_ROUTE(app, "/api/auth/2fa/enable").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        // Gerçek bir sistemde burada Google Authenticator için "TOTP Secret" ve "QR Code" üretilir.
        std::string mockSecret = Security::generateId(16);

        if (db.enable2FA(myId, mockSecret)) {
            db.logAction(myId, "ENABLE_2FA", myId, "Kullanici 2 Asamali Dogrulamayi aktif etti.");
            crow::json::wvalue res;
            res["secret"] = mockSecret;
            res["message"] = "2FA aktif edildi. Lutfen bu gizli anahtari Authenticator uygulamasina girin.";
            return crow::response(200, res);
        }
        return crow::response(500);
            });
    // 2FA DEVRE DIŞI BIRAKMA (Kapatma)
    CROW_ROUTE(app, "/api/auth/2fa/disable").methods("DELETE"_method, "POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (db.disable2FA(myId)) {
            db.logAction(myId, "DISABLE_2FA", myId, "Kullanici 2 Asamali Dogrulamayi (2FA) kapatti.");
            return crow::response(200, "2FA basariyla devre disi birakildi.");
        }
        return crow::response(500);
            });
}