#include "MessageRoutes.h"
#include "../utils/Security.h"
#include <crow/json.h>

void MessageRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // =============================================================
    // API: KANAL MESAJLARINI GETİR (GET /api/channels/<id>/messages)
    // =============================================================
    CROW_ROUTE(app, "/api/channels/<string>/messages").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        // İsteğe bağlı limit parametresi (Varsayılan 50)
        char* limitParam = req.url_params.get("limit");
        int limit = limitParam ? std::stoi(limitParam) : 50;

        auto messages = db.getChannelMessages(channelId, limit);

        crow::json::wvalue res;
        for (size_t i = 0; i < messages.size(); ++i) {
            res[i]["id"] = messages[i].id;
            res[i]["sender_id"] = messages[i].senderId;
            res[i]["sender_name"] = messages[i].senderName;
            res[i]["sender_avatar"] = messages[i].senderAvatar;
            res[i]["content"] = messages[i].content;
            res[i]["attachment_url"] = messages[i].attachmentUrl;
            res[i]["timestamp"] = messages[i].timestamp;
        }
        return crow::response(200, res);
            });

    // =============================================================
    // API: KANALA MESAJ GÖNDER (REST Üzerinden) (POST /api/channels/<id>/messages)
    // Not: Normal kullanıcılar WebSocket kullanır ama botlar veya 
    // entegrasyonlar için REST API ucu da bulunmalıdır.
    // =============================================================
    CROW_ROUTE(app, "/api/channels/<string>/messages").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("content")) return crow::response(400, "Mesaj icerigi (content) gerekli");

        std::string userId = Security::getUserIdFromHeader(&req);
        std::string content = body["content"].s();
        std::string attachmentUrl = body.has("attachment_url") ? body["attachment_url"].s() : "";

        if (db.sendMessage(channelId, userId, content, attachmentUrl)) {
            // Not: İdeal sistemde buraya Redis veya benzeri bir Pub/Sub eklenerek 
            // REST'ten gelen mesajın WebSocket abonelerine de anında iletilmesi sağlanabilir.
            return crow::response(201, "Mesaj gonderildi");
        }
        return crow::response(500, "Mesaj gonderilemedi");
            });

    // =============================================================
    // API: MESAJI GÜNCELLE/DÜZENLE (PUT /api/messages/<id>)
    // =============================================================
    CROW_ROUTE(app, "/api/messages/<string>").methods(crow::HTTPMethod::PUT)
        ([&db](const crow::request& req, std::string messageId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("content")) return crow::response(400, "Yeni mesaj icerigi (content) gerekli");

        std::string newContent = body["content"].s();

        if (db.updateMessage(messageId, newContent)) {
            return crow::response(200, "Mesaj guncellendi");
        }
        return crow::response(500, "Mesaj guncellenemedi");
            });

    // =============================================================
    // API: MESAJI SİL (DELETE /api/messages/<id>)
    // =============================================================
    CROW_ROUTE(app, "/api/messages/<string>").methods(crow::HTTPMethod::DELETE)
        ([&db](const crow::request& req, std::string messageId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        if (db.deleteMessage(messageId)) {
            return crow::response(200, "Mesaj silindi");
        }
        return crow::response(500, "Mesaj silinemedi");
            });

    // =============================================================
    // API: ÖZEL MESAJ (DM) KANALI OLUŞTUR VEYA GETİR (POST /api/users/dm)
    // =============================================================
    CROW_ROUTE(app, "/api/users/dm").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("target_user_id")) return crow::response(400, "Hedef kullanici (target_user_id) gerekli");

        std::string myId = Security::getUserIdFromHeader(&req);
        std::string targetId = body["target_user_id"].s();

        if (myId == targetId) return crow::response(400, "Kendinizle DM baslatamazsiniz");

        std::string dmChannelId = db.getOrCreateDMChannel(myId, targetId);

        if (!dmChannelId.empty()) {
            crow::json::wvalue res;
            res["channel_id"] = dmChannelId;
            res["message"] = "DM kanali hazir";
            return crow::response(200, res);
        }
        return crow::response(500, "DM kanali olusturulamadi");
            });
}