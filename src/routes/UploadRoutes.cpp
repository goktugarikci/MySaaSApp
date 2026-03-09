#include "UploadRoutes.h"
#include "../utils/Security.h"
#include <crow/multipart.h>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;

void UploadRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // ==========================================================
    // MERKEZİ MEDYA VE DOSYA YÜKLEME MOTORU (MULTIPART)
    // ==========================================================
    CROW_ROUTE(app, "/api/upload").methods("POST"_method)
        ([&db](const crow::request& req) {

        // 1. Kimlik Doğrulama (Sadece giriş yapmış kullanıcılar dosya yükleyebilir)
        if (!Security::checkAuth(req, db, false)) return crow::response(401, "Yetkisiz islem.");

        // 2. Gelen verinin 'multipart/form-data' olduğunu doğrula
        crow::multipart::message file_message(req);
        if (file_message.parts.empty()) {
            return crow::response(400, "Dosya verisi bulunamadi veya yanlis format.");
        }

        std::string uploadType = "general"; // Varsayılan klasör
        std::string fileContent = "";
        std::string originalFilename = "unknown_file.bin";

        // 3. Form parçalarını (form-data) analiz et
        for (const auto& part : file_message.parts) {
            auto name_it = part.headers.find("Content-Disposition");
            if (name_it != part.headers.end()) {
                std::string header_val = name_it->second.value;

                // A) Frontend'den gelen 'type' alanı (avatars, chats, kanban)
                if (header_val.find("name=\"type\"") != std::string::npos) {
                    uploadType = part.body;
                }
                // B) Gerçek dosyanın (file) kendisi
                else if (header_val.find("name=\"file\"") != std::string::npos) {
                    fileContent = part.body;

                    // Orjinal dosya adını ayıkla
                    size_t filename_pos = header_val.find("filename=\"");
                    if (filename_pos != std::string::npos) {
                        filename_pos += 10;
                        size_t filename_end = header_val.find("\"", filename_pos);
                        if (filename_end != std::string::npos) {
                            originalFilename = header_val.substr(filename_pos, filename_end - filename_pos);
                        }
                    }
                }
            }
        }

        // 4. Dosya içeriği boş mu kontrol et
        if (fileContent.empty()) {
            return crow::response(400, "Gecerli bir dosya yuklenmedi.");
        }

        // 5. Güvenlik: Yalnızca izin verilen klasörlere yazılabilir
        if (uploadType != "avatars" && uploadType != "chats" && uploadType != "kanban") {
            uploadType = "general";
        }

        // 6. Klasör yoksa oluştur (Örn: uploads/chats)
        std::string directoryPath = "uploads/" + uploadType;
        if (!fs::exists(directoryPath)) {
            fs::create_directories(directoryPath);
        }

        // 7. Çakışmayı önlemek için benzersiz bir dosya adı oluştur (Timestamp + Rastgele ID)
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        std::string uniqueFilename = std::to_string(timestamp) + "_" + Security::generateId(6) + "_" + originalFilename;
        std::string finalPath = directoryPath + "/" + uniqueFilename;

        // 8. Dosyayı doğrudan diske yaz (RAM'i şişirmeden Binary olarak kaydeder)
        std::ofstream outFile(finalPath, std::ios::binary);
        if (outFile.is_open()) {
            outFile.write(fileContent.data(), fileContent.size());
            outFile.close();

            // 9. Frontend'e dosyanın erişilebilir URL'sini gönder
            crow::json::wvalue res;
            res["status"] = "success";
            res["url"] = "/" + finalPath; // Örn: /uploads/chats/1739023_X9A2_video.mp4
            res["type"] = uploadType;

            return crow::response(200, res);
        }

        return crow::response(500, "Sunucu hatasi: Dosya diske yazilamadi.");
            });
}