#include "AdminRoutes.h"
#include "../utils/Security.h"
#include <crow/json.h>

void AdminRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // =============================================================
    // API (ADMIN): SİSTEM LOGLARINI GETİR (GET /api/admin/logs/system)
    // Sadece Süper Adminler görebilir. Günlük (Daily) veya genel loglar.
    // =============================================================
    CROW_ROUTE(app, "/api/admin/logs/system").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req) {
        // 'true' parametresi, bu rotanın sadece Süper Adminlere açık olduğunu belirtir
        if (!Security::checkAuth(&req, &db, true)) return crow::response(403, "Yetkisiz Erisim: Sadece Super Adminler");

        // Not: DatabaseManager içine eklenecek getSystemLogs() fonksiyonu çağrılacak
        // auto logs = db.getSystemLogs(limit, dateFilter);

        crow::json::wvalue res;
        res[0]["id"] = 1;
        res[0]["level"] = "INFO";
        res[0]["action"] = "SERVER_START";
        res[0]["details"] = "Sistem basariyla baslatildi.";
        res[0]["created_at"] = "2026-02-20 15:00:00";
        // ... (Gerçek veriler DB'den gelecek)

        return crow::response(200, res);
            });

    // =============================================================
    // API (ADMIN): SUNUCU ÖZEL LOGLARINI GETİR (GET /api/admin/logs/servers/<id>)
    // =============================================================
    CROW_ROUTE(app, "/api/admin/logs/servers/<string>").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(&req, &db, true)) return crow::response(403, "Yetkisiz Erisim: Sadece Super Adminler");

        auto logs = db.getServerLogs(serverId);

        crow::json::wvalue res;
        for (size_t i = 0; i < logs.size(); ++i) {
            res[i]["created_at"] = logs[i].createdAt;
            res[i]["action"] = logs[i].action;
            res[i]["details"] = logs[i].details;
        }
        return crow::response(200, res);
            });

    // =============================================================
    // API (ADMIN): ARŞİVLENMİŞ / SİLİNMİŞ ESKİ MESAJLARI GETİR 
    // =============================================================
    CROW_ROUTE(app, "/api/admin/archive/messages").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(&req, &db, true)) return crow::response(403, "Yetkisiz Erisim: Sadece Super Adminler");

        // Not: DatabaseManager'da "ArchivedMessages" tablosundan verileri çekecek fonksiyon eklenecek.
        // Silinen mesajlar tamamen yok olmak yerine bu tabloda audit (denetim) için tutulacak.

        crow::json::wvalue res;
        res["message"] = "Arsivlenmis mesajlar modulu hazirlaniyor...";
        return crow::response(200, res);
            });

    // =============================================================
    // API (ADMIN): KULLANICI DENETİMİ VE GECMİŞİ (GET /api/admin/users/<id>/audit)
    // Bir kullanıcının yaptığı tüm kritik işlemleri listeler.
    // =============================================================
    CROW_ROUTE(app, "/api/admin/users/<string>/audit").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req, std::string targetUserId) {
        if (!Security::checkAuth(&req, &db, true)) return crow::response(403, "Yetkisiz Erisim: Sadece Super Adminler");

        crow::json::wvalue res;
        res["user_id"] = targetUserId;
        res["audit_trail"] = "Kullanici islem gecmisi (Login, Sunucu kurma, Odeme vb.) burada listelenecek.";

        return crow::response(200, res);
            });

    // =============================================================
    // API (ADMIN): KULLANICININ SUNUCU VE ROL BİLGİLERİNİ GETİR
    // Hangi sunucuda, rolü ne, enterprise mı, sunucu kaç kişi?
    // =============================================================
    CROW_ROUTE(app, "/api/admin/users/<string>/servers").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req, std::string targetUserId) {
        if (!Security::checkAuth(&req, &db, true)) return crow::response(403, "Yetkisiz Erisim: Sadece Super Adminler");

        // Kullanıcının bulunduğu tüm sunucuları çek
        auto servers = db.getUserServers(targetUserId);

        // Kullanıcının abonelik/profil detaylarını çek
        auto userOpt = db.getUserById(targetUserId);

        crow::json::wvalue res;

        if (userOpt) {
            res["user"]["id"] = userOpt->id;
            res["user"]["name"] = userOpt->name;
            res["user"]["status"] = userOpt->status; // Online, Offline
            res["user"]["subscription_level"] = userOpt->subscriptionLevel; // 0: Normal, 1: Premium, 2: Enterprise vb.
            res["user"]["is_enterprise"] = (userOpt->subscriptionLevel > 0);
        }

        for (size_t i = 0; i < servers.size(); ++i) {
            res["servers"][i]["server_id"] = servers[i].id;
            res["servers"][i]["server_name"] = servers[i].name;
            res["servers"][i]["member_count"] = servers[i].memberCount;

            // Eğer sunucunun OwnerID'si bu kullanıcıya eşitse rolü "Owner" yap
            bool isOwner = (servers[i].ownerId == targetUserId);
            res["servers"][i]["is_owner"] = isOwner;
            res["servers"][i]["role"] = isOwner ? "Owner" : "Member";

            // Not: İleride `DatabaseManager::getServerRoles` ile "Admin", "Moderator" gibi alt roller de eklenebilir.
        }

        return crow::response(200, res);
            });

    // =============================================================
    // API (ADMIN): SUNUCU DETAYLI ÜYE VE DURUM DENETİMİ
    // Sunucudaki tüm üyeler kim, hangileri online, rolleri neler?
    // =============================================================
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
            res["members"][i]["status"] = members[i].status; // Online, Offline, Away

            bool isOwner = (serverOpt && serverOpt->ownerId == members[i].id);
            res["members"][i]["is_owner"] = isOwner;
            res["members"][i]["role"] = isOwner ? "Owner" : "Member";
        }

        return crow::response(200, res);
            });
}