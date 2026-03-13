#include "AdminRoutes.h"
#include "../utils/Security.h"

// Sadece C++ AdminUI programının bildiği çok gizli şifre
const std::string ADMIN_SECRET_KEY = "MYSASS_ADMIN_SECRET_998877";

// Yetki Kontrol Fonksiyonu
bool isFromAdminUI(const crow::request& req) {
    return req.get_header_value("X-Admin-Key") == ADMIN_SECRET_KEY;
}

void AdminRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // ==========================================================
    // 1. SİSTEM İSTATİSTİKLERİ (Dashboard için)
    // ==========================================================
    CROW_ROUTE(app, "/api/admin/stats").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!isFromAdminUI(req)) return crow::response(403, "Bu endpoint sadece AdminUI uygulamasina aciktir.");
        if (!Security::checkAuth(req, db, true)) return crow::response(401);

        SystemStats stats = db.getSystemStats();
        crow::json::wvalue res;
        res["total_users"] = stats.total_users;
        res["total_servers"] = stats.total_servers;
        res["active_users"] = stats.active_users;
        res["total_messages"] = stats.total_messages;
        return crow::response(200, res);
            });

    // ==========================================================
    // 2. TÜM KULLANICILARI ÇEK
    // ==========================================================
    CROW_ROUTE(app, "/api/admin/users").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!isFromAdminUI(req)) return crow::response(403, "Sadece AdminUI erisebilir.");
        if (!Security::checkAuth(req, db, true)) return crow::response(401);

        auto users = db.getAllUsers();
        crow::json::wvalue res = crow::json::wvalue::list();
        for (size_t i = 0; i < users.size(); ++i) {
            res[i]["id"] = users[i].id;
            res[i]["name"] = users[i].name;
            res[i]["email"] = users[i].email;
            res[i]["status"] = users[i].status;
            res[i]["is_system_admin"] = users[i].isSystemAdmin;
            res[i]["subscription_level"] = users[i].subscriptionLevel;
        }
        return crow::response(200, res);
            });

    // ==========================================================
    // 3. TÜM SUNUCULARI ÇEK
    // ==========================================================
    CROW_ROUTE(app, "/api/admin/servers").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!isFromAdminUI(req)) return crow::response(403, "Sadece AdminUI erisebilir.");
        if (!Security::checkAuth(req, db, true)) return crow::response(401);

        auto servers = db.getAllServers();
        crow::json::wvalue res = crow::json::wvalue::list();
        for (size_t i = 0; i < servers.size(); ++i) {
            res[i]["id"] = servers[i].id;
            res[i]["name"] = servers[i].name;
            res[i]["owner_id"] = servers[i].owner_id;
            res[i]["member_count"] = servers[i].member_count;
        }
        return crow::response(200, res);
            });

    // ==========================================================
    // 4. YASAKLI (BAN) LİSTESİ VE İŞLEMLERİ
    // ==========================================================
    CROW_ROUTE(app, "/api/admin/banlist").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!isFromAdminUI(req)) return crow::response(403, "Sadece AdminUI erisebilir.");
        if (!Security::checkAuth(req, db, true)) return crow::response(401);

        auto bans = db.getBannedUsers();
        crow::json::wvalue res = crow::json::wvalue::list();
        for (size_t i = 0; i < bans.size(); ++i) {
            res[i]["user_id"] = bans[i].user_id;
            res[i]["reason"] = bans[i].reason;
            res[i]["date"] = bans[i].date;
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/admin/users/<string>/ban").methods("POST"_method)
        ([&db](const crow::request& req, std::string targetId) {
        if (!isFromAdminUI(req)) return crow::response(403, "Sadece AdminUI erisebilir.");
        if (!Security::checkAuth(req, db, true)) return crow::response(401);

        if (db.banUser(targetId, "Sistem Yoneticisi Tarafindan Engellendi")) {
            return crow::response(200, "Kullanici yasaklandi.");
        }
        return crow::response(500, "Islem basarisiz.");
            });

    CROW_ROUTE(app, "/api/admin/users/<string>/unban").methods("POST"_method)
        ([&db](const crow::request& req, std::string targetId) {
        if (!isFromAdminUI(req)) return crow::response(403, "Sadece AdminUI erisebilir.");
        if (!Security::checkAuth(req, db, true)) return crow::response(401);

        if (db.unbanUser(targetId)) {
            return crow::response(200, "Yasak kaldirildi.");
        }
        return crow::response(500, "Islem basarisiz.");
            });

    // ==========================================================
    // 5. DENETİM LOGLARI (AUDIT)
    // ==========================================================
    CROW_ROUTE(app, "/api/admin/logs").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!isFromAdminUI(req)) return crow::response(403, "Sadece AdminUI erisebilir.");
        if (!Security::checkAuth(req, db, true)) return crow::response(401);

        auto logs = db.getSystemLogs();
        crow::json::wvalue res = crow::json::wvalue::list();
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
}