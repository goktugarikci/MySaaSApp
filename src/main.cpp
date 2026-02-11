#include "crow.h"
#include "db/DatabaseManager.h"
#include "utils/Security.h"
#include "utils/FileManager.h"

int main() {
    // 1. Klasörleri oluştur
    FileManager::initDirectories();

    // Veritabanı başlat
    DatabaseManager db("mysaasapp.db");
    if (db.open()) {
        std::cout << "Veritabani baglantisi basarili.\n";
        db.initTables();
    }
    else {
        std::cerr << "Veritabani acilamadi!\n";
        return -1;
    }

    crow::SimpleApp app;

    // --- DOSYA YÜKLEME ENDPOINT'İ ---
    // POST /api/upload
    CROW_ROUTE(app, "/api/upload").methods("POST"_method)
        ([&db](const crow::request& req) {
        crow::multipart::message msg(req);

        std::string original_filename;
        std::string file_content;
        std::string upload_type;
        bool has_file = false;
        bool has_type = false;

        // GÜVENLİ YÖNTEM: Tüm parçaları gez ve isimlerine bak
        for (const auto& part : msg.parts) {
            const auto& content_disposition = part.get_header_object("Content-Disposition");

            if (content_disposition.params.count("name")) {
                std::string name = content_disposition.params.at("name");

                if (name == "file") {
                    // Dosya bulundu
                    if (content_disposition.params.count("filename")) {
                        original_filename = content_disposition.params.at("filename");
                    }
                    file_content = part.body;
                    has_file = true;
                }
                else if (name == "type") {
                    // Tip bilgisi bulundu (avatar/attachment)
                    upload_type = part.body;
                    has_type = true;
                }
            }
        }

        if (!has_file || !has_type) {
            return crow::response(400, "Eksik parametre: 'file' ve 'type' gereklidir.");
        }

        try {
            FileManager::FileType fType = (upload_type == "avatar") ?
                FileManager::FileType::AVATAR :
                FileManager::FileType::ATTACHMENT;

            // Dosyayı kaydet
            std::string fileUrl = FileManager::saveFile(file_content, original_filename, fType);

            // JSON Cevap Dön
            crow::json::wvalue result;
            result["url"] = fileUrl;
            result["message"] = "Dosya basariyla yuklendi.";
            return crow::response(200, result);

        }
        catch (const std::exception& e) {
            return crow::response(500, std::string("Sunucu Hatasi: ") + e.what());
        }
            });

    // --- PROFİL FOTOĞRAFI GÜNCELLEME ---
    CROW_ROUTE(app, "/api/users/me/avatar").methods("PUT"_method)
        ([&db](const crow::request& req) {
        int currentUserId = 1; // Test için sabit ID

        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Geçersiz JSON");

        std::string newUrl = x["avatar_url"].s();

        if (db.updateUserAvatar(currentUserId, newUrl)) {
            return crow::response(200, "Profil fotografi guncellendi.");
        }
        return crow::response(500, "Veritabani hatasi.");
            });

    // --- STATİK DOSYA SUNUCUSU ---
    CROW_ROUTE(app, "/uploads/<path>")
        ([](const crow::request& req, crow::response& res, std::string path) {
        std::string fullPath = "/uploads/" + path;
        std::string content = FileManager::readFile(fullPath);

        if (content.empty()) {
            res.code = 404;
            res.write("Dosya bulunamadi.");
        }
        else {
            res.code = 200;
            if (path.find(".png") != std::string::npos) res.set_header("Content-Type", "image/png");
            else if (path.find(".jpg") != std::string::npos || path.find(".jpeg") != std::string::npos) res.set_header("Content-Type", "image/jpeg");
            else if (path.find(".mp4") != std::string::npos) res.set_header("Content-Type", "video/mp4");
            else if (path.find(".pdf") != std::string::npos) res.set_header("Content-Type", "application/pdf");

            res.write(content);
        }
        res.end();
            });

    app.port(8080).multithreaded().run();
    return 0;
}