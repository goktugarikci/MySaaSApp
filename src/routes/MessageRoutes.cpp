#include "MessageRoutes.h"
#include "../utils/Security.h"
#include "../utils/FileManager.h"

void MessageRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // 1. ŞİFRELİ MESAJ GÖNDERME VE DOSYAYA YAZMA
    CROW_ROUTE(app, "/api/chat/<string>/messages").methods("POST"_method)
        ([&db](const crow::request& req, std::string targetId) {

        if (!Security::checkAuth(req, db, true)) return crow::response(403, "Giris yapmalisiniz.");
        std::string senderId = Security::getUserIdFromHeader(req);

        auto body = crow::json::load(req.body);
        if (!body || !body.has("content")) return crow::response(400, "Mesaj icerigi eksik.");

        std::string rawContent = std::string(body["content"].s());
        std::string contentType = body.has("content_type") ? std::string(body["content_type"].s()) : "text";
        std::string mediaPath = body.has("media_path") ? std::string(body["media_path"].s()) : "";
        std::string msgId = Security::generateId(18);

        // METNİ ŞİFRELE
        std::string encryptedContent = Security::encryptMessage(rawContent);

        if (FileManager::saveChatMessage(senderId, targetId, senderId, msgId, contentType, encryptedContent, mediaPath)) {
            auto targetUser = db.getUser(targetId);
            if (targetUser && targetUser->status == "Offline") {
                db.createNotification(targetId, "OFFLINE_MESSAGE", "Siz yokken yeni bir şifreli mesaj geldi.", 1);
            }
            return crow::response(201, "Mesaj basariyla sifrelendi.");
        }
        return crow::response(500, "Dosyaya yazilamadi.");
            });

    // 2. ŞİFRELERİ ÇÖZEREK GEÇMİŞİ GETİR (E2291 HATASI ÇÖZÜLDÜ!)
    CROW_ROUTE(app, "/api/chat/history/<string>").methods("GET"_method)
        ([&](const crow::request& req, std::string targetId) {

        if (!Security::checkAuth(req, db, true)) return crow::response(401, "Yetkisiz islem.");
        std::string myId = Security::getUserIdFromHeader(req);

        // Metin olarak oku ve RVALUE (Okunabilir JSON) olarak parse et
        std::string rawJson = FileManager::getChatHistoryString(myId, targetId);
        auto parsed = crow::json::load(rawJson);

        std::vector<crow::json::wvalue> decryptedHistory;

        if (parsed && parsed.t() == crow::json::type::List) {
            // Rvalue üzerinde döngü kurabiliriz!
            for (const auto& item : parsed) {
                crow::json::wvalue msg(item); // Arayüze göndermek için kopyala

                // Sadece okunabilir 'item' üzerinden sorgu yapıyoruz
                if (item.has("content_type") && item["content_type"].s() == "text" &&
                    item.has("is_recalled") && !item["is_recalled"].b()) {

                    std::string encrypted = item["content"].s();
                    std::string decrypted = Security::decryptMessage(encrypted);
                    msg["content"] = decrypted; // Çözülmüş şifreyi wvalue içine geri koy
                }
                decryptedHistory.push_back(std::move(msg));
            }
        }
        return crow::response(200, crow::json::wvalue(decryptedHistory));
            });

    // 3. MESAJ PINLEME (SABİTLEME)
    CROW_ROUTE(app, "/api/messages/<string>/pin").methods("PUT"_method)
        ([&db](const crow::request& req, std::string msgId) {
        if (!Security::checkAuth(req, db, true)) return crow::response(401);
        auto body = crow::json::load(req.body);
        bool pin = body && body.has("is_pinned") ? body["is_pinned"].b() : true;

        if (db.toggleMessagePin(msgId, pin)) return crow::response(200, pin ? "Sabitlendi." : "Kaldirildi.");
        return crow::response(500);
            });

    // 4. EMOJİ TEPKİSİ (REACTION) EKLEME
    CROW_ROUTE(app, "/api/messages/<string>/reactions").methods("POST"_method)
        ([&db](const crow::request& req, std::string messageId) {
        if (!Security::checkAuth(req, db, true)) return crow::response(401);
        auto body = crow::json::load(req.body);
        if (!body || !body.has("reaction")) return crow::response(400);

        if (db.addMessageReaction(messageId, Security::getUserIdFromHeader(req), std::string(body["reaction"].s()))) {
            return crow::response(201, "Tepki eklendi.");
        }
        return crow::response(500);
            });

    // 5. YAZIYOR... (TYPING) SİNYALİ
    CROW_ROUTE(app, "/api/chat/<string>/typing").methods("POST"_method)
        ([&db](const crow::request& req, std::string targetId) {
        if (!Security::checkAuth(req, db, true)) return crow::response(401);
        return crow::response(200, "Yaziyor sinyali iletildi.");
            });
    // ==========================================================
    // 8. MESAJI HERKESTEN SİL (RECALL) VE WEBSOCKET
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/<string>/messages/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string targetId, std::string msgId) {

        if (!Security::checkAuth(req, db, true)) return crow::response(401, "Giris yapmalisiniz.");
        std::string myId = Security::getUserIdFromHeader(req);

        // FileManager üzerinden dosyadan sil
        if (FileManager::recallChatMessage(myId, targetId, msgId)) {

            // 🌐 WEBSOCKET TETİKLEMESİ: Karşı tarafın ekranında mesaj anında "Silindi" balonuna dönüşür
            // WsManager::broadcastMessageDeleted(targetId, msgId);

            return crow::response(200, "Mesaj herkesten silindi.");
        }
        return crow::response(404, "Mesaj bulunamadi veya zaten silinmis.");
            });

}
