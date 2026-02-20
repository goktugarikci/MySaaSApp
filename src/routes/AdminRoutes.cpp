#include "AdminRoutes.h"
#include "../utils/Security.h"
#include <crow/json.h>

void AdminRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {
    CROW_ROUTE(app, "/api/admin/logs/system").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(&req, &db, true)) return crow::response(403, "Yetkisiz Erisim: Sadece Super Adminler");

        auto logs = db.getSystemLogs(100);

        crow::json::wvalue res;
        for (size_t i = 0; i < logs.size(); ++i) {
            res[i]["id"] = logs[i].id;
            res[i]["level"] = logs[i].level;
            res[i]["action"] = logs[i].action;
            res[i]["details"] = logs[i].details;
            res[i]["created_at"] = logs[i].created_at;
        }

        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/admin/logs/servers/<string>").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(&req, &db, true)) return crow::response(403, "Yetkisiz Erisim: Sadece Super Adminler");

        auto logs = db.getServerLogs(serverId);

        crow::json::wvalue res;
        for (size_t i = 0; i < logs.size(); ++i) {
            res[i]["created_at"] = logs[i].timestamp;
            res[i]["action"] = logs[i].action;
            res[i]["details"] = logs[i].details;
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/admin/archive/messages").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(&req, &db, true)) return crow::response(403, "Yetkisiz Erisim: Sadece Super Adminler");

        auto archives = db.getArchivedMessages(100);

        crow::json::wvalue res;
        for (size_t i = 0; i < archives.size(); ++i) {
            res[i]["id"] = archives[i].id;
            res[i]["original_channel_id"] = archives[i].original_channel_id;
            res[i]["sender_id"] = archives[i].sender_id;
            res[i]["content"] = archives[i].content;
            res[i]["deleted_at"] = archives[i].deleted_at;
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/admin/users/<string>/audit").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req, std::string targetUserId) {
        if (!Security::checkAuth(&req, &db, true)) return crow::response(403, "Yetkisiz Erisim: Sadece Super Adminler");

        crow::json::wvalue res;
        res["user_id"] = targetUserId;
        res["audit_trail"] = std::string("Kullanici islem gecmisi hazirlaniyor...");

        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/admin/users/<string>/servers").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req, std::string targetUserId) {
        if (!Security::checkAuth(&req, &db, true)) return crow::response(403, "Yetkisiz Erisim: Sadece Super Adminler");

        auto servers = db.getUserServers(targetUserId);
        auto userOpt = db.getUserById(targetUserId);

        crow::json::wvalue res;

        if (userOpt) {
            res["user"]["id"] = userOpt->id;
            res["user"]["name"] = userOpt->name;
            res["user"]["status"] = userOpt->status;
            res["user"]["subscription_level"] = userOpt->subscriptionLevelInt;
            res["user"]["is_enterprise"] = (userOpt->subscriptionLevelInt > 0);
        }

        for (size_t i = 0; i < servers.size(); ++i) {
            res["servers"][i]["server_id"] = servers[i].id;
            res["servers"][i]["server_name"] = servers[i].name;
            res["servers"][i]["member_count"] = servers[i].memberCount;

            bool isOwner = (servers[i].ownerId == targetUserId);
            res["servers"][i]["is_owner"] = isOwner;
            res["servers"][i]["role"] = isOwner ? std::string("Owner") : std::string("Member");
        }

        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/admin/servers/<string>/detailed_members").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(&req, &db, true)) return crow::response(403, "Yetkisiz Erisim: Sadece Super Adminler");

        auto members = db.getServerMembersDetails(serverId);
        auto serverOpt = db.getServerDetails(serverId);

        crow::json::wvalue res;
        if (serverOpt) {
            res["server_id"] = serverOpt->id;
            res["server_name"] = serverOpt->name;
            res["owner_id"] = serverOpt->ownerId;
            res["total_members"] = members.size();
        }

        for (size_t i = 0; i < members.size(); ++i) {
            res["members"][i]["user_id"] = members[i].id;
            res["members"][i]["name"] = members[i].name;
            res["members"][i]["status"] = members[i].status;

            bool isOwner = (serverOpt && serverOpt->ownerId == members[i].id);
            res["members"][i]["is_owner"] = isOwner;
            res["members"][i]["role"] = isOwner ? std::string("Owner") : std::string("Member");
        }

        return crow::response(200, res);
            });
}