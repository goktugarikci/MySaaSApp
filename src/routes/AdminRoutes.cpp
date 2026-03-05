#include "AdminRoutes.h"
#include "../utils/Security.h"
#include <crow/middlewares/cors.h>
#include <vector> // Array işlemi için gerekli

void AdminRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // 1. İSTATİSTİKLER
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

    // 2. ARŞİVLENMİŞ MESAJLAR
    CROW_ROUTE(app, "/api/admin/archives")
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        auto archives = db.getArchivedMessages(100);

        std::vector<crow::json::wvalue> list;
        for (const auto& a : archives) {
            crow::json::wvalue obj;
            obj["id"] = a.id;
            obj["original_channel_id"] = a.original_channel_id;
            obj["sender_id"] = a.sender_id;
            obj["content"] = a.content;
            obj["deleted_at"] = a.deleted_at;
            list.push_back(std::move(obj));
        }
        return crow::response(200, crow::json::wvalue(list));
            });

    // 3. TÜM SUNUCULARI GETİR (DASHBOARD İÇİN)
    CROW_ROUTE(app, "/api/admin/servers").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        auto servers = db.getAllServers();

        std::vector<crow::json::wvalue> list;
        for (const auto& s : servers) {
            crow::json::wvalue obj;
            obj["id"] = s.id;
            obj["name"] = s.name;
            obj["owner_id"] = s.owner_id;
            obj["member_count"] = s.member_count;
            list.push_back(std::move(obj));
        }
        return crow::response(200, crow::json::wvalue(list));
            });

    // 4. SUNUCU DETAYLARI
    CROW_ROUTE(app, "/api/admin/servers/<string>/details")
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        crow::json::wvalue res;

        auto members = db.getServerMembersDetails(serverId);
        std::vector<crow::json::wvalue> membersList;
        for (const auto& m : members) {
            crow::json::wvalue obj;
            obj["id"] = m.id;
            obj["name"] = m.name;
            obj["status"] = m.status;
            membersList.push_back(std::move(obj));
        }
        res["members"] = std::move(membersList);

        auto logs = db.getServerLogs(serverId);
        std::vector<crow::json::wvalue> logsList;
        for (const auto& l : logs) {
            crow::json::wvalue obj;
            obj["time"] = l.created_at;
            obj["action"] = l.action;
            obj["details"] = l.details;
            logsList.push_back(std::move(obj));
        }
        res["logs"] = std::move(logsList);

        return crow::response(200, res);
            });

    // 5. SİSTEM LOGLARI
    CROW_ROUTE(app, "/api/admin/logs/system").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        auto logs = db.getSystemLogs(100);

        std::vector<crow::json::wvalue> list;
        for (const auto& l : logs) {
            crow::json::wvalue obj;
            obj["id"] = l.id;
            obj["action"] = l.action;
            obj["details"] = l.details;
            obj["created_at"] = l.created_at;
            list.push_back(std::move(obj));
        }
        return crow::response(200, crow::json::wvalue(list));
            });

    // 6. TÜM KULLANICILARI GETİR (DASHBOARD İÇİN - ASIL DÜZELTİLEN YER)
    CROW_ROUTE(app, "/api/admin/users").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        auto users = db.getAllUsers();

        std::vector<crow::json::wvalue> list;
        for (auto& u : users) {
            list.push_back(u.toJson());
        }
        return crow::response(200, crow::json::wvalue(list));
            });

    // 7. KULLANICIYI BANLA (YASAKLA) - GÜVENLİ VERSİYON
    CROW_ROUTE(app, "/api/admin/ban").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("user_id")) return crow::response(400);

        std::string targetId = std::string(x["user_id"].s());
        std::string adminId = Security::getUserIdFromHeader(req);

        // Yeni güvenli Ban fonksiyonunu çağırıyoruz
        if (db.banUser(targetId, "Sistem Yoneticisi Yasaklamasi")) {
            db.logAction(adminId, "BAN_USER", targetId, "Sistem yoneticisi bir kullaniciyi yasakladi.");
            return crow::response(200, "Kullanici yasaklandi. Verileri güvende.");
        }
        return crow::response(500, "Yasaklama isleminde hata.");
            });

    // 8. KULLANICI BANINI AÇ (UNBAN) - GÜVENLİ VERSİYON
    CROW_ROUTE(app, "/api/admin/unban").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("user_id")) return crow::response(400);

        std::string targetId = std::string(x["user_id"].s());
        std::string adminId = Security::getUserIdFromHeader(req);

        // Yeni güvenli Unban fonksiyonunu çağırıyoruz
        if (db.unbanUser(targetId)) {
            db.logAction(adminId, "UNBAN_USER", targetId, "Sistem yoneticisi kullanici yasagini kaldirildi.");
            return crow::response(200, "Yasak kaldirildi. Kullanici sistemine geri donebilir.");
        }
        return crow::response(500, "Yasak kaldirma isleminde hata.");
            });

    // 9. YASAKLI KULLANICILARI (BANLIST) GETİR
    CROW_ROUTE(app, "/api/admin/banlist").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);

        auto bans = db.getBannedUsers();
        std::vector<crow::json::wvalue> list;
        for (const auto& b : bans) {
            crow::json::wvalue obj;
            obj["user_id"] = b.user_id;
            obj["reason"] = b.reason;
            obj["date"] = b.date;
            list.push_back(std::move(obj));
        }
        return crow::response(200, crow::json::wvalue(list));
            });

    // 10. SISTEM LOGLARINI (AUDIT TRAIL) GETIR
    CROW_ROUTE(app, "/api/admin/logs").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);

        auto logs = db.getAuditLogs(300);
        std::vector<crow::json::wvalue> list;
        for (const auto& l : logs) {
            crow::json::wvalue obj;
            obj["id"] = l.id;
            obj["user_id"] = l.user_id;
            obj["action"] = l.action_type;
            obj["target"] = l.target_id;
            obj["details"] = l.details;
            obj["date"] = l.created_at;
            list.push_back(std::move(obj));
        }
        return crow::response(200, crow::json::wvalue(list));
            });

    // 11. ŞİKAYETİ ÇÖZÜLDÜ (KAPATILDI) OLARAK İŞARETLE
    CROW_ROUTE(app, "/api/admin/reports/<string>").methods("PUT"_method)
        ([&db](const crow::request& req, std::string reportId) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403); // Sadece admin
        if (db.resolveReport(reportId)) {
            db.logAction(Security::getUserIdFromHeader(req), "RESOLVE_REPORT", reportId, "Admin bir sikayeti cozume kavusturdu.");
            return crow::response(200, "Sikayet cozuldu olarak isaretlendi.");
        }
        return crow::response(500);
            });

    // KULLANICI ABONELİK (STATÜ) YÜKSELTME / DÜŞÜRME (MEVCUT INT MİMARİSİNE UYGUN)
    CROW_ROUTE(app, "/api/admin/users/<string>/subscription").methods("PUT"_method)
        ([&db](const crow::request& req, std::string targetUserId) {

        if (!Security::checkAuth(req, db, true)) return crow::response(403, "Bu islem icin Super Admin yetkisi gerekiyor.");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("level")) {
            return crow::response(400, "JSON formatinda 'level' (sayi) parametresi eksik. Orn: {\"level\": 1}");
        }

        // DÜZELTME: Artık string değil, int (sayı) olarak alıyoruz!
        int newLevel = body["level"].i();
        int days = body.has("days") ? body["days"].i() : 0;
        std::string adminId = Security::getUserIdFromHeader(req);

        // Sizin mevcut fonksiyonunuzu çağırıyoruz:
        if (db.updateUserSubscription(targetUserId, newLevel, days)) {

            std::string logMsg = "Kullanici aboneligi seviye " + std::to_string(newLevel) + " olarak guncellendi.";
            if (days > 0) logMsg += " (" + std::to_string(days) + " Gun Gecerli)";
            else logMsg += " (Suresiz / Kalici)";

            db.logAction(adminId, "UPDATE_SUBSCRIPTION", targetUserId, logMsg);
            return crow::response(200, logMsg);
        }

        return crow::response(500, "Veritabani isleminde hata olustu.");
            });

}
