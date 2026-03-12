#include "MessageRoutes.h"
#include "../utils/Security.h"
#include "../utils/FileManager.h"
#include "../utils/LogManager.h" 
#include <mutex>
#include <vector>
#include <string>

void MessageRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // ==========================================================
    // 1. MESAJ GÖNDERME (YENİ FileManager UYUMLU)
    // ==========================================================
<<<<<<< HEAD
    CROW_ROUTE(app, "/api/chat/<string>/messages").methods("POST"_method)
        ([&db](const crow::request& req, std::string targetId) {
        try {
            if (!Security::checkAuth(req, db, false)) return crow::response(401);

            std::string senderId = Security::getUserIdFromHeader(req);
            auto body = crow::json::load(req.body);
            if (!body) return crow::response(400, "Gecersiz JSON.");

            std::string rawContent = std::string("");
            if (body.has("Mesaj")) rawContent = std::string(body["Mesaj"].s());
            else if (body.has("content")) rawContent = std::string(body["content"].s());
            else if (body.has("text")) rawContent = std::string(body["text"].s());

            if (rawContent.empty()) return crow::response(400, "Mesaj bos olamaz.");

            std::string contentType = std::string("Text");
            if (body.has("MesajİçerikDurumu")) contentType = std::string(body["MesajİçerikDurumu"].s());
            else if (body.has("content_type")) contentType = std::string(body["content_type"].s());

            bool isGroup = false;
            if (body.has("is_group")) isGroup = body["is_group"].b();
            else if (body.has("is_server")) isGroup = body["is_server"].b();

            std::string groupId = body.has("group_id") ? std::string(body["group_id"].s()) : std::string("default_group");

            std::string msgId = Security::generateId(18);
            std::string encrypted = Security::encryptMessage(rawContent);

            // FileManager'in YENİ fonksiyonlarını kullan
            bool saved = false;
            if (isGroup) {
                saved = FileManager::saveGroupMessage(groupId, targetId, senderId, encrypted, contentType);
            }
            else {
                saved = FileManager::savePrivateMessage(senderId, targetId, encrypted, contentType);
            }

            if (saved) {
                // LogManager Kuyruğu (İsteğe Bağlı)
                LogManager::addToQueue(senderId, targetId, encrypted);

                if (!isGroup) {
                    try {
                        auto targetUser = db.getUserById(targetId);
                        if (targetUser && targetUser->status == "Offline") {
                            db.createNotification(targetId, "OFFLINE_MESSAGE", "Yeni şifreli mesajınız var.", 1);
                        }
                    }
                    catch (...) {}
                }
                return crow::response(201, "Mesaj basariyla iletildi ve kaydedildi.");
            }

            return crow::response(500, "Mesaj kaydedilemedi.");
        }
        catch (const std::exception& e) {
            CROW_LOG_ERROR << "API Mesaj Hatasi: " << e.what();
            return crow::response(500, "Sunucu Hatasi.");
        }
=======
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
<<<<<<< HEAD

        std::string attachmentUrl = x.has("attachment_url") ? std::string(x["attachment_url"].s()) : "";
        std::string userId = Security::getUserIdFromHeader(req);

        if (db.sendMessage(channelId, userId, std::string(x["content"].s()), attachmentUrl)) {

            // LOG: Yeni Mesaj Gönderimi
            db.logAction(userId, "SEND_MESSAGE", channelId, "Kullanici bir kanala veya DM'e yeni mesaj gonderdi.");

            return crow::response(201, "Mesaj gonderildi.");
        }
        return crow::response(500);
>>>>>>> parent of 25d01e2 (v)
            });

    // ==========================================================
    // 2. GEÇMİŞİ GETİR (YENİ FileManager UYUMLU)
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/history/<string>").methods("GET"_method)
        ([&db](const crow::request& req, std::string targetId) {
        try {
            if (!Security::checkAuth(req, db, false)) return crow::response(401);
            std::string myId = Security::getUserIdFromHeader(req);

            // İstemci URL'ye ?is_group=true veya &group_id=X ekleyebilir
            bool isGroup = req.url_params.get("is_group") != nullptr ? std::string(req.url_params.get("is_group")) == "true" : (targetId.find("dm_") == std::string::npos && targetId.length() > 15);
            std::string rawJson = "[]";

            // YENİ FONKSİYONLARI ÇAĞIRIYORUZ (getChatHistory silindi)
            if (isGroup) {
                std::string groupId = req.url_params.get("group_id") ? std::string(req.url_params.get("group_id")) : "default_group";
                rawJson = FileManager::getGroupChatHistory(groupId, targetId);
            }
            else {
                // DM için targetId karşı kullanıcının ID'sidir
                rawJson = FileManager::getPrivateChatHistory(myId, targetId);
            }

            nlohmann::json finalHistory = nlohmann::json::array();

            if (rawJson.empty() || rawJson.find_first_not_of(" \t\n\v\f\r") == std::string::npos) {
                return crow::response(200, finalHistory.dump());
            }

            auto parsed = nlohmann::json::parse(rawJson);
            for (auto& item : parsed) {
                if (item.contains("MesajSilinmeDurumu") && item["MesajSilinmeDurumu"].is_boolean() && item["MesajSilinmeDurumu"].get<bool>() == true) {
                    if (item.contains("Mesaj")) item["Mesaj"] = "[Bu mesaj silindi]";
                }
                else if (item.contains("MesajSilinmeDurumu") && item["MesajSilinmeDurumu"].is_string() && item["MesajSilinmeDurumu"].get<std::string>() == "Global") {
                    if (item.contains("Mesaj")) item["Mesaj"] = "[Bu mesaj silindi]";
                }
                else {
                    std::string encrypted = item.contains("Mesaj") ? item["Mesaj"].get<std::string>() : "";
                    if (!encrypted.empty()) {
                        std::string decrypted = Security::decryptMessage(encrypted);
                        if (item.contains("Mesaj")) item["Mesaj"] = decrypted;
                    }
                }
                finalHistory.push_back(item);
            }
            return crow::response(200, finalHistory.dump());
        }
        catch (...) {
            return crow::response(200, "[]");
        }
=======

        std::string attachmentUrl = x.has("attachment_url") ? std::string(x["attachment_url"].s()) : "";
        std::string userId = Security::getUserIdFromHeader(req);

        if (db.sendMessage(channelId, userId, std::string(x["content"].s()), attachmentUrl)) {

            // LOG: Yeni Mesaj Gönderimi
            db.logAction(userId, "SEND_MESSAGE", channelId, "Kullanici bir kanala veya DM'e yeni mesaj gonderdi.");

            return crow::response(201, "Mesaj gonderildi.");
        }
        return crow::response(500);
>>>>>>> parent of 25d01e2 (v)
            });

    // ==========================================================
    // 3. ADMIN: SOHBET DENETİMİ
    // ==========================================================
    CROW_ROUTE(app, "/api/admin/chat/inspect/<string>").methods("GET"_method)
        ([&db](const crow::request& req, std::string targetId) {
        try {
            if (!Security::checkAuth(req, db, true)) return crow::response(403);

            bool isGroup = req.url_params.get("is_group") != nullptr ? std::string(req.url_params.get("is_group")) == "true" : false;
            std::string rawJson = "[]";

            if (isGroup) {
                std::string groupId = req.url_params.get("group_id") ? std::string(req.url_params.get("group_id")) : "default_group";
                rawJson = FileManager::getGroupChatHistory(groupId, targetId);
            }
            else {
                // Admin iki kullanıcı idsini parametre olarak geçmelidir ?user1=x&user2=y
                std::string u1 = req.url_params.get("user1") ? std::string(req.url_params.get("user1")) : "";
                std::string u2 = req.url_params.get("user2") ? std::string(req.url_params.get("user2")) : targetId;
                rawJson = FileManager::getPrivateChatHistory(u1, u2);
            }

            if (rawJson.empty() || rawJson.length() < 5) return crow::response(200, "[]");

            auto parsed = nlohmann::json::parse(rawJson);
            for (auto& item : parsed) {
                std::string encrypted = item.contains("Mesaj") ? item["Mesaj"].get<std::string>() : "";
                item["admin_view"] = Security::decryptMessage(encrypted);
            }
            return crow::response(200, parsed.dump());
        }
        catch (...) { return crow::response(500, "[]"); }
            });

    // ==========================================================
    // 4. MESAJ GİZLEME (SOFT DELETE)
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/hide-message").methods("POST"_method)
        ([&db](const crow::request& req) {
        try {
            if (!Security::checkAuth(req, db, false)) return crow::response(401);
            auto body = crow::json::load(req.body);
            if (!body || !body.has("context_id") || !body.has("message_id")) return crow::response(400);

            std::string cId = std::string(body["context_id"].s());
            std::string mId = std::string(body["message_id"].s());

            bool isGroup = false;
            if (body.has("is_group")) isGroup = body["is_group"].b();

            std::string groupId = body.has("group_id") ? std::string(body["group_id"].s()) : "";

            // toggleMessageVisibility güncellenmiş haliyle
            if (FileManager::toggleMessageVisibility(cId, mId, isGroup, groupId)) {
                return crow::response(200, "Mesaj gizlendi.");
            }
            return crow::response(404);
        }
        catch (...) { return crow::response(500); }
            });
}