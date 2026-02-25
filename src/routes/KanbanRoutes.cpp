#include "KanbanRoutes.h"
#include "../utils/Security.h"

void KanbanRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // ==========================================================
    // KANBAN PANO VE LİSTE İŞLEMLERİ
    // ==========================================================

    CROW_ROUTE(app, "/api/boards/<string>")
        ([&db](const crow::request& req, std::string chId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto board = db.getKanbanBoard(chId);
        crow::json::wvalue res;
        for (size_t i = 0; i < board.size(); i++) {
            res[i] = board[i].toJson();
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/boards/<string>/lists").methods("POST"_method)
        ([&db](const crow::request& req, std::string chId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("title")) return crow::response(400);

        if (db.createKanbanList(chId, std::string(x["title"].s()))) return crow::response(201, "Liste olusturuldu.");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/lists/<string>").methods("PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string listId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("title") || !x.has("position")) return crow::response(400);
            if (db.updateKanbanList(listId, std::string(x["title"].s()), x["position"].i())) return crow::response(200);
        }
        else {
            if (db.deleteKanbanList(listId)) return crow::response(200, "Liste silindi.");
        }
        return crow::response(500);
            });

    // ==========================================================
    // KART (GÖREV) İŞLEMLERİ
    // ==========================================================

    CROW_ROUTE(app, "/api/lists/<string>/cards").methods("POST"_method)
        ([&db](const crow::request& req, std::string listId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("title") || !x.has("description") || !x.has("priority")) return crow::response(400);

        std::string assigneeId = x.has("assignee_id") ? std::string(x["assignee_id"].s()) : "";
        std::string attachmentUrl = x.has("attachment_url") ? std::string(x["attachment_url"].s()) : "";
        std::string dueDate = x.has("due_date") ? std::string(x["due_date"].s()) : "";

        if (db.createKanbanCard(listId, std::string(x["title"].s()), std::string(x["description"].s()), x["priority"].i(), assigneeId, attachmentUrl, dueDate)) {
            return crow::response(201, "Görev karti basariyla olusturuldu.");
        }
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/cards/<string>").methods("PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("title") || !x.has("description") || !x.has("priority")) return crow::response(400);
            if (db.updateKanbanCard(cardId, std::string(x["title"].s()), std::string(x["description"].s()), x["priority"].i())) return crow::response(200);
        }
        else {
            if (db.deleteKanbanCard(cardId)) return crow::response(200, "Kart silindi.");
        }
        return crow::response(500);
            });

    // SÜRÜKLE BIRAK (MOVE) - SADECE BİR TANE VAR
    CROW_ROUTE(app, "/api/cards/<string>/move").methods("PUT"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("new_list_id") || !x.has("new_position")) return crow::response(400);

        if (db.moveCard(cardId, std::string(x["new_list_id"].s()), x["new_position"].i())) return crow::response(200, "Kartin sirasi/listesi güncellendi.");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/cards/<string>/assign").methods("PUT"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("assignee_id")) return crow::response(400);

        std::string assigneeId = std::string(x["assignee_id"].s());
        std::string serverId = db.getServerIdByCardId(cardId);

        if (!db.isUserInServer(serverId, assigneeId)) return crow::response(400, "Sadece sunucudaki kisiler atanabilir.");

        if (db.assignUserToCard(cardId, assigneeId)) return crow::response(200, "Kisi atandi.");
        return crow::response(500);
            });

    // TAMAMLANMA (STATUS) - SADECE BİR TANE VAR
    CROW_ROUTE(app, "/api/cards/<string>/status").methods("PUT"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("is_completed")) return crow::response(400);

        bool isCompleted = false;
        if (x["is_completed"].t() == crow::json::type::True) isCompleted = true;
        else if (x["is_completed"].t() == crow::json::type::Number) isCompleted = (x["is_completed"].i() == 1);
        else if (x["is_completed"].t() == crow::json::type::String && std::string(x["is_completed"].s()) == "true") isCompleted = true;

        if (db.updateCardCompletion(cardId, isCompleted)) return crow::response(200, "Kart durumu guncellendi.");
        return crow::response(500);
            });

    // ==========================================================
    // KART YORUMLARI VE ETİKETLER
    // ==========================================================
    CROW_ROUTE(app, "/api/cards/<string>/comments").methods("GET"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::vector<CardComment> comments = db.getCardComments(cardId);
        crow::json::wvalue res;
        for (size_t i = 0; i < comments.size(); ++i) {
            res[i]["id"] = comments[i].id;
            res[i]["card_id"] = comments[i].card_id;
            res[i]["sender_id"] = comments[i].sender_id;
            res[i]["sender_name"] = comments[i].sender_name;
            res[i]["content"] = comments[i].content;
            res[i]["timestamp"] = comments[i].timestamp;
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/cards/<string>/comments").methods("POST"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("content")) return crow::response(400);
        std::string userId = Security::getUserIdFromHeader(req);
        if (db.addCardComment(cardId, userId, std::string(x["content"].s()))) return crow::response(201, "Yorum eklendi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/comments/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string commentId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);
        if (db.deleteCardComment(commentId, userId)) return crow::response(200, "Yorum silindi.");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/cards/<string>/tags").methods("GET"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::vector<CardTag> tags = db.getCardTags(cardId);
        crow::json::wvalue res;
        for (size_t i = 0; i < tags.size(); ++i) {
            res[i]["id"] = tags[i].id;
            res[i]["tag_name"] = tags[i].tag_name;
            res[i]["color"] = tags[i].color;
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/cards/<string>/tags").methods("POST"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("tag_name") || !x.has("color")) return crow::response(400);
        if (db.addCardTag(cardId, std::string(x["tag_name"].s()), std::string(x["color"].s()))) return crow::response(201, "Etiket eklendi.");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/tags/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string tagId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        if (db.removeCardTag(tagId)) return crow::response(200, "Etiket kaldirildi.");
        return crow::response(500);
            });

    // ==========================================================
    // 4. KANBAN EKSTRALAR - V2.0
    // ==========================================================
    CROW_ROUTE(app, "/api/cards/<string>/deadline").methods("PUT"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("date")) return crow::response(400);

        if (db.setCardDeadline(cardId, std::string(x["date"].s()))) {
            db.logAction(Security::getUserIdFromHeader(req), "SET_DEADLINE", cardId, "Goreve bitis tarihi eklendi.");
            return crow::response(200);
        }
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/cards/<string>/labels").methods("POST"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);

        if (db.addCardLabel(cardId, std::string(x["text"].s()), std::string(x["color"].s()))) {
            return crow::response(201, "Etiket eklendi.");
        }
        return crow::response(500);
            });
    // ==========================================================
    // V3.0 - AŞAMA 4: KANBAN ALT GÖREVLER (CHECKLIST) VE GEÇMİŞ
    // ==========================================================

    // KARTA ALT GÖREV (CHECKLIST) EKLEME VE LİSTELEME
    CROW_ROUTE(app, "/api/cards/<string>/checklists").methods("GET"_method, "POST"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (req.method == "GET"_method) {
            auto items = db.getCardChecklist(cardId);
            crow::json::wvalue res;
            for (size_t i = 0; i < items.size(); ++i) {
                res[i]["id"] = items[i].id;
                res[i]["content"] = items[i].content;
                res[i]["is_completed"] = items[i].is_completed;
            }
            return crow::response(200, res);
        }
        else {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("content")) return crow::response(400);

            std::string itemId = db.addChecklistItem(cardId, std::string(x["content"].s()));
            if (!itemId.empty()) {
                db.logCardActivity(cardId, myId, "Karta yeni bir alt gorev ekledi.");
                crow::json::wvalue res; res["item_id"] = itemId;
                return crow::response(201, res);
            }
            return crow::response(500);
        }
            });

    // ALT GÖREVİN DURUMUNU DEĞİŞTİRME (TİKLEME)
    CROW_ROUTE(app, "/api/checklists/<string>/toggle").methods("PUT"_method)
        ([&db](const crow::request& req, std::string itemId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("is_completed")) return crow::response(400);

        if (db.toggleChecklistItem(itemId, x["is_completed"].b())) {
            return crow::response(200, "Alt gorev durumu guncellendi.");
        }
        return crow::response(500);
            });

    // KARTIN GEÇMİŞ AKTİVİTELERİNİ (LOGLARINI) GETİRME
    CROW_ROUTE(app, "/api/cards/<string>/activity").methods("GET"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        auto activities = db.getCardActivity(cardId);
        crow::json::wvalue res;
        for (size_t i = 0; i < activities.size(); ++i) {
            res[i]["id"] = activities[i].id;
            res[i]["user_name"] = activities[i].user_name;
            res[i]["action"] = activities[i].action;
            res[i]["timestamp"] = activities[i].timestamp;
        }
        return crow::response(200, res);
            });

}