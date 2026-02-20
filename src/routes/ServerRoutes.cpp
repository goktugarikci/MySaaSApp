#include "ServerRoutes.h"
#include "../utils/Security.h"
#include <crow/json.h>

// WINDOWS MAKRO ÇAKIŞMASI ÇÖZÜMÜ
#ifdef _WIN32
#undef DELETE
#endif

void ServerRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {
    CROW_ROUTE(app, "/api/servers").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        std::string userId = Security::getUserIdFromHeader(&req);
        auto servers = db.getUserServers(userId);

        crow::json::wvalue res;
        for (size_t i = 0; i < servers.size(); ++i) {
            res[i]["id"] = servers[i].id;
            res[i]["name"] = servers[i].name;
            res[i]["owner_id"] = servers[i].ownerId;
            res[i]["invite_code"] = servers[i].inviteCode;
            res[i]["icon_url"] = servers[i].iconUrl;
            res[i]["member_count"] = servers[i].memberCount;
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/servers").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("name")) return crow::response(400, "Sunucu adi gerekli");

        std::string userId = Security::getUserIdFromHeader(&req);
        std::string serverName = body["name"].s();

        std::string serverId = db.createServer(serverName, userId);

        if (!serverId.empty()) {
            db.createChannel(serverId, "genel", 0);
            db.createChannel(serverId, "sesli-sohbet", 1);

            crow::json::wvalue res;
            res["server_id"] = serverId;
            res["message"] = std::string("Sunucu basariyla olusturuldu");
            return crow::response(201, res);
        }
        return crow::response(403, "Sunucu olusturulamadi");
            });

    CROW_ROUTE(app, "/api/servers/<string>").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto serverOpt = db.getServerDetails(serverId);
        if (!serverOpt) return crow::response(404, "Sunucu bulunamadi");

        crow::json::wvalue res;
        res["id"] = serverOpt->id;
        res["name"] = serverOpt->name;
        res["owner_id"] = serverOpt->ownerId;
        res["invite_code"] = serverOpt->inviteCode;
        res["icon_url"] = serverOpt->iconUrl;
        res["created_at"] = serverOpt->createdAt;
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/servers/<string>").methods(crow::HTTPMethod::DELETE)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        std::string userId = Security::getUserIdFromHeader(&req);
        auto serverOpt = db.getServerDetails(serverId);

        if (!serverOpt) return crow::response(404, "Sunucu bulunamadi");
        if (serverOpt->ownerId != userId && !db.isSystemAdmin(userId)) {
            return crow::response(403, "Yetkiniz yok");
        }

        if (db.deleteServer(serverId)) {
            return crow::response(200, "Sunucu silindi");
        }
        return crow::response(500, "Sunucu silinemedi");
            });

    CROW_ROUTE(app, "/api/servers/<string>/channels").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        std::string userId = Security::getUserIdFromHeader(&req);
        auto channels = db.getServerChannels(serverId, userId);

        crow::json::wvalue res;
        for (size_t i = 0; i < channels.size(); ++i) {
            res[i]["id"] = channels[i].id;
            res[i]["name"] = channels[i].name;
            res[i]["type"] = channels[i].type;
            res[i]["is_private"] = channels[i].isPrivate;
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/servers/<string>/channels").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("name") || !body.has("type")) {
            return crow::response(400, "Eksik parametre");
        }

        std::string name = body["name"].s();
        int type = body["type"].i();
        bool isPrivate = body.has("is_private") ? body["is_private"].b() : false;

        if (db.createChannel(serverId, name, type, isPrivate)) {
            return crow::response(201, "Kanal basariyla olusturuldu");
        }
        return crow::response(403, "Kanal olusturulamadi");
            });

    CROW_ROUTE(app, "/api/channels/<string>/members").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("user_id")) return crow::response(400, "user_id gerekli");

        std::string targetUserId = body["user_id"].s();
        std::string myId = Security::getUserIdFromHeader(&req);

        if (!db.hasChannelAccess(channelId, myId)) {
            return crow::response(403, "Bu kanala uye ekleme yetkiniz yok");
        }

        if (db.addMemberToChannel(channelId, targetUserId)) {
            return crow::response(200, "Uye ozel kanala eklendi");
        }
        return crow::response(500, "Uye eklenemedi");
            });

    CROW_ROUTE(app, "/api/channels/<string>/members/<string>").methods(crow::HTTPMethod::DELETE)
        ([&db](const crow::request& req, std::string channelId, std::string targetUserId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        std::string myId = Security::getUserIdFromHeader(&req);

        if (!db.hasChannelAccess(channelId, myId) && myId != targetUserId) {
            return crow::response(403, "Yetkiniz yok");
        }

        if (db.removeMemberFromChannel(channelId, targetUserId)) {
            return crow::response(200, "Uye kanaldan cikarildi");
        }
        return crow::response(500, "Islem basarisiz");
            });

    CROW_ROUTE(app, "/api/servers/join").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("invite_code")) return crow::response(400, "Davet kodu gerekli");

        std::string userId = Security::getUserIdFromHeader(&req);
        std::string inviteCode = body["invite_code"].s();

        if (db.joinServerByCode(userId, inviteCode)) {
            return crow::response(200, "Sunucuya basariyla katildiniz");
        }
        return crow::response(400, "Gecersiz davet kodu veya zaten uyesiniz");
            });

    CROW_ROUTE(app, "/api/servers/<string>/members").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto members = db.getServerMembersDetails(serverId);

        crow::json::wvalue res;
        for (size_t i = 0; i < members.size(); ++i) {
            res[i]["id"] = members[i].id;
            res[i]["name"] = members[i].name;
            res[i]["status"] = members[i].status;
        }
        return crow::response(200, res);
            });
}