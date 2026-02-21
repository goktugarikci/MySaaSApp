#include "ServerRoutes.h"
#include "../utils/Security.h"

void ServerRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    CROW_ROUTE(app, "/api/servers").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);
        std::vector<Server> servers = db.getUserServers(userId);
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
            });

    CROW_ROUTE(app, "/api/servers").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("name")) return crow::response(400);

        std::string userId = Security::getUserIdFromHeader(req);
        std::string serverId = db.createServer(std::string(x["name"].s()), userId);
        if (!serverId.empty()) {
            crow::json::wvalue res; res["server_id"] = serverId;
            return crow::response(201, res);
        }
        return crow::response(403);
            });

    CROW_ROUTE(app, "/api/servers/<string>/channels").methods("POST"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);
        auto srv = db.getServerDetails(serverId);
        if (!srv || srv->owner_id != userId) return crow::response(403);

        auto x = crow::json::load(req.body);
        if (!x || !x.has("name") || !x.has("type")) return crow::response(400);

        bool isPrivate = x.has("is_private") ? x["is_private"].b() : false;

        if (db.createChannel(serverId, std::string(x["name"].s()), x["type"].i(), isPrivate)) {
            return crow::response(201);
        }
        return crow::response(403);
            });

    CROW_ROUTE(app, "/api/servers/<string>/channels").methods("GET"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);

        std::vector<Channel> channels = db.getServerChannels(serverId, userId);
        crow::json::wvalue res;
        for (size_t i = 0; i < channels.size(); ++i) res[i] = channels[i].toJson();
        return crow::response(200, res);
            });
}