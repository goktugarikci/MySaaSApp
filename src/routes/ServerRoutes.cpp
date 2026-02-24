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

    // ==========================================================
    // 5. ROL YÖNETİMİ - V2.0
    // ==========================================================
    CROW_ROUTE(app, "/api/servers/<string>/roles").methods("POST"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("name")) return crow::response(400);

        std::string roleId = db.createServerRole(serverId, std::string(x["name"].s()), x.has("color") ? std::string(x["color"].s()) : "#000000", x.has("permissions") ? x["permissions"].i() : 0);
        if (!roleId.empty()) {
            db.logAction(Security::getUserIdFromHeader(req), "CREATE_ROLE", roleId, "Yeni sunucu rolu olusturuldu.");
            crow::json::wvalue res; res["role_id"] = roleId;
            return crow::response(201, res);
        }
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/servers/<string>/members/<string>/roles").methods("POST"_method)
        ([&db](const crow::request& req, std::string serverId, std::string userId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("role_id")) return crow::response(400);

        if (db.assignRoleToUser(serverId, userId, std::string(x["role_id"].s()))) {
            db.logAction(Security::getUserIdFromHeader(req), "ASSIGN_ROLE", userId, "Uyeye rol atandi.");
            return crow::response(200, "Rol atandi.");
        }
        return crow::response(500);
            });

}