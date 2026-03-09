#include "MessageRoutes.h"
#include "../utils/Security.h"
#include "../utils/FileManager.h"

void MessageRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // ==========================================================
        // 1. KULLANICI: ŞİFRELİ MESAJ GÖNDERME (ESNEK JSON & LOG DESTEKLİ)
        // ==========================================================
    CROW_ROUTE(app, "/api/chat/<string>/messages").methods("POST"_method)
        ([&db](const crow::request& req, std::string targetId) {

        if (!Security::checkAuth(req, db, false)) {
            CROW_LOG_WARNING << "[MESAJ HATASI] Yetkisiz erisim. Token gecersiz.";
            return crow::response(401, "Yetkisiz erisim.");
        }

        std::string senderId = Security::getUserIdFromHeader(req);
        auto body = crow::json::load(req.body);

        if (!body) {
            CROW_LOG_ERROR << "[MESAJ HATASI] Gelen JSON formati bozuk: " << req.body;
            return crow::response(400, "Gecersiz JSON formatı.");
        }

        // Frontend 'content' veya 'text' gonderebilir, ikisini de kabul et
        std::string rawContent = "";
        if (body.has("content")) rawContent = std::string(body["content"].s());
        else if (body.has("text")) rawContent = std::string(body["text"].s());
        else {
            CROW_LOG_ERROR << "[MESAJ HATASI] JSON icinde 'content' veya 'text' bulunamadi.";
            return crow::response(400, "Mesaj icerigi eksik.");
        }

        std::string contentType = body.has("content_type") ? std::string(body["content_type"].s()) : "text";
        std::string msgId = Security::generateId(18);
        std::string encryptedContent = Security::encryptMessage(rawContent);

        // Bağlam (Context) Belirleme
        bool isServer = targetId.find("dm_") == std::string::npos && targetId.length() > 15;
        std::string contextId = targetId;
        if (!isServer && targetId.find("dm_") == std::string::npos) {
            std::string u1 = (senderId < targetId) ? senderId : targetId;
            std::string u2 = (senderId < targetId) ? targetId : senderId;
            contextId = "dm_" + u1 + "_" + u2;
        }

        CROW_LOG_INFO << "[MESAJ] JSON dosyasina yaziliyor... Context: " << contextId;

        if (FileManager::saveChatMessage(contextId, senderId, msgId, contentType, encryptedContent, "", isServer)) {

            // Bildirim Kismi (Sadece DM ise hedefe bildirim at)
            if (!isServer) {
                try {
                    auto targetUser = db.getUserById(targetId);
                    if (targetUser) { // Kullanıcı bulundu mu kontrolü
                        if (targetUser->status == "Offline") {
                            db.createNotification(targetId, "OFFLINE_MESSAGE", "Yeni şifreli mesajınız var.", 1);
                        }
                        // DİKKAT: delete targetUser; SATIRI SİLİNDİ! 
                        // std::optional belleği otomatik temizler, sızıntı yapmaz.
                    }
                }
                catch (...) {
                    CROW_LOG_ERROR << "[BILDIRIM HATASI] Veritabanina bildirim yazilamadi.";
                }
            }
            return crow::response(201, "Mesaj basariyla iletildi ve kaydedildi.");
        }

        CROW_LOG_ERROR << "[DOSYA HATASI] FileManager " << contextId << ".json dosyasina yazamadi!";
        return crow::response(500, "Sunucu dosya yazma hatasi.");
            });

    // ==========================================================
    // 2. KULLANICI: ŞİFRELERİ ÇÖZEREK GEÇMİŞİ GETİR
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/history/<string>").methods("GET"_method)
        ([&db](const crow::request& req, std::string targetId) {
        if (!Security::checkAuth(req, db, false)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        // Bağlamı belirle
        bool isServer = targetId.find("dm_") == std::string::npos && targetId.length() > 15;
        std::string contextId = targetId;
        if (!isServer && targetId.find("dm_") == std::string::npos) {
            std::string u1 = (myId < targetId) ? myId : targetId;
            std::string u2 = (myId < targetId) ? targetId : myId;
            contextId = "dm_" + u1 + "_" + u2;
        }

        // FileManager'dan JSON listesini string olarak al
        std::string rawJson = FileManager::getChatHistory(contextId, isServer);
        auto parsed = nlohmann::json::parse(rawJson);
        nlohmann::json finalHistory = nlohmann::json::array();

        for (auto& item : parsed) {
            nlohmann::json msg = item;
            // Eğer mesaj admin/sahibi tarafından gizlenmişse
            if (msg.contains("is_visible") && msg["is_visible"].get<bool>() == false) {
                msg["content"] = "[Bu mesaj silindi]";
                msg["content_type"] = "system_alert";
            }
            else {
                // Şifreyi çözüp ham metni döndür (Frontend kolaylığı)
                std::string encrypted = msg["content"].get<std::string>();
                msg["content"] = Security::decryptMessage(encrypted);
            }
            finalHistory.push_back(msg);
        }

        return crow::response(200, finalHistory.dump());
            });

    // ==========================================================
    // 3. ADMIN: İSTEDİĞİ SOHBETİ DENETLE (ZORUNLU GÖZETİM)
    // ==========================================================
    CROW_ROUTE(app, "/api/admin/chat/inspect/<string>").methods("GET"_method)
        ([&db](const crow::request& req, std::string contextId) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403, "Sadece Admin.");

        bool isServer = contextId.find("dm_") == std::string::npos;
        std::string rawJson = FileManager::getChatHistory(contextId, isServer);
        auto parsed = nlohmann::json::parse(rawJson);

        for (auto& item : parsed) {
            std::string enc = item["content"].get<std::string>();
            item["content_decrypted_by_admin"] = Security::decryptMessage(enc);
        }

        return crow::response(200, parsed.dump());
            });

    // ==========================================================
    // 4. ADMIN & KULLANICI: MESAJI GİZLE (GÖRÜNMEZ YAPMA)
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/hide-message").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, false)) return crow::response(401);

        auto body = crow::json::load(req.body);
        if (!body || !body.has("context_id") || !body.has("message_id")) return crow::response(400);

        std::string contextId = body["context_id"].s();
        std::string msgId = body["message_id"].s();
        bool isServer = contextId.find("dm_") == std::string::npos;

        // Görünmez yap (Veri tabanından silinmez)
        if (FileManager::toggleMessageVisibility(contextId, msgId, isServer, false)) {
            db.logAction(Security::getUserIdFromHeader(req), "MSG_HIDE", msgId, "Mesaj gizlendi.");
            return crow::response(200, "Mesaj basariyla gizlendi.");
        }

        return crow::response(404, "Mesaj bulunamadi.");
            });

    // ==========================================================
    // 5. MESAJ OKUNDU VE DİĞERLERİ
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/<string>/read").methods("POST"_method)
        ([&db](const crow::request& req, std::string targetId) {
        if (!Security::checkAuth(req, db, false)) return crow::response(401);
        auto body = crow::json::load(req.body);
        if (!body || !body.has("message_id")) return crow::response(400);

        if (db.setChannelReadCursor(Security::getUserIdFromHeader(req), targetId, std::string(body["message_id"].s()))) {
            return crow::response(200);
        }
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/messages/<string>/pin").methods("PUT"_method)
        ([&db](const crow::request& req, std::string msgId) {
        if (!Security::checkAuth(req, db, false)) return crow::response(401);
        auto body = crow::json::load(req.body);
        bool pin = body && body.has("is_pinned") ? body["is_pinned"].b() : true;
        return db.toggleMessagePin(msgId, pin) ? crow::response(200) : crow::response(500);
            });

    CROW_ROUTE(app, "/api/chat/<string>/typing").methods("POST"_method)
        ([&db](const crow::request& req, std::string targetId) {
        if (!Security::checkAuth(req, db, false)) return crow::response(401);
        return crow::response(200);
            });
}