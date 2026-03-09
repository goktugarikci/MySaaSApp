#include "ReportRoutes.h"
#include "../utils/Security.h"

void ReportRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // ==========================================================
        // KULLANICI: YENİ BİR ŞİKAYET (REPORT) OLUŞTURMA
        // ==========================================================
    CROW_ROUTE(app, "/api/reports").methods("POST"_method)
        ([&db](const crow::request& req) {

        // Normal kullanıcı yetkisi yeterlidir (false)
        if (!Security::checkAuth(req, db, false)) return crow::response(401);

        auto body = crow::json::load(req.body);
        if (!body || !body.has("content_id") || !body.has("type") || !body.has("reason")) {
            return crow::response(400, "Lutfen sikayet edilen icerigi (content_id), turunu (type) ve sebebini (reason) belirtin.");
        }

        std::string reporterId = Security::getUserIdFromHeader(req);
        std::string contentId = std::string(body["content_id"].s());
        std::string type = std::string(body["type"].s()); // Örn: "MESSAGE", "USER", "SERVER"
        std::string reason = std::string(body["reason"].s());

        if (db.createReport(reporterId, contentId, type, reason)) {
            return crow::response(201, "Sikayetiniz basariyla alindi ve adminlere iletildi.");
        }
        return crow::response(500, "Sikayet olusturulurken sunucu hatasi.");
            });

    // ==========================================================
    // 2. AÇIK ŞİKAYETLERİ LİSTELE (SADECE SÜPER ADMİN)
    // ==========================================================
    CROW_ROUTE(app, "/api/admin/reports").methods("GET"_method)
        ([&db](const crow::request& req) {
        // Dikkat: checkAuth'un 3. parametresini 'true' yolluyoruz, yani Admin Yetkisi ZORUNLU!
        if (!Security::checkAuth(req, db, true)) return crow::response(403, "Bu islem icin Super Admin yetkisi gereklidir.");

        auto reports = db.getOpenReports();
        crow::json::wvalue res;
        for (size_t i = 0; i < reports.size(); ++i) {
            res[i]["id"] = reports[i].id;
            res[i]["reporter_id"] = reports[i].reporter_id;
            res[i]["content_id"] = reports[i].content_id;
            res[i]["type"] = reports[i].type;
            res[i]["reason"] = reports[i].reason;
            res[i]["status"] = reports[i].status;
        }
        return crow::response(200, res);
            });

    // 9. ŞİKAYETİ ÇÖZÜLDÜ OLARAK İŞARETLE
    CROW_ROUTE(app, "/api/admin/reports/<string>").methods("PUT"_method)
        ([&db](const crow::request& req, std::string reportId) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        if (db.resolveReport(reportId)) {
            db.logAction(Security::getUserIdFromHeader(req), "RESOLVE_REPORT", reportId, "Admin bir sikayeti cozume kavusturdu.");
            return crow::response(200, "Sikayet cozuldu olarak isaretlendi.");
        }
        return crow::response(500);
            });
}