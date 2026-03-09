#include "MessageRoutes.h"
#include "../utils/Security.h"
#include "../utils/FileManager.h"

void MessageRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // ==========================================================
    // 1. DOSYA / MEDYA YÜKLEME (1. AŞAMA)
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/upload").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db, true)) return crow::response(401);
        auto body = crow::json::load(req.body);
        if (!body || !body.has("file_data") || !body.has("filename")) return crow::response(400, "Eksik veri.");

        try {
            std::string fileUrl = FileManager::saveFile(body["file_data"].s(), body["filename"].s(), FileManager::FileType::ATTACHMENT);
            crow::json::wvalue res;
            res["status"] = "success";
            res["media_path"] = fileUrl;
            return crow::response(201, res);
        }
        catch (const std::exception& e) { return crow::response(500, e.what()); }
            });

    // ==========================================================
    // 2. KULLANICI: ŞİFRELİ MESAJ GÖNDERME
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/<string>/messages").methods("POST"_method)
        ([&db](const crow::request& req, std::string targetId) {
        if (!Security::checkAuth(req, db, true)) return crow::response(403);
        std::string senderId = Security::getUserIdFromHeader(req);
        auto body = crow::json::load(req.body);
        if (!body || !body.has("content")) return crow::response(400);

        std::string rawContent = std::string(body["content"].s());
        std::string contentType = body.has("content_type") ? std::string(body["content_type"].s()) : "text";
        std::string mediaPath = body.has("media_path") ? std::string(body["media_path"].s()) : "";
        std::string msgId = Security::generateId(18);

        std::string encryptedContent = Security::encryptMessage(rawContent);

        if (FileManager::saveChatMessage(senderId, targetId, senderId, msgId, contentType, encryptedContent, mediaPath)) {
            auto targetUser = db.getUser(targetId);
            if (targetUser && targetUser->status == "Offline") {
                db.createNotification(targetId, "OFFLINE_MESSAGE", "Yeni şifreli mesaj.", 1);
            }
            return crow::response(201, "İletildi.");
        }
        return crow::response(500);
            });

    // ==========================================================
    // 3. KULLANICI: ŞİFRELERİ ÇÖZEREK KENDİ GEÇMİŞİNİ GETİR
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/history/<string>").methods("GET"_method)
        ([&](const crow::request& req, std::string targetId) {
        if (!Security::checkAuth(req, db, true)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        std::string rawJson = FileManager::getChatHistoryString(myId, targetId);
        auto parsed = crow::json::load(rawJson);
        std::vector<crow::json::wvalue> decryptedHistory;

        if (parsed && parsed.t() == crow::json::type::List) {
            for (const auto& item : parsed) {
                crow::json::wvalue msg(item);
                if (item.has("content_type") && item["content_type"].s() == "text" && item.has("is_recalled") && !item["is_recalled"].b()) {
                    msg["content"] = Security::decryptMessage(item["content"].s());
                }
                decryptedHistory.push_back(std::move(msg));
            }
        }
        return crow::response(200, crow::json::wvalue(decryptedHistory));
            });

    // ==========================================================
    // 4. ADMIN PANELİ: İSTEDİĞİ İKİ KULLANICININ SOHBETİNİ DENETLE
    // ==========================================================
    CROW_ROUTE(app, "/api/admin/chat/history/<string>/<string>").methods("GET"_method)
        ([&db](const crow::request& req, std::string userA, std::string userB) {

        // DİKKAT: Sadece Sistem Yöneticisi (Admin) erişebilir!
        if (!Security::checkAuth(req, db, true)) return crow::response(403, "Yetkisiz Erisim. Sadece Admin.");

        std::string rawJson = FileManager::getChatHistoryString(userA, userB);
        auto parsed = crow::json::load(rawJson);
        std::vector<crow::json::wvalue> decryptedHistory;

        if (parsed && parsed.t() == crow::json::type::List) {
            for (const auto& item : parsed) {
                crow::json::wvalue msg(item);
                // Admin silinmiş (recalled) mesajların da orjinal metinlerini denetim için görebilir
                if (item.has("content_type") && item["content_type"].s() == "text") {
                    std::string encrypted = item["content"].s();
                    if (!encrypted.empty()) {
                        msg["content_decrypted_by_admin"] = Security::decryptMessage(encrypted); // Admine özel alan
                    }
                }
                decryptedHistory.push_back(std::move(msg));
            }
        }
        return crow::response(200, crow::json::wvalue(decryptedHistory));
            });

    // ==========================================================
    // 5. ADMIN PANELİ: SAKINCALI MESAJI ZORLA SİL (MODERASYON)
    // ==========================================================
    CROW_ROUTE(app, "/api/admin/chat/<string>/<string>/messages/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string userA, std::string userB, std::string msgId) {

        if (!Security::checkAuth(req, db, true)) return crow::response(403, "Yetkisiz Erisim. Sadece Admin.");

        // Kullanıcıların kendi sildiği gibi admin de silebilir
        if (FileManager::recallChatMessage(userA, userB, msgId)) {
            db.logAction(Security::getUserIdFromHeader(req), "ADMIN_MSG_DELETE", msgId, "Admin tarafindan zorla silindi.");
            return crow::response(200, "Mesaj sistemden kaldirildi.");
        }
        return crow::response(404, "Mesaj bulunamadi.");
            });

    // ==========================================================
    // 6. KULLANICI: MESAJI HERKESTEN SİL (RECALL)
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/<string>/messages/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string targetId, std::string msgId) {
        if (!Security::checkAuth(req, db, false)) return crow::response(401); // Düzeltildi: Kullanıcı silebilir
        if (FileManager::recallChatMessage(Security::getUserIdFromHeader(req), targetId, msgId)) {
            return crow::response(200, "Mesaj silindi.");
        }
        return crow::response(404);
            });

    // ==========================================================
    // 7. PINLEME, FAVORİ VE TEPKİLER (Hızlı DB İşlemleri)
    // ==========================================================
    CROW_ROUTE(app, "/api/messages/<string>/pin").methods("PUT"_method)
        ([&db](const crow::request& req, std::string msgId) {
        if (!Security::checkAuth(req, db, false)) return crow::response(401);
        auto body = crow::json::load(req.body);
        bool pin = body && body.has("is_pinned") ? body["is_pinned"].b() : true;
        return db.toggleMessagePin(msgId, pin) ? crow::response(200) : crow::response(500);
            });

    CROW_ROUTE(app, "/api/messages/<string>/reactions").methods("POST"_method)
        ([&db](const crow::request& req, std::string messageId) {
        if (!Security::checkAuth(req, db, false)) return crow::response(401);
        auto body = crow::json::load(req.body);
        if (!body || !body.has("reaction")) return crow::response(400);
        return db.addMessageReaction(messageId, Security::getUserIdFromHeader(req), std::string(body["reaction"].s())) ? crow::response(201) : crow::response(500);
            });

    CROW_ROUTE(app, "/api/messages/<string>/reactions/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string messageId, std::string reaction) {
        if (!Security::checkAuth(req, db, false)) return crow::response(401);
        return db.removeMessageReaction(messageId, Security::getUserIdFromHeader(req), reaction) ? crow::response(200) : crow::response(500);
            });

    // ==========================================================
    // 8. YAZIYOR... (TYPING) SİNYALİ
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/<string>/typing").methods("POST"_method)
        ([&db](const crow::request& req, std::string targetId) {
        if (!Security::checkAuth(req, db, false)) return crow::response(401);
        return crow::response(200);
            });
}