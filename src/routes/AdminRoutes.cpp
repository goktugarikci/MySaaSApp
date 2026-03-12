#include "AdminRoutes.h"
#include "../utils/Security.h"

void AdminRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // ==========================================================
    // SİSTEM İSTATİSTİKLERİ (Dashboard için)
    // ==========================================================
    CROW_ROUTE(app, "/api/admin/stats").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403, "Erisim reddedildi. Sistem Yoneticisi olmalisiniz.");

        SystemStats stats = db.getSystemStats();
        crow::json::wvalue res;
        res["total_users"] = stats.total_users;
        res["total_servers"] = stats.total_servers;
        res["active_users"] = stats.active_users;
        res["total_messages"] = stats.total_messages;

        return crow::response(200, res);
            });

    // ==========================================================
    // GENEL SİSTEM LOGLARI (Güvenlik / Denetim)
    // ==========================================================
    CROW_ROUTE(app, "/api/admin/logs").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);

        auto logs = db.getSystemLogs();
        crow::json::wvalue res;
        for (size_t i = 0; i < logs.size(); ++i) {
            res[i]["id"] = logs[i].id;
            res[i]["server_id"] = logs[i].server_id;
            res[i]["level"] = logs[i].level;
            res[i]["action"] = logs[i].action;
            res[i]["details"] = logs[i].details;
            res[i]["timestamp"] = logs[i].timestamp;
        }
        return crow::response(200, res);
            });

    // ==========================================================
    // SPESİFİK BİR SUNUCUNUN (WORKSPACE) LOGLARI
    // ==========================================================
    CROW_ROUTE(app, "/api/admin/server-logs/<string>").methods("GET"_method)
        ([&db](const crow::request& req, std::string serverId) {

        std::string myId = Security::getUserIdFromHeader(req);
        bool isSysAdmin = db.isSystemAdmin(myId);
        bool isOwner = false;

        auto srv = db.getServerDetails(serverId);
        if (srv && srv->owner_id == myId) {
            isOwner = true;
        }

        if (!isSysAdmin && !isOwner) {
            return crow::response(403, "Bu sunucunun loglarini gormeye yetkiniz yok.");
        }

        auto logs = db.getServerLogs(serverId);
        crow::json::wvalue res;
        for (size_t i = 0; i < logs.size(); ++i) {
            res[i]["id"] = logs[i].id;
            res[i]["level"] = logs[i].level;
            res[i]["action"] = logs[i].action;
            res[i]["details"] = logs[i].details;
            res[i]["timestamp"] = logs[i].timestamp;
        }
        return crow::response(200, res);
            });

}