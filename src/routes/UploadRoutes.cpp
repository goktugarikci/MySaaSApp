#include "UploadRoutes.h"
#include "../utils/Security.h"
#include "../utils/FileManager.h"
#include <fstream>
#include <filesystem>

void UploadRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // İstemciden gelen dosyaları diske yazar ve URL'sini döndürür
    CROW_ROUTE(app, "/api/upload").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        // Uploads klasörü yoksa oluştur
        if (!std::filesystem::exists("uploads")) {
            std::filesystem::create_directory("uploads");
        }

        try {
            crow::multipart::message file_message(req);
            auto part = file_message.get_part_by_name("file");

            if (part.body.empty()) return crow::response(400, "Dosya bulunamadi.");

            // Dosya uzantısını belirle (Basit Güvenlik/Tahmin)
            std::string ext = ".bin";
            auto cTypeHeader = part.get_header_object("Content-Type");
            if (cTypeHeader.value.find("image/jpeg") != std::string::npos) ext = ".jpg";
            else if (cTypeHeader.value.find("image/png") != std::string::npos) ext = ".png";
            else if (cTypeHeader.value.find("application/pdf") != std::string::npos) ext = ".pdf";

            // Benzersiz bir dosya ismi oluştur
            std::string fileName = "file_" + Security::generateId(12) + ext;
            std::string filePath = "uploads/" + fileName;

            // Dosyayı diske yaz (Binary modda)
            std::ofstream out(filePath, std::ios::binary);
            if (!out) return crow::response(500, "Dosya diske yazilamadi.");
            out << part.body;
            out.close();

            // Frontend'e dosyanın erişim linkini dön
            crow::json::wvalue res;
            res["url"] = "/" + filePath; // Örn: /uploads/file_A1B2C3.png
            res["message"] = "Dosya basariyla yuklendi.";

            return crow::response(201, res);
        }
        catch (const std::exception& e) {
            return crow::response(500, "Upload hatasi: " + std::string(e.what()));
        }
            });
    // ==========================================================
    // 9. DOSYA / MEDYA YÜKLEME (1. AŞAMA)
    // ==========================================================
    CROW_ROUTE(app, "/api/chat/upload").methods("POST"_method)
        ([&db](const crow::request& req) {

        if (!Security::checkAuth(req, db, true)) return crow::response(401);

        // JSON içinde base64 veya raw byte olarak geldiğini varsayıyoruz
        auto body = crow::json::load(req.body);
        if (!body || !body.has("file_data") || !body.has("filename")) {
            return crow::response(400, "Eksik dosya verisi.");
        }

        std::string fileData = body["file_data"].s(); // Dosyanın ham hali
        std::string fileName = body["filename"].s();  // Örn: tatil.mp4

        try {
            // FileManager dosyamızdaki saveFile metodunu kullanarak diske kaydediyoruz
            std::string fileUrl = FileManager::saveFile(fileData, fileName, FileManager::FileType::ATTACHMENT);

            // URL'yi Frontend'e geri dönüyoruz
            crow::json::wvalue res;
            res["status"] = "success";
            res["media_path"] = fileUrl; // Örn: /uploads/attachments/123456_789.mp4
            return crow::response(201, res);
        }
        catch (const std::exception& e) {
            return crow::response(500, e.what());
        }
            });
}