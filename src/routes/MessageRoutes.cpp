#include "MessageRoutes.h"
#include "../utils/Security.h"

void MessageRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // ==========================================================
    // 1. KANAL / DM MESAJLARINI GETİR (Okuma işlemi çok tekrarlandığı için loglanmaz)
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
    // 2. KANALA VEYA DM'E MESAJ GÖNDER VE LOGLA
    // ==========================================================
    CROW_ROUTE(app, "/api/channels/<string>/messages").methods("POST"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("content")) return crow::response(400);

        std::string attachmentUrl = x.has("attachment_url") ? std::string(x["attachment_url"].s()) : "";
        std::string userId = Security::getUserIdFromHeader(req);

        if (db.sendMessage(channelId, userId, std::string(x["content"].s()), attachmentUrl)) {

            // LOG: Yeni Mesaj Gönderimi
            db.logAction(userId, "SEND_MESSAGE", channelId, "Kullanici bir kanala veya DM'e yeni mesaj gonderdi.");

            return crow::response(201, "Mesaj gonderildi.");
        }
        return crow::response(500);
            });

    // ==========================================================
    // 3. MESAJI DÜZENLE VE LOGLA (SUNUCU VEYA DM FARK ETMEZ)
    // ==========================================================
    CROW_ROUTE(app, "/api/messages/<string>").methods("PUT"_method)
        ([&db](const crow::request& req, std::string messageId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("content")) return crow::response(400);

        std::string userId = Security::getUserIdFromHeader(req);

        if (db.updateMessage(messageId, std::string(x["content"].s()))) {

            // LOG: Mesaj Düzenleme
            db.logAction(userId, "EDIT_MESSAGE", messageId, "Kullanici kendi mesajinin icerigini duzenledi.");

            return crow::response(200, "Mesaj guncellendi.");
        }
        return crow::response(500);
            });

    // ==========================================================
    // 4. MESAJI SİL VE LOGLA
    // ==========================================================
    CROW_ROUTE(app, "/api/messages/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string msgId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string userId = Security::getUserIdFromHeader(req);

        // Sadece yetkili kişi (mesajın sahibi) silebilir
        if (db.deleteMessage(msgId, userId)) {

            // LOG: Mesaj Silme
            db.logAction(userId, "DELETE_MESSAGE", msgId, "Kullanici kendi mesajini sildi.");

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
    // 6. MESAJ TEPKİSİNİ GERİ AL (SİL)
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
    // 8. ALT MESAJ (THREAD) GÖNDER VE LOGLA
    // ==========================================================
    CROW_ROUTE(app, "/api/messages/<string>/thread").methods("POST"_method)
        ([&db](const crow::request& req, std::string messageId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("content")) return crow::response(400);

        std::string userId = Security::getUserIdFromHeader(req);
        if (db.addThreadReply(messageId, userId, std::string(x["content"].s()))) {

            // LOG: Thread (Alt Sohbet) Mesajı Gönderme
            db.logAction(userId, "SEND_THREAD_REPLY", messageId, "Kullanici bir mesaja alt yanit (thread) gonderdi.");

            return crow::response(201, "Yanit eklendi.");
        }
        return crow::response(500);
            });

    // ==========================================================
    // 9. MESAJ ARAMA (SEARCH) - V2.0
    // ==========================================================
    CROW_ROUTE(app, "/api/channels/<string>/messages/search").methods("GET"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        char* q = req.url_params.get("q");
        if (!q) return crow::response(400, "Arama metni eksik.");

        auto msgs = db.searchMessages(channelId, std::string(q));
        crow::json::wvalue res;
        for (size_t i = 0; i < msgs.size(); ++i) res[i] = msgs[i].toJson();
        return crow::response(200, res);
            });

    // ==========================================================
    // 10. MESAJ PINLEME (SABİTLEME) - V2.0
    // ==========================================================
    CROW_ROUTE(app, "/api/messages/<string>/pin").methods("PUT"_method)
        ([&db](const crow::request& req, std::string msgId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        bool pin = x.has("is_pinned") ? x["is_pinned"].b() : true;

        if (db.toggleMessagePin(msgId, pin)) {
            db.logAction(Security::getUserIdFromHeader(req), pin ? "PIN_MESSAGE" : "UNPIN_MESSAGE", msgId, "Mesaj sabitlendi/kaldirildi.");
            return crow::response(200, pin ? "Mesaj sabitlendi." : "Mesaj sabitten kaldirildi.");
        }
        return crow::response(500);
            });

    // ==========================================================
    // V3.0 - AŞAMA 2: KANAL OKUNDU VE TYPING BİLGİSİ
    // ==========================================================

    // MESAJI KAYDET (FAVORİLERE EKLE/ÇIKAR)
    CROW_ROUTE(app, "/api/messages/<string>/save").methods("POST"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string messageId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (req.method == "POST"_method) {
            if (db.saveMessage(myId, messageId)) return crow::response(200, "Mesaj kaydedildi.");
        }
        else {
            if (db.removeSavedMessage(myId, messageId)) return crow::response(200, "Mesaj kaydedilenlerden cikarildi.");
        }
        return crow::response(500);
            });

    // OKUNDU BİLGİSİNİ GÜNCELLE
    CROW_ROUTE(app, "/api/channels/<string>/read").methods("PUT"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("message_id")) return crow::response(400);

        std::string myId = Security::getUserIdFromHeader(req);
        if (db.setChannelReadCursor(myId, channelId, std::string(x["message_id"].s()))) {
            return crow::response(200);
        }
        return crow::response(500);
            });

    // "YAZIYOR..." (TYPING) BİLDİRİMİ TETİKLEYİCİ
    CROW_ROUTE(app, "/api/channels/<string>/typing").methods("POST"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        // İleride buraya WebSocket Broadcast fonksiyonu eklenecek: 
        // WsManager::broadcastTypingEvent(channelId, userId);
        return crow::response(200);
            });

}