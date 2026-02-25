#include "AdminRoutes.h"
#include "../utils/Security.h"

void AdminRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

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


    CROW_ROUTE(app, "/api/admin/archives")
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        std::vector<Message> archives = db.getArchivedMessages(100);
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

    // 2. TÜM SUNUCULARI GETİR (DASHBOARD İÇİN)
    CROW_ROUTE(app, "/api/admin/servers").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        auto servers = db.getAllServers();
        crow::json::wvalue res;
        for (size_t i = 0; i < servers.size(); ++i) {
            res[i]["id"] = servers[i].id;
            res[i]["name"] = servers[i].name;
            res[i]["owner_id"] = servers[i].owner_id;
            res[i]["member_count"] = servers[i].member_count;
        }
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
    // 3. SİSTEM LOGLARI
    CROW_ROUTE(app, "/api/admin/logs/system").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        auto logs = db.getSystemLogs(100);
        crow::json::wvalue res;
        for (size_t i = 0; i < logs.size(); ++i) {
            res[i]["id"] = logs[i].id;
            res[i]["action"] = logs[i].action;
            res[i]["details"] = logs[i].details;
            res[i]["created_at"] = logs[i].created_at;
        }
        return crow::response(200, res);
            });
    // 1. TÜM KULLANICILARI GETİR (DASHBOARD İÇİN)
    CROW_ROUTE(app, "/api/admin/users").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        auto users = db.getAllUsers();
        crow::json::wvalue res;
        for (size_t i = 0; i < users.size(); ++i) {
            res[i] = users[i].toJson();
        }
        return crow::response(200, res);
            });
    // 4. KULLANICIYI BANLA (YASAKLA)
    CROW_ROUTE(app, "/api/admin/ban").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("user_id")) return crow::response(400);

        std::string targetId = std::string(x["user_id"].s());
        std::string adminId = Security::getUserIdFromHeader(req);

        db.executeQuery("CREATE TABLE IF NOT EXISTS banned_users (user_id TEXT PRIMARY KEY, reason TEXT, date DATETIME DEFAULT CURRENT_TIMESTAMP);");
        std::string sql = "INSERT OR REPLACE INTO banned_users (user_id, reason) VALUES ('" + targetId + "', 'Sistem Yoneticisi Yasaklamasi');";

        if (db.executeQuery(sql)) {
            db.updateUserStatus(targetId, "Banned");
            // BAN LOGU
            db.logAction(adminId, "BAN_USER", targetId, "Sistem yoneticisi bir kullaniciyi yasakladi.");
            return crow::response(200, "Kullanici yasaklandi.");
        }
        return crow::response(500);
            });

    // 5. KULLANICI BANINI AÇ (UNBAN)
    CROW_ROUTE(app, "/api/admin/unban").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("user_id")) return crow::response(400);

        std::string targetId = std::string(x["user_id"].s());
        std::string adminId = Security::getUserIdFromHeader(req);

        db.executeQuery("DELETE FROM banned_users WHERE user_id = '" + targetId + "';");
        db.updateUserStatus(targetId, "Offline");

        // UNBAN LOGU
        db.logAction(adminId, "UNBAN_USER", targetId, "Sistem yoneticisi kullanici yasagini kaldirildi.");
        return crow::response(200, "Yasak kaldirildi.");
            });

    // 6. YASAKLI KULLANICILARI (BANLIST) GETİR
    CROW_ROUTE(app, "/api/admin/banlist").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);

        auto bans = db.getBannedUsers();
        crow::json::wvalue res;
        for (size_t i = 0; i < bans.size(); ++i) {
            res[i]["user_id"] = bans[i].user_id;
            res[i]["reason"] = bans[i].reason;
            res[i]["date"] = bans[i].date;
        }
        return crow::response(200, res);
            });
    // 7. SISTEM LOGLARINI (AUDIT TRAIL) GETIR
    CROW_ROUTE(app, "/api/admin/logs").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);

        // FONKSİYON İSMİ BURADA DA DEĞİŞTİRİLDİ:
        auto logs = db.getAuditLogs(300);

        crow::json::wvalue res;
        for (size_t i = 0; i < logs.size(); ++i) {
            res[i]["id"] = logs[i].id;
            res[i]["user_id"] = logs[i].user_id;
            res[i]["action"] = logs[i].action_type;
            res[i]["target"] = logs[i].target_id;
            res[i]["details"] = logs[i].details;
            res[i]["date"] = logs[i].created_at;
        }
        return crow::response(200, res);
            });

    // ŞİKAYETİ ÇÖZÜLDÜ (KAPATILDI) OLARAK İŞARETLE
    CROW_ROUTE(app, "/api/admin/reports/<string>").methods("PUT"_method)
        ([&db](const crow::request& req, std::string reportId) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403); // Sadece admin
        if (db.resolveReport(reportId)) {
            db.logAction(Security::getUserIdFromHeader(req), "RESOLVE_REPORT", reportId, "Admin bir sikayeti cozume kavusturdu.");
            return crow::response(200, "Sikayet cozuldu olarak isaretlendi.");
        }
        return crow::response(500);
            });

}