#include "MessageRoutes.h"
#include "../utils/Security.h"

void MessageRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

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

    // KANALA VEYA DM'YE MESAJ GÖNDER (mysaas_logs.db'ye KAYDEDER)
    CROW_ROUTE(app, "/api/channels/<string>/messages").methods("POST"_method)
        ([&db](const crow::request& req, std::string targetId) {

        if (!Security::checkAuth(req, db, true)) {
            return crow::response(403, "Mesaj gondermek icin giris yapmalisiniz.");
        }
        std::string senderId = Security::getUserIdFromHeader(req);

        auto body = crow::json::load(req.body);
        if (!body || !body.has("content")) {
            return crow::response(400, "Mesaj icerigi ('content') eksik.");
        }

        std::string content = std::string(body["content"].s());
        std::string chatType = body.has("chat_type") ? std::string(body["chat_type"].s()) : "SERVER";

        // YENİ 4 PARAMETRELİ KULLANIM BURADA!
        if (db.saveMessage(senderId, targetId, chatType, content)) {

            // Çevrimdışı bildirimi (Offline Notification) kontrolü
            if (chatType == "DM") {
                auto targetUser = db.getUser(targetId);
                if (targetUser && targetUser->status == "Offline") {
                    db.createNotification(targetId, "OFFLINE_MESSAGE", "Siz yokken yeni bir mesaj geldi.", 1);
                }
            }

            crow::json::wvalue res;
            res["status"] = "success";
            res["message"] = "Mesaj iletildi ve loglandi.";
            return crow::response(201, res);
        }

        return crow::response(500, "Sunucu hatasi: Mesaj veritabanina yazilamadi.");
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
    
    // MESAJI KAYDET (FAVORİLERE EKLE / ÇIKAR)
    CROW_ROUTE(app, "/api/chat/history/<string>").methods("POST"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string messageId) {

        if (!Security::checkAuth(req, db, true)) return crow::response(401, "Giris yapmalisiniz.");
        std::string myId = Security::getUserIdFromHeader(req);

        // Fonksiyon ismini saveFavoriteMessage olarak değiştirdik
        if (req.method == "POST"_method) {
            if (db.saveFavoriteMessage(myId, messageId)) return crow::response(200, "Mesaj favorilere kaydedildi.");
        }
        else if (req.method == "DELETE"_method) {
            if (db.removeSavedMessage(myId, messageId)) return crow::response(200, "Mesaj favorilerden cikarildi.");
        }

        return crow::response(500, "Islem basarisiz.");
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
    // ==========================================================
    // MESAJ GEÇMİŞİNİ GETİR (Sohbet Geçmişi Yüklenemedi Hatası Çözümü)
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/history/<string>").methods("GET"_method)
        ([&db](const crow::request& req, std::string targetId) {

        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        crow::json::wvalue res;
        int idx = 0;

        // İki kişi arasındaki tüm mesajları tarihe göre sıralayarak çek
        std::string sql = "SELECT ID, SenderID, Content, strftime('%H:%M', CreatedAt) FROM Messages "
            "WHERE (SenderID='" + myId + "' AND TargetID='" + targetId + "') "
            "   OR (SenderID='" + targetId + "' AND TargetID='" + myId + "') "
            "ORDER BY CreatedAt ASC;";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db.getDb(), sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                res[idx]["id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                res[idx]["senderId"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                res[idx]["text"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

                // Saat bilgisini formatlı al (Eğer NULL ise varsayılan değer ata)
                const char* timePtr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                res[idx]["timestamp"] = timePtr ? timePtr : "00:00";
                idx++;
            }
        }
        sqlite3_finalize(stmt);

        // Hiç mesaj yoksa boş dizi dön (Hata vermemesi için type::List kullanıyoruz)
        if (idx == 0) return crow::response(200, crow::json::wvalue(crow::json::type::List));

        return crow::response(200, res);
            });

}