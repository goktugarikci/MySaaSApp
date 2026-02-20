#include "KanbanRoutes.h"
#include "../utils/Security.h"
#include <crow/json.h>

// WINDOWS MAKRO ÇAKIŞMASI ÇÖZÜMÜ
#ifdef _WIN32
#undef DELETE
#endif

void KanbanRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {
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

    CROW_ROUTE(app, "/api/kanban/lists/<string>").methods(crow::HTTPMethod::PUT)
        ([&db](const crow::request& req, std::string listId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("title") || !body.has("position")) return crow::response(400, "Eksik parametre");

        std::string title = body["title"].s();
        int position = body["position"].i();

        if (db.updateKanbanList(listId, title, position)) {
            return crow::response(200, "Liste guncellendi");
        }
        return crow::response(500, "Liste guncellenemedi");
            });

    CROW_ROUTE(app, "/api/kanban/lists/<string>").methods(crow::HTTPMethod::DELETE)
        ([&db](const crow::request& req, std::string listId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        if (db.deleteKanbanList(listId)) {
            return crow::response(200, "Liste ve icindeki tüm kartlar silindi");
        }
        return crow::response(500, "Liste silinemedi");
            });

    CROW_ROUTE(app, "/api/kanban/lists/<string>/cards").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req, std::string listId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("title")) return crow::response(400, "Kart basligi (title) gerekli");

        std::string title = body["title"].s();
        std::string desc = body.has("description") ? std::string(body["description"].s()) : std::string("");
        int priority = body.has("priority") ? body["priority"].i() : 0;

        if (db.createKanbanCard(listId, title, desc, priority)) {
            return crow::response(201, "Kart basariyla olusturuldu");
        }
        return crow::response(500, "Kart olusturulamadi");
            });

    CROW_ROUTE(app, "/api/kanban/cards/<string>").methods(crow::HTTPMethod::PUT)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("title")) return crow::response(400, "Eksik parametre");

        std::string title = body["title"].s();
        std::string desc = body.has("description") ? std::string(body["description"].s()) : std::string("");
        int priority = body.has("priority") ? body["priority"].i() : 0;

        if (db.updateKanbanCard(cardId, title, desc, priority)) {
            return crow::response(200, "Kart guncellendi");
        }
        return crow::response(500, "Kart guncellenemedi");
            });

    CROW_ROUTE(app, "/api/kanban/cards/<string>").methods(crow::HTTPMethod::DELETE)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        if (db.deleteKanbanCard(cardId)) {
            return crow::response(200, "Kart silindi");
        }
        return crow::response(500, "Kart silinemedi");
            });

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

    CROW_ROUTE(app, "/api/kanban/cards/<string>/comments").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto comments = db.getCardComments(cardId);

        crow::json::wvalue res;
        for (size_t i = 0; i < comments.size(); ++i) {
            res[i]["id"] = comments[i].id;
            res[i]["sender_id"] = comments[i].sender_id;
            res[i]["sender_name"] = comments[i].sender_name;
            res[i]["content"] = comments[i].content;
            res[i]["timestamp"] = comments[i].timestamp;
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/kanban/cards/<string>/comments").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("content")) return crow::response(400, "Yorum icerigi gerekli");

        std::string userId = Security::getUserIdFromHeader(&req);
        std::string content = body["content"].s();

        if (db.addCardComment(cardId, userId, content)) {
            return crow::response(201, "Yorum eklendi");
        }
        return crow::response(500, "Yorum eklenemedi");
            });

    CROW_ROUTE(app, "/api/kanban/comments/<string>").methods(crow::HTTPMethod::DELETE)
        ([&db](const crow::request& req, std::string commentId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        std::string userId = Security::getUserIdFromHeader(&req);

        if (db.deleteCardComment(commentId, userId)) {
            return crow::response(200, "Yorum silindi");
        }
        return crow::response(403, "Bu yorumu silmeye yetkiniz yok veya yorum bulunamadi");
            });

    CROW_ROUTE(app, "/api/kanban/cards/<string>/tags").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto tags = db.getCardTags(cardId);

        crow::json::wvalue res;
        for (size_t i = 0; i < tags.size(); ++i) {
            res[i]["id"] = tags[i].id;
            res[i]["tag_name"] = tags[i].tag_name;
            res[i]["color"] = tags[i].color;
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/kanban/cards/<string>/tags").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("tag_name") || !body.has("color")) {
            return crow::response(400, "Eksik parametre");
        }

        std::string tagName = body["tag_name"].s();
        std::string color = body["color"].s();

        if (db.addCardTag(cardId, tagName, color)) {
            return crow::response(201, "Etiket eklendi");
        }
        return crow::response(500, "Etiket eklenemedi");
            });

    CROW_ROUTE(app, "/api/kanban/tags/<string>").methods(crow::HTTPMethod::DELETE)
        ([&db](const crow::request& req, std::string tagId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        if (db.removeCardTag(tagId)) {
            return crow::response(200, "Etiket kaldirildi");
        }
        return crow::response(500, "Etiket kaldirilamadi");
            });
}