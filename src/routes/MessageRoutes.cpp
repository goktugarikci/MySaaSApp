#include "MessageRoutes.h"
#include "../utils/Security.h"

void MessageRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // ==========================================================
    // 1. KANAL MESAJLARINI GETİR
    // ==========================================================
    CROW_ROUTE(app, "/api/channels/<string>/messages").methods("GET"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        std::vector<Message> messages = db.getChannelMessages(channelId, 50);
        crow::json::wvalue res;
        for (size_t i = 0; i < messages.size(); ++i) {
            res[i] = messages[i].toJson();
        }
        return crow::response(200, res);
            });

    // ==========================================================
    // 2. KANALA MESAJ GÖNDER (EK DOSYA DESTEKLİ)
    // ==========================================================
    CROW_ROUTE(app, "/api/channels/<string>/messages").methods("POST"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("content")) return crow::response(400);

        std::string attachmentUrl = x.has("attachment_url") ? std::string(x["attachment_url"].s()) : "";
        std::string userId = Security::getUserIdFromHeader(req);

        if (db.sendMessage(channelId, userId, std::string(x["content"].s()), attachmentUrl)) {
            return crow::response(201, "Mesaj gonderildi.");
        }
        return crow::response(500);
            });

    // ==========================================================
    // 3. MESAJI DÜZENLE
    // ==========================================================
    CROW_ROUTE(app, "/api/messages/<string>").methods("PUT"_method)
        ([&db](const crow::request& req, std::string messageId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("content")) return crow::response(400);

        if (db.updateMessage(messageId, std::string(x["content"].s()))) {
            return crow::response(200, "Mesaj guncellendi.");
        }
        return crow::response(500);
            });

    // ==========================================================
    // 4. MESAJI SİL (ÇATIŞMA ÇÖZÜLDÜ - SADECE GÜVENLİ OLAN KALDI)
    // ==========================================================
    CROW_ROUTE(app, "/api/messages/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string msgId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        // Sadece yetkili kişi (mesajın sahibi) silebilir
        if (db.deleteMessage(msgId, Security::getUserIdFromHeader(req))) {
            return crow::response(200, "Mesaj silindi.");
        }
        return crow::response(403, "Yetkisiz islem veya mesaj bulunamadi.");
            });

    // ==========================================================
    // 5. MESAJ TEPKİSİ (EMOJİ) EKLE
    // ==========================================================
    CROW_ROUTE(app, "/api/messages/<string>/reactions").methods("POST"_method)
        ([&db](const crow::request& req, std::string messageId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("reaction")) return crow::response(400);

        std::string userId = Security::getUserIdFromHeader(req);
        if (db.addMessageReaction(messageId, userId, std::string(x["reaction"].s()))) {
            return crow::response(201, "Tepki eklendi.");
        }
        return crow::response(500);
            });

    // ==========================================================
    // 6. MESAJ TEPKİSİNİ GERİ AL (URL'DEN SPESİFİK EMOJİ SİLME)
    // ==========================================================
    CROW_ROUTE(app, "/api/messages/<string>/reactions/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string messageId, std::string reaction) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);

        if (db.removeMessageReaction(messageId, userId, reaction)) {
            return crow::response(200, "Tepki kaldirildi.");
        }
        return crow::response(500);
            });

    // ==========================================================
    // 7. ALT MESAJLARI (THREAD) GETİR
    // ==========================================================
    CROW_ROUTE(app, "/api/messages/<string>/thread").methods("GET"_method)
        ([&db](const crow::request& req, std::string messageId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        std::vector<Message> replies = db.getThreadReplies(messageId);
        crow::json::wvalue res;
        for (size_t i = 0; i < replies.size(); ++i) {
            res[i]["id"] = replies[i].id;
            res[i]["sender_id"] = replies[i].sender_id;
            res[i]["sender_name"] = replies[i].sender_name;
            res[i]["content"] = replies[i].content;
            res[i]["timestamp"] = replies[i].timestamp;
        }
        return crow::response(200, res);
            });

    // ==========================================================
    // 8. ALT MESAJ (THREAD) GÖNDER
    // ==========================================================
    CROW_ROUTE(app, "/api/messages/<string>/thread").methods("POST"_method)
        ([&db](const crow::request& req, std::string messageId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("content")) return crow::response(400);

        std::string userId = Security::getUserIdFromHeader(req);
        if (db.addThreadReply(messageId, userId, std::string(x["content"].s()))) {
            return crow::response(201, "Yanit eklendi.");
        }
        return crow::response(500);
            });
}