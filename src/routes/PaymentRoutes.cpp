#include "MessageRoutes.h"
#include "../utils/Security.h"
#include "../utils/FileManager.h"
#include <nlohmann/json.hpp>

void MessageRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // ==========================================================
    // 1. REST API İLE MESAJ GÖNDERME
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/<string>/messages").methods("POST"_method)
        ([&db](const crow::request& req, std::string targetId) {
        // Kimlik doğrulama
        if (!Security::checkAuth(req, db, false)) {
            return crow::response(401, "Yetkisiz erisim. Gecerli bir token saglayin.");
        }

        std::string senderId = Security::getUserIdFromHeader(req);
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Gecersiz JSON formatı.");
        }

        // Mesaj içeriğini güvenli bir şekilde al
        std::string rawContent = "";
        if (body.has("content")) rawContent = std::string(body["content"].s());
        else if (body.has("text")) rawContent = std::string(body["text"].s());

        if (rawContent.empty()) {
            return crow::response(400, "Mesaj icerigi eksik.");
        }

        // Güvenli Tip Dönüşümleri (Derleyici hatasını önlemek için std::string cast yapıldı)
        std::string contentType = body.has("content_type") ? std::string(body["content_type"].s()) : std::string("Text");
        bool isGroup = body.has("is_group") ? body["is_group"].b() : false;
        std::string groupId = body.has("group_id") ? std::string(body["group_id"].s()) : std::string("default_group");

        // Mesajı Şifrele
        std::string encryptedContent = Security::encryptMessage(rawContent);
        bool saved = false;

        // Dosya Yöneticisine (FileManager) kaydet
        if (isGroup) {
            saved = FileManager::saveGroupMessage(groupId, targetId, senderId, encryptedContent, contentType);
        }
        else {
            saved = FileManager::savePrivateMessage(senderId, targetId, encryptedContent, contentType);
        }

        if (saved) {
            return crow::response(201, "Mesaj basariyla iletildi ve sisteme kaydedildi.");
        }

        return crow::response(500, "Sunucu hatasi: Dosya yazilamadi.");
            });


    // ==========================================================
    // 2. MESAJ GEÇMİŞİNİ GETİRME
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/history/<string>").methods("GET"_method)
        ([&db](const crow::request& req, std::string targetId) {
        // Kimlik doğrulama
        if (!Security::checkAuth(req, db, false)) {
            return crow::response(401, "Yetkisiz erisim.");
        }

        std::string myId = Security::getUserIdFromHeader(req);

        // URL Parametrelerinden grup/DM ayrımını kontrol et (?is_group=true&group_id=...)
        auto isGroupParam = req.url_params.get("is_group");
        bool isGroup = (isGroupParam != nullptr && std::string(isGroupParam) == "true");

        std::string rawJsonStr;

        if (isGroup) {
            auto groupIdParam = req.url_params.get("group_id");
            std::string groupId = groupIdParam ? std::string(groupIdParam) : std::string("default_group");
            rawJsonStr = FileManager::getGroupChatHistory(groupId, targetId);
        }
        else {
            rawJsonStr = FileManager::getPrivateChatHistory(myId, targetId);
        }

        try {
            // Ham JSON stringini Nlohmann JSON nesnesine çevir
            auto parsedHistory = nlohmann::json::parse(rawJsonStr);

            // Her mesajı dön ve şifrelenmiş içeriği çöz (Frontend'e açık göndermek için)
            // İsterseniz frontend şifreyi çözebilir, ancak API üzerinden okunabilir isteniyorsa:
            for (auto& msg : parsedHistory) {
                if (msg.contains("Mesaj")) {

                    bool isDeleted = false;
                    if (msg["MesajSilinmeDurumu"].is_boolean()) {
                        isDeleted = msg["MesajSilinmeDurumu"].get<bool>();
                    }
                    else if (msg["MesajSilinmeDurumu"].is_string()) {
                        isDeleted = (msg["MesajSilinmeDurumu"].get<std::string>() != "Yok");
                    }

                    // Mesaj silinmemişse şifresini çöz
                    if (!isDeleted) {
                        std::string encryptedStr = msg["Mesaj"].get<std::string>();
                        msg["Mesaj_Cozulmus"] = Security::decryptMessage(encryptedStr);
                    }
                    else {
                        msg["Mesaj_Cozulmus"] = "Bu mesaj silindi.";
                    }
                }
            }

            crow::response res;
            res.code = 200;
            res.set_header("Content-Type", "application/json");
            res.body = parsedHistory.dump();
            return res;

        }
        catch (const std::exception& e) {
            // JSON Parse hatası durumu
            return crow::response(500, "Gecmis okunurken JSON format hatasi olustu.");
        }
            });

}