#include "KanbanRoutes.h"
#include "../utils/Security.h"
#include <crow/json.h>

void KanbanRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // =============================================================
    // API: KANBAN PANOSUNU VE KARTLARI GETİR (GET /api/channels/<id>/kanban)
    // =============================================================
    CROW_ROUTE(app, "/api/channels/<string>/kanban").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto board = db.getKanbanBoard(channelId);

        crow::json::wvalue res;
        for (size_t i = 0; i < board.size(); ++i) {
            res[i]["id"] = board[i].id;
            res[i]["title"] = board[i].title;
            res[i]["position"] = board[i].position;

            for (size_t j = 0; j < board[i].cards.size(); ++j) {
                res[i]["cards"][j]["id"] = board[i].cards[j].id;
                res[i]["cards"][j]["title"] = board[i].cards[j].title;
                res[i]["cards"][j]["description"] = board[i].cards[j].description;
                res[i]["cards"][j]["priority"] = board[i].cards[j].priority;
                res[i]["cards"][j]["position"] = board[i].cards[j].position;
            }
        }
        return crow::response(200, res);
            });

    // =============================================================
    // API: YENİ KANBAN LİSTESİ OLUŞTUR (POST /api/channels/<id>/kanban/lists)
    // =============================================================
    CROW_ROUTE(app, "/api/channels/<string>/kanban/lists").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("title")) return crow::response(400, "Liste basligi (title) gerekli");

        std::string title = body["title"].s();

        if (db.createKanbanList(channelId, title)) {
            return crow::response(201, "Liste basariyla olusturuldu");
        }
        return crow::response(500, "Liste olusturulamadi");
            });

    // =============================================================
    // API: KANBAN LİSTESİNİ GÜNCELLE (PUT /api/kanban/lists/<id>)
    // =============================================================
    CROW_ROUTE(app, "/api/kanban/lists/<string>").methods(crow::HTTPMethod::PUT)
        ([&db](const crow::request& req, std::string listId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("title") || !body.has("position")) return crow::response(400, "Eksik parametre (title, position)");

        std::string title = body["title"].s();
        int position = body["position"].i();

        if (db.updateKanbanList(listId, title, position)) {
            return crow::response(200, "Liste guncellendi");
        }
        return crow::response(500, "Liste guncellenemedi");
            });

    // =============================================================
    // API: KANBAN LİSTESİNİ SİL (DELETE /api/kanban/lists/<id>)
    // =============================================================
    CROW_ROUTE(app, "/api/kanban/lists/<string>").methods(crow::HTTPMethod::DELETE)
        ([&db](const crow::request& req, std::string listId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        if (db.deleteKanbanList(listId)) {
            return crow::response(200, "Liste ve icindeki tüm kartlar silindi");
        }
        return crow::response(500, "Liste silinemedi");
            });

    // =============================================================
    // API: LİSTEYE YENİ KART EKLEME (POST /api/kanban/lists/<id>/cards)
    // =============================================================
    CROW_ROUTE(app, "/api/kanban/lists/<string>/cards").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req, std::string listId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("title")) return crow::response(400, "Kart basligi (title) gerekli");

        std::string title = body["title"].s();
        std::string desc = body.has("description") ? body["description"].s() : "";
        int priority = body.has("priority") ? body["priority"].i() : 0; // 0: Dusuk, 1: Orta, 2: Yuksek

        if (db.createKanbanCard(listId, title, desc, priority)) {
            return crow::response(201, "Kart basariyla olusturuldu");
        }
        return crow::response(500, "Kart olusturulamadi");
            });

    // =============================================================
    // API: KARTI GÜNCELLE (PUT /api/kanban/cards/<id>)
    // =============================================================
    CROW_ROUTE(app, "/api/kanban/cards/<string>").methods(crow::HTTPMethod::PUT)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("title")) return crow::response(400, "Eksik parametre");

        std::string title = body["title"].s();
        std::string desc = body.has("description") ? body["description"].s() : "";
        int priority = body.has("priority") ? body["priority"].i() : 0;

        if (db.updateKanbanCard(cardId, title, desc, priority)) {
            return crow::response(200, "Kart guncellendi");
        }
        return crow::response(500, "Kart guncellenemedi");
            });

    // =============================================================
    // API: KARTI SİL (DELETE /api/kanban/cards/<id>)
    // =============================================================
    CROW_ROUTE(app, "/api/kanban/cards/<string>").methods(crow::HTTPMethod::DELETE)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        if (db.deleteKanbanCard(cardId)) {
            return crow::response(200, "Kart silindi");
        }
        return crow::response(500, "Kart silinemedi");
            });

    // =============================================================
    // API: KARTI BAŞKA BİR LİSTEYE VEYA SIRAYA TAŞI (POST /api/kanban/cards/<id>/move)
    // Not: Bu islem WebSocket uzerinden de yapilir ama REST karsiligi da bulunmalidir.
    // =============================================================
    CROW_ROUTE(app, "/api/kanban/cards/<string>/move").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("new_list_id") || !body.has("new_position")) {
            return crow::response(400, "Eksik parametre (new_list_id, new_position)");
        }

        std::string newListId = body["new_list_id"].s();
        int newPosition = body["new_position"].i();

        if (db.moveCard(cardId, newListId, newPosition)) {
            return crow::response(200, "Kart tasindi");
        }
        return crow::response(500, "Kart tasinamadi");
            });
}