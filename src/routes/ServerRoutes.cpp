#include "ServerRoutes.h"
#include "../utils/Security.h"

void ServerRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // ==========================================================
    // 1. TEMEL SUNUCU İŞLEMLERİ VE AYARLAR
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
                db.logAction(userId, "CREATE_SERVER", serverId, "Kullanici yeni bir sunucu olusturdu.");
                crow::json::wvalue res; res["server_id"] = serverId;
                return crow::response(201, res);
            }
            return crow::response(500, "Sunucu olusturulamadi.");
        }
            });

    CROW_ROUTE(app, "/api/servers/<string>").methods("PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("name")) return crow::response(400);

            if (db.updateServerName(serverId, myId, std::string(x["name"].s()))) {
                db.logAction(myId, "UPDATE_SERVER", serverId, "Sunucu adi degistirildi.");
                return crow::response(200, "Sunucu adi degistirildi.");
            }
        }
        else {
            if (db.deleteServer(serverId, myId)) {
                db.logAction(myId, "DELETE_SERVER", serverId, "Kurucu sunucuyu tamamen sildi.");
                return crow::response(200, "Sunucu tamamen silindi.");
            }
        }
        return crow::response(403, "Bu islem icin sunucu sahibi olmalisiniz.");
            });

    // SUNUCU AYARLARI (SETTINGS) - DÜZELTİLDİ (C2440 Hatası)
    CROW_ROUTE(app, "/api/servers/<string>/settings").methods("GET"_method, "PUT"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);

        if (req.method == "GET"_method) {
            std::string settingsStr = db.getServerSettings(serverId);
            // DÜZELTME: Okunabilir (rvalue) olan JSON'u wvalue'ya çevirip öyle döndürüyoruz.
            auto parsedJson = crow::json::load(settingsStr);
            if (!parsedJson) return crow::response(200, crow::json::wvalue()); // Boş json
            return crow::response(200, crow::json::wvalue(parsedJson));
        }
        else {
            if (!db.hasServerPermission(serverId, userId, "MANAGE_SERVER")) return crow::response(403, "Ayarlari degistirme yetkiniz yok.");
            auto body = crow::json::load(req.body);
            if (!body) return crow::response(400);

            if (db.updateServerSettings(serverId, req.body)) {
                db.logServerAction(serverId, "UPDATE_SETTINGS", "Sunucu ayarlari guncellendi.");
                return crow::response(200, "Ayarlar basariyla kaydedildi.");
            }
            return crow::response(500);
        }
            });

    // ==========================================================
    // 2. KANAL YÖNETİMİ VE KATEGORİLER
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
                db.logAction(userId, "CREATE_CHANNEL", serverId, "Sunucuya yeni bir kanal eklendi.");
                return crow::response(201, "Kanal olusturuldu.");
            }
            return crow::response(500);
        }
            });

    CROW_ROUTE(app, "/api/channels/<string>").methods("PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("name")) return crow::response(400);
            if (db.updateChannelName(channelId, std::string(x["name"].s()))) return crow::response(200, "Kanal adi guncellendi.");
            return crow::response(500);
        }
        else {
            if (db.deleteChannel(channelId)) {
                db.logAction(myId, "DELETE_CHANNEL", channelId, "Sunucu kanali silindi.");
                return crow::response(200, "Kanal silindi.");
            }
            return crow::response(500);
        }
            });

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
                db.logAction(myId, "CREATE_CATEGORY", catId, "Sunucuda kategori olusturuldu.");
                crow::json::wvalue res; res["category_id"] = catId;
                return crow::response(201, res);
            }
            return crow::response(500);
        }
            });

    CROW_ROUTE(app, "/api/channels/<string>/position").methods("PUT"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("position")) return crow::response(400);
        if (db.updateChannelPosition(channelId, x["position"].i())) return crow::response(200);
        return crow::response(500);
            });

    // ==========================================================
    // 3. DAVET (INVITE) SİSTEMİ
    // ==========================================================
    CROW_ROUTE(app, "/api/servers/<string>/invites").methods("POST"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string code = "INV-" + Security::generateId(8);
        if (db.createServerInvite(serverId, Security::getUserIdFromHeader(req), code)) {
            crow::json::wvalue res; res["invite_code"] = code; res["url"] = "https://mysaas.com/join/" + code;
            return crow::response(201, res);
        }
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/servers/join/<string>").methods("POST"_method)
        ([&db](const crow::request& req, std::string inviteCode) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);
        if (db.joinServerByInvite(userId, inviteCode)) {
            db.logAction(userId, "JOIN_SERVER", inviteCode, "Davet kodu ile sunucuya katildi.");
            return crow::response(200, "Sunucuya katildiniz!");
        }
        return crow::response(400, "Gecersiz davet kodu.");
            });

     // ==========================================================
    // SUNUCU ÜYELERİNİ LİSTELE (NORMAL KULLANICI İZNİ)
    // ==========================================================
    CROW_ROUTE(app, "/api/servers/<string>/members").methods("GET"_method)
        ([&db](const crow::request& req, std::string serverId) {

        // 1. Kullanıcı sisteme giriş yapmış mı?
        if (!Security::checkAuth(req, db, false)) return crow::response(401);

        std::string userId = Security::getUserIdFromHeader(req);

        // 2. Güvenlik Kontrolü: Bu kullanıcı bu sunucunun üyesi mi?
        // (Üyesi olmadığı sunucunun üye listesini çekememeli)
        if (!db.isUserInServer(serverId, userId)) {
            return crow::response(403, "Bu sunucunun uyesi degilsiniz.");
        }

        // 3. Veritabanından detaylı üye listesini çek
        auto members = db.getServerMembersDetails(serverId);

        crow::json::wvalue res;
        for (size_t i = 0; i < members.size(); ++i) {
            res[i]["id"] = members[i].id;
            res[i]["name"] = members[i].name;
            res[i]["status"] = members[i].status;
            // İleride buraya avatar_url veya roller de eklenebilir
        }

        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/servers/<string>/leave").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);
        if (db.leaveServer(serverId, userId)) {
            db.logAction(userId, "LEAVE_SERVER", serverId, "Kullanici sunucudan ayrildi.");
            return crow::response(200, "Sunucudan ayrildiniz.");
        }
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/servers/<string>/members/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string serverId, std::string targetId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);
        if (db.kickMember(serverId, myId, targetId)) {
            db.logAction(myId, "KICK_USER", targetId, "Bir uye sunucudan atildi.");
            return crow::response(200, "Uye atildi.");
        }
        return crow::response(403);
            });

    CROW_ROUTE(app, "/api/servers/<string>/members/<string>/timeout").methods("POST"_method)
        ([&db](const crow::request& req, std::string serverId, std::string targetId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        int duration = (x && x.has("duration_minutes")) ? x["duration_minutes"].i() : 60;
        if (db.timeoutUser(serverId, targetId, duration)) {
            db.logAction(Security::getUserIdFromHeader(req), "TIMEOUT_USER", targetId, "Uye " + std::to_string(duration) + " dakika susturuldu.");
            return crow::response(200, "Uye susturuldu.");
        }
        return crow::response(500);
            });

    // ==========================================================
    // 5. ROL YÖNETİMİ (HİYERARŞİK REST STANDARDI)
    // ==========================================================

    // ROLLERİ LİSTELE (GET) VE YENİ ROL OLUŞTUR (POST)
    CROW_ROUTE(app, "/api/servers/<string>/roles").methods("GET"_method, "POST"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);

        if (req.method == "GET"_method) {
            auto roles = db.getServerRoles(serverId);
            crow::json::wvalue res;
            for (size_t i = 0; i < roles.size(); ++i) {
                res[i]["id"] = roles[i].id;
                res[i]["name"] = roles[i].name;
                res[i]["color"] = roles[i].color;
                res[i]["permissions"] = roles[i].permissions;
            }
            return crow::response(200, res);
        }
        else {
            if (!db.hasServerPermission(serverId, userId, "MANAGE_ROLES")) return crow::response(403, "Yetkiniz yok.");
            auto body = crow::json::load(req.body);
            if (!body || !body.has("name") || !body.has("permissions")) return crow::response(400);

            if (db.createRole(serverId, std::string(body["name"].s()), 0, body["permissions"].i())) {
                db.logServerAction(serverId, "ROLE_CREATED", "Yeni rol: " + std::string(body["name"].s()));
                return crow::response(201, "Rol olusturuldu.");
            }
            return crow::response(500);
        }
            });

    // ROLÜ GÜNCELLE VEYA SİL
    CROW_ROUTE(app, "/api/servers/<string>/roles/<string>").methods("PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string serverId, std::string roleId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);
        if (!db.hasServerPermission(serverId, userId, "MANAGE_ROLES")) return crow::response(403);

        if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x) return crow::response(400);
            std::string name = x.has("name") ? std::string(x["name"].s()) : "";
            std::string color = x.has("color") ? std::string(x["color"].s()) : "#FFFFFF";
            int perms = x.has("permissions") ? x["permissions"].i() : 0;

            if (db.updateServerRole(roleId, name, color, perms)) return crow::response(200, "Rol guncellendi.");
        }
        else {
            if (db.deleteServerRole(roleId)) return crow::response(200, "Rol silindi.");
        }
        return crow::response(500);
            });

    // KULLANICIYA ROL ATA (PUT) VEYA GERİ AL (DELETE)
    CROW_ROUTE(app, "/api/servers/<string>/members/<string>/roles/<string>").methods("PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string serverId, std::string targetUserId, std::string roleId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string reqUserId = Security::getUserIdFromHeader(req);
        if (!db.hasServerPermission(serverId, reqUserId, "MANAGE_ROLES")) return crow::response(403);

        if (req.method == "PUT"_method) {
            if (db.assignRoleToMember(serverId, targetUserId, roleId)) return crow::response(200, "Rol atandi.");
        }
        else {
            if (db.removeRoleFromUser(serverId, targetUserId, roleId)) return crow::response(200, "Rol geri alindi.");
        }
        return crow::response(500);
            });

    // ==========================================================
    // 6. SESLİ ODA (VOICE ROOM) YÖNETİMİ
    // ==========================================================
    CROW_ROUTE(app, "/api/channels/<string>/voice/join").methods("POST"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (req.method == "POST"_method) {
            if (db.joinVoiceChannel(channelId, myId)) {
                std::string myName = "User_" + myId;
                std::string livekitToken = Security::generateLiveKitToken(channelId, myName, myId);

                crow::json::wvalue res;
                res["message"] = "Sesli odaya katildiniz.";
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

    CROW_ROUTE(app, "/api/channels/<string>/voice/status").methods("PUT"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400);

        bool muted = x.has("is_muted") ? x["is_muted"].b() : false;
        bool camera = x.has("is_camera_on") ? x["is_camera_on"].b() : false;
        bool screen = x.has("is_screen_sharing") ? x["is_screen_sharing"].b() : false;

        if (db.updateVoiceStatus(channelId, Security::getUserIdFromHeader(req), muted, camera, screen)) {
            return crow::response(200, "Yayin durumu guncellendi.");
        }
        return crow::response(500);
            });

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