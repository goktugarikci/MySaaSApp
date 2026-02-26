#include "ServerRoutes.h"
#include "../utils/Security.h"

void ServerRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // ==========================================================
    // 1. TEMEL SUNUCU İŞLEMLERİ VE LOGLAR
    // ==========================================================
    CROW_ROUTE(app, "/api/servers").methods("GET"_method, "POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);

        if (req.method == "GET"_method) {
            auto servers = db.getUserServers(userId);
            crow::json::wvalue res;
            for (size_t i = 0; i < servers.size(); ++i) {
                res[i]["id"] = servers[i].id;
                res[i]["name"] = servers[i].name;
                res[i]["owner_id"] = servers[i].owner_id;
                res[i]["invite_code"] = servers[i].invite_code;
                res[i]["icon_url"] = servers[i].icon_url;
                res[i]["member_count"] = servers[i].member_count;
            }
            return crow::response(200, res);
        }
        else {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("name")) return crow::response(400, "Sunucu adi eksik.");

            std::string serverId = db.createServer(std::string(x["name"].s()), userId);
            if (!serverId.empty()) {
                // LOG: Sunucu Kurulumu
                db.logAction(userId, "CREATE_SERVER", serverId, "Kullanici yeni bir sunucu olusturdu.");

                crow::json::wvalue res;
                res["server_id"] = serverId;
                return crow::response(201, res);
            }
            return crow::response(500, "Sunucu olusturulamadi.");
        }
            });

    // SUNUCUYU DÜZENLE VEYA SİL (SADECE KURUCU)
    CROW_ROUTE(app, "/api/servers/<string>").methods("PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("name")) return crow::response(400);

            // DİKKAT: İsim çakışması (E0308) burada updateServerName ile çözüldü!
            if (db.updateServerName(serverId, myId, std::string(x["name"].s()))) {
                db.logAction(myId, "UPDATE_SERVER", serverId, "Sunucu adi degistirildi.");
                return crow::response(200, "Sunucu adi degistirildi.");
            }
        }
        else {
            // SUNUCU SİLME VE LOGLAMA
            if (db.deleteServer(serverId, myId)) {
                db.logAction(myId, "DELETE_SERVER", serverId, "Kurucu sunucuyu tamamen sildi.");
                return crow::response(200, "Sunucu tamamen silindi.");
            }
        }
        return crow::response(403, "Bu islem icin sunucu sahibi olmalisiniz.");
            });

    // ==========================================================
    // 2. KANAL YÖNETİMİ VE LOGLAR
    // ==========================================================
    CROW_ROUTE(app, "/api/servers/<string>/channels").methods("GET"_method, "POST"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);

        if (req.method == "GET"_method) {
            auto channels = db.getServerChannels(serverId, userId);
            crow::json::wvalue res;
            for (size_t i = 0; i < channels.size(); ++i) res[i] = channels[i].toJson();
            return crow::response(200, res);
        }
        else {
            auto srv = db.getServerDetails(serverId);
            if (!srv || srv->owner_id != userId) return crow::response(403, "Sadece sunucu sahibi kanal acabilir.");

            auto x = crow::json::load(req.body);
            if (!x || !x.has("name") || !x.has("type")) return crow::response(400);

            bool isPrivate = x.has("is_private") ? x["is_private"].b() : false;

            if (db.createChannel(serverId, std::string(x["name"].s()), x["type"].i(), isPrivate)) {
                // LOG: Kanal Ekleme
                db.logAction(userId, "CREATE_CHANNEL", serverId, "Sunucuya yeni bir kanal eklendi.");
                return crow::response(201, "Kanal olusturuldu.");
            }
            return crow::response(500);
        }
            });

    // KANALI DÜZENLE VEYA SİL
    CROW_ROUTE(app, "/api/channels/<string>").methods("PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("name")) return crow::response(400);

            if (db.updateChannelName(channelId, std::string(x["name"].s()))) {
                return crow::response(200, "Kanal adi guncellendi.");
            }
            return crow::response(500);
        }
        else {
            // KANAL SİLME LOGU
            if (db.deleteChannel(channelId)) {
                db.logAction(myId, "DELETE_CHANNEL", channelId, "Sunucu kanali silindi.");
                return crow::response(200, "Kanal silindi.");
            }
            return crow::response(500);
        }
            });

    // ==========================================================
    // 3. DAVET (INVITE) SİSTEMİ
    // ==========================================================
    CROW_ROUTE(app, "/api/servers/<string>/invites").methods("POST"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        std::string inviterId = Security::getUserIdFromHeader(req);
        std::string code = "INV-" + Security::generateId(8); // 8 Haneli Benzersiz Kod

        if (db.createServerInvite(serverId, inviterId, code)) {
            crow::json::wvalue res;
            res["invite_code"] = code;
            res["url"] = "https://mysaas.com/join/" + code;
            res["message"] = "Davet kodu basariyla uretildi.";
            return crow::response(201, res);
        }
        return crow::response(500, "Davet linki olusturulamadi.");
            });

    CROW_ROUTE(app, "/api/servers/join/<string>").methods("POST"_method)
        ([&db](const crow::request& req, std::string inviteCode) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);

        if (db.joinServerByInvite(userId, inviteCode)) {
            // LOG: Sunucuya Katılma
            db.logAction(userId, "JOIN_SERVER", inviteCode, "Kullanici davet kodu ile sunucuya katildi.");
            return crow::response(200, "Sunucuya basariyla katildiniz!");
        }
        return crow::response(400, "Gecersiz veya suresi dolmus davet kodu.");
            });

    // ==========================================================
    // 4. ÜYE YÖNETİMİ (AYRILMA VE KICK LOGLARI)
    // ==========================================================
    CROW_ROUTE(app, "/api/servers/<string>/leave").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);

        if (db.leaveServer(serverId, userId)) {
            // LOG: Sunucudan Çıkış
            db.logAction(userId, "LEAVE_SERVER", serverId, "Kullanici sunucudan ayrildi.");
            return crow::response(200, "Sunucudan ayrildiniz.");
        }
        return crow::response(500);
            });

    // ÜYEYİ AT (KICK) VE LOGLA
    CROW_ROUTE(app, "/api/servers/<string>/members/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string serverId, std::string targetId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (db.kickMember(serverId, myId, targetId)) {
            // LOG: Sunucudan Atma (Kick)
            db.logAction(myId, "KICK_USER", targetId, "Bir uye sunucudan atildi. Sunucu ID: " + serverId);
            return crow::response(200, "Uye sunucudan atildi.");
        }
        return crow::response(403, "Yetkisiz islem (Sadece kurucu silebilir).");
            });

    // ROLÜ GÜNCELLE VE LOGLA
    CROW_ROUTE(app, "/api/servers/<string>/roles/<string>").methods("PUT"_method)
        ([&db](const crow::request& req, std::string serverId, std::string roleId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400);

        std::string name = x.has("name") ? std::string(x["name"].s()) : "";
        std::string color = x.has("color") ? std::string(x["color"].s()) : "#000000";
        int permissions = x.has("permissions") ? x["permissions"].i() : 0;

        if (db.updateServerRole(roleId, name, color, permissions)) {
            db.logAction(Security::getUserIdFromHeader(req), "UPDATE_ROLE", roleId, "Sunucu rolu guncellendi.");
            return crow::response(200, "Rol guncellendi.");
        }
        return crow::response(500);
            });

    // ROLÜ SİL VE LOGLA
    CROW_ROUTE(app, "/api/servers/<string>/roles/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string serverId, std::string roleId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        if (db.deleteServerRole(roleId)) {
            db.logAction(Security::getUserIdFromHeader(req), "DELETE_ROLE", roleId, "Sunucu rolu tamamen silindi.");
            return crow::response(200, "Rol silindi.");
        }
        return crow::response(500);
            });

    // ÜYEDEN ROLÜ GERİ AL (KALDIR)
    CROW_ROUTE(app, "/api/servers/<string>/members/<string>/roles/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string serverId, std::string userId, std::string roleId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        if (db.removeRoleFromUser(serverId, userId, roleId)) {
            db.logAction(Security::getUserIdFromHeader(req), "REMOVE_ROLE_FROM_USER", userId, "Uyeden rol geri alindi.");
            return crow::response(200, "Rol uyeden alindi.");
        }
        return crow::response(500);
            });
    // ==========================================================
    // V3.0 - AŞAMA 3: KATEGORİLER VE MUTE (TIMEOUT)
    // ==========================================================

    // KATEGORİ (KLASÖR) OLUŞTURMA VE LİSTELEME
    CROW_ROUTE(app, "/api/servers/<string>/categories").methods("GET"_method, "POST"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (req.method == "GET"_method) {
            auto cats = db.getServerCategories(serverId);
            crow::json::wvalue res;
            for (size_t i = 0; i < cats.size(); ++i) {
                res[i]["id"] = cats[i].id;
                res[i]["name"] = cats[i].name;
                res[i]["position"] = cats[i].position;
            }
            return crow::response(200, res);
        }
        else {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("name")) return crow::response(400);

            int pos = x.has("position") ? x["position"].i() : 0;
            std::string catId = db.createServerCategory(serverId, std::string(x["name"].s()), pos);

            if (!catId.empty()) {
                db.logAction(myId, "CREATE_CATEGORY", catId, "Sunucuda yeni bir kanal kategorisi olusturuldu.");
                crow::json::wvalue res; res["category_id"] = catId;
                return crow::response(201, res);
            }
            return crow::response(500);
        }
            });

    // KANAL SIRALAMASINI (POSITION) DEĞİŞTİRME
    CROW_ROUTE(app, "/api/channels/<string>/position").methods("PUT"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("position")) return crow::response(400);

        if (db.updateChannelPosition(channelId, x["position"].i())) {
            return crow::response(200, "Kanal siralamasi guncellendi.");
        }
        return crow::response(500);
            });

    // ÜYEYİ SUSTURMA (TIMEOUT / MUTE) VE LOGLAMA
    CROW_ROUTE(app, "/api/servers/<string>/members/<string>/timeout").methods("POST"_method)
        ([&db](const crow::request& req, std::string serverId, std::string targetId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);

        // Varsayılan olarak 60 dakika (1 saat) susturur
        int durationMinutes = (x && x.has("duration_minutes")) ? x["duration_minutes"].i() : 60;

        if (db.timeoutUser(serverId, targetId, durationMinutes)) {
            db.logAction(Security::getUserIdFromHeader(req), "TIMEOUT_USER", targetId, "Uye " + std::to_string(durationMinutes) + " dakika susturuldu.");
            return crow::response(200, "Uye basariyla susturuldu.");
        }
        return crow::response(500);
            });
    // ==========================================================
    // V3.0 - SESLİ ODA (VOICE ROOM) YÖNETİMİ
    // ==========================================================

    // SESLİ ODAYA KATIL / AYRIL
// SESLİ ODAYA KATIL (MEDYA SUNUCUSUNDAN TOKEN AL)
    CROW_ROUTE(app, "/api/channels/<string>/voice/join").methods("POST"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (req.method == "POST"_method) {
            // 1. Kullanıcıyı SQL Veritabanında Odaya Ekle
            if (db.joinVoiceChannel(channelId, myId)) {

                // 2. Kullanıcının LiveKit'te görünecek adını hazırla
                // İleride db.getUserById() tarzı bir fonksiyonla gerçek adını çekebilirsiniz, şimdilik ID'si ile isim oluşturuyoruz:
                std::string myName = "User_" + myId;

                // 3. LiveKit Biletini (JWT) Üret
                std::string livekitToken = Security::generateLiveKitToken(channelId, myName, myId);

                // 4. İstemciye (Frontend) Bağlantı Bilgilerini JSON Olarak Dön
                crow::json::wvalue res;
                res["message"] = "Sesli odaya katildiniz.";
                // Eğer LiveKit sunucunuzu Oracle Cloud'a kurduysanız, "localhost" yerine o makinenin IP adresini (ws://IP_ADRES:7880) yazmalısınız.
                res["livekit_url"] = "ws://localhost:7880";
                res["livekit_token"] = livekitToken;

                return crow::response(200, res);
            }
        }
        else {
            if (db.leaveVoiceChannel(channelId, myId)) return crow::response(200, "Sesli odadan ayrildi.");
        }
        return crow::response(500);
            });


    // KAMERA, MİKROFON VEYA EKRAN PAYLAŞIMI DURUMUNU GÜNCELLE
    CROW_ROUTE(app, "/api/channels/<string>/voice/status").methods("PUT"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400);

        std::string myId = Security::getUserIdFromHeader(req);
        bool muted = x.has("is_muted") ? x["is_muted"].b() : false;
        bool camera = x.has("is_camera_on") ? x["is_camera_on"].b() : false;
        bool screen = x.has("is_screen_sharing") ? x["is_screen_sharing"].b() : false;

        if (db.updateVoiceStatus(channelId, myId, muted, camera, screen)) {
            return crow::response(200, "Yayin durumu guncellendi.");
        }
        return crow::response(500);
            });

    // ODADAKİLERİ VE DURUMLARINI GETİR
    CROW_ROUTE(app, "/api/channels/<string>/voice/members").methods("GET"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        auto members = db.getVoiceChannelMembers(channelId);
        crow::json::wvalue res;
        for (size_t i = 0; i < members.size(); ++i) {
            res[i]["user_id"] = members[i].user_id;
            res[i]["user_name"] = members[i].user_name;
            res[i]["is_muted"] = members[i].is_muted;
            res[i]["is_camera_on"] = members[i].is_camera_on;
            res[i]["is_screen_sharing"] = members[i].is_screen_sharing;
        }
        return crow::response(200, res);
            });
}