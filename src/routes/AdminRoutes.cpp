#include "crow.h"
#include "../db/DatabaseManager.h"
#include "../utils/Security.h"

void setupAdminRoutes(crow::SimpleApp& app, DatabaseManager& db) {

    CROW_ROUTE(app, "/api/admin/stats")
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        SystemStats stats = db.getSystemStats();
        crow::json::wvalue res;
        res["user_count"] = stats.user_count;
        res["server_count"] = stats.server_count;
        res["message_count"] = stats.message_count;
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/admin/logs")
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        std::vector<ServerLog> logs = db.getSystemLogs(100);
        crow::json::wvalue res;
        for (size_t i = 0; i < logs.size(); ++i) {
            res[i]["id"] = logs[i].id;
            res[i]["server_id"] = logs[i].server_id;
            res[i]["level"] = logs[i].level;
            res[i]["action"] = logs[i].action;
            res[i]["details"] = logs[i].details;
            res[i]["created_at"] = logs[i].created_at; // DÜZELTİLDİ
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/admin/archives")
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        std::vector<Message> archives = db.getArchivedMessages(100);
        crow::json::wvalue res;
        for (size_t i = 0; i < archives.size(); ++i) {
            res[i]["id"] = archives[i].id;
            res[i]["original_channel_id"] = archives[i].original_channel_id; // DÜZELTİLDİ
            res[i]["sender_id"] = archives[i].sender_id;                     // DÜZELTİLDİ
            res[i]["content"] = archives[i].content;
            res[i]["deleted_at"] = archives[i].deleted_at;                   // DÜZELTİLDİ
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/admin/servers")
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        auto servers = db.getAllServers();
        crow::json::wvalue res;
        for (size_t i = 0; i < servers.size(); ++i) res[i] = servers[i].toJson();
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/admin/servers/<string>/details")
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        crow::json::wvalue res;
        auto members = db.getServerMembersDetails(serverId);
        for (size_t i = 0; i < members.size(); ++i) {
            res["members"][i]["id"] = members[i].id;
            res["members"][i]["name"] = members[i].name;
            res["members"][i]["status"] = members[i].status;
        }
        auto logs = db.getServerLogs(serverId);
        for (size_t i = 0; i < logs.size(); ++i) {
            res["logs"][i]["time"] = logs[i].created_at;
            res["logs"][i]["action"] = logs[i].action;
            res["logs"][i]["details"] = logs[i].details;
        }
        return crow::response(200, res);
            });
}