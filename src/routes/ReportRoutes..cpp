#include "ReportRoutes.h"
#include "../utils/Security.h"

void ReportRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // ==========================================================
    // 1. KULLANICI ŞİKAYET OLUŞTURMA
    // ==========================================================
    CROW_ROUTE(app, "/api/reports").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string reporterId = Security::getUserIdFromHeader(req);

        auto x = crow::json::load(req.body);
        // content_id: Şikayet edilen mesajın/kullanıcının ID'si
        // type: "MESSAGE", "USER", "SERVER" gibi türler
        // reason: Şikayet sebebi
        if (!x || !x.has("content_id") || !x.has("type") || !x.has("reason")) return crow::response(400);

        if (db.createReport(reporterId, std::string(x["content_id"].s()), std::string(x["type"].s()), std::string(x["reason"].s()))) {
            return crow::response(201, "Sikayetiniz basariyla alindi. Yoneticiler tarafindan incelenecektir.");
        }
        return crow::response(500);
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

    // ==========================================================
    // 3. ŞİKAYETİ ÇÖZÜLDÜ OLARAK İŞARETLE (SADECE SÜPER ADMİN)
    // ==========================================================
    CROW_ROUTE(app, "/api/admin/reports/<string>/resolve").methods("PUT"_method)
        ([&db](const crow::request& req, std::string reportId) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);

        if (db.resolveReport(reportId)) {
            return crow::response(200, "Sikayet cozumlendi olarak isaretlendi.");
        }
        return crow::response(500);
            });
}