#include "MessageRoutes.h"
#include "../utils/Security.h"
#include "../utils/FileManager.h"

void MessageRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // ==========================================================
    // 1. KULLANICI: ŞİFRELİ MESAJ GÖNDERME (JSON'A YAZAR)
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/<string>/messages").methods("POST"_method)
        ([&db](const crow::request& req, std::string targetId) {

        if (!Security::checkAuth(req, db, false)) return crow::response(401);

        std::string senderId = Security::getUserIdFromHeader(req);
        auto body = crow::json::load(req.body);
        if (!body || !body.has("content")) return crow::response(400);

        std::string rawContent = std::string(body["content"].s());
        std::string contentType = body.has("content_type") ? std::string(body["content_type"].s()) : "text";
        std::string mediaPath = body.has("media_path") ? std::string(body["media_path"].s()) : "";
        std::string msgId = Security::generateId(18);

        // İçeriği diskte saklamadan önce şifreliyoruz
        std::string encryptedContent = Security::encryptMessage(rawContent);

        // Bağlamı belirle (DM ise alfabetik sıralı context oluştur)
        bool isServer = targetId.find("dm_") == std::string::npos && targetId.length() > 15; // Basit bir server/DM ayrımı
        std::string contextId = targetId;
        if (!isServer && targetId.find("dm_") == std::string::npos) {
            std::string u1 = (senderId < targetId) ? senderId : targetId;
            std::string u2 = (senderId < targetId) ? targetId : senderId;
            contextId = "dm_" + u1 + "_" + u2;
        }

        if (FileManager::saveChatMessage(contextId, senderId, msgId, contentType, encryptedContent, mediaPath, isServer)) {
            auto targetUser = db.getUserById(targetId);
            if (targetUser && targetUser->status == "Offline") {
                db.createNotification(targetId, "OFFLINE_MESSAGE", "Yeni mesajiniz var.", 1);
            }
            return crow::response(201, "Mesaj JSON dosyasina mühürlendi.");
        }
        return crow::response(500, "Dosya yazma hatasi.");
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