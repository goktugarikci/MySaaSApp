#include "UploadRoutes.h"
#include "../utils/Security.h"
#include "../utils/FileManager.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

void UploadRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // ==========================================================
    // 1. SOHBET İÇİ DOSYA VE MEDYA YÜKLEME (GÜVENLİKLİ)
    // ==========================================================
    CROW_ROUTE(app, "/api/upload/chat_media").methods("POST"_method)
        ([&db](const crow::request& req) {
        try {
            // Kullanıcı doğrulaması
            if (!Security::checkAuth(req, db, false)) return crow::response(401);
            std::string myId = Security::getUserIdFromHeader(req);

            // Gelen form verilerini parse et
            crow::multipart::message msg(req);

            std::string targetId = "";
            bool isGroup = false;
            std::string fileData = "";
            std::string fileName = "file_" + Security::generateId(6);

            for (const auto& part : msg.part_map) {
                if (part.first == "target_id") {
                    targetId = part.second.body;
                }
                else if (part.first == "is_group") {
                    isGroup = (part.second.body == "true");
                }
                else if (part.first == "file") {
                    fileData = part.second.body;

                    // Dosya adını ayıkla
                    auto it = part.second.headers.find("Content-Disposition");
                    if (it != part.second.headers.end()) {
                        size_t pos = it->second.value.find("filename=");
                        if (pos != std::string::npos) {
                            fileName = it->second.value.substr(pos + 10);
                            fileName.erase(std::remove(fileName.begin(), fileName.end(), '"'), fileName.end());
                        }
                    }
                }
            }

            if (targetId.empty() || fileData.empty()) {
                return crow::response(400, "Eksik parametre. 'target_id' ve 'file' zorunludur.");
            }

            // --- GÜVENLİK BLOĞU: DOSYA UZANTISI KONTROLÜ ---
            std::string ext = "";
            size_t dotPos = fileName.find_last_of('.');
            if (dotPos != std::string::npos) {
                ext = fileName.substr(dotPos);
                // Uzantıyı küçük harfe çevir (Örn: .PNG -> .png)
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            }

            // Sadece beyaz listedeki (whitelist) uzantılara izin ver
            if (ext != ".txt" && ext != ".png" && ext != ".jpg" && ext != ".jpeg" &&
                ext != ".gif" && ext != ".pdf" && ext != ".zip" && ext != ".rar") {
                return crow::response(400, "Guvenlik ihlali: Sadece resim, pdf, txt, zip ve rar dosyalarina izin verilmektedir.");
            }
            // ------------------------------------------------

            // Dosyanın kaydedileceği doğru Hash'li klasörü bul
            std::string folderPath = "";
            if (isGroup) {
                folderPath = FileManager::generateGroupFolderPath(targetId);
            }
            else {
                folderPath = FileManager::generateChatFolderPath(myId, targetId);
            }

            // Dosyayı diske kaydet (Başına rastgele ID ekleyerek isim çakışmasını önle)
            std::string filePath = folderPath + "/" + Security::generateId(8) + "_" + fileName;
            std::ofstream out(filePath, std::ios::binary);

            if (!out.is_open()) {
                return crow::response(500, "Dosya diske kaydedilemedi.");
            }

            out << fileData;
            out.close();

            // Frontend'e başarılı yanıt dön
            crow::json::wvalue res;
            res["url"] = "/" + filePath;
            res["file_name"] = fileName;
            res["message"] = "Dosya basariyla yuklendi.";

            return crow::response(200, res);

        }
        catch (const std::exception& e) {
            CROW_LOG_ERROR << "Dosya yukleme hatasi: " << e.what();
            return crow::response(500, "Sunucu hatasi.");
        }
            });

    // ==========================================================
    // 2. KULLANICI PROFİL FOTOĞRAFI (AVATAR) YÜKLEME
    // ==========================================================
    CROW_ROUTE(app, "/api/upload/avatar").methods("POST"_method)
        ([&db](const crow::request& req) {
        try {
            if (!Security::checkAuth(req, db, false)) return crow::response(401);
            std::string myId = Security::getUserIdFromHeader(req);

            crow::multipart::message msg(req);
            std::string fileData = "";
            std::string extension = ".png";

            for (const auto& part : msg.part_map) {
                if (part.first == "file") {
                    fileData = part.second.body;

                    auto it = part.second.headers.find("Content-Type");
                    if (it != part.second.headers.end()) {
                        if (it->second.value == "image/jpeg") extension = ".jpg";
                        else if (it->second.value == "image/gif") extension = ".gif";
                        else if (it->second.value == "image/webp") extension = ".webp";
                    }
                }
            }

            if (fileData.empty()) return crow::response(400, "Dosya bulunamadi.");

            std::string folderPath = "uploads/avatars";
            if (!fs::exists(folderPath)) {
                fs::create_directories(folderPath);
            }

            std::string filePath = folderPath + "/" + myId + extension;
            std::ofstream out(filePath, std::ios::binary);
            if (!out.is_open()) return crow::response(500, "Avatar kaydedilemedi.");

            out << fileData;
            out.close();

            db.updateUserAvatar(myId, "/" + filePath);

            crow::json::wvalue res;
            res["url"] = "/" + filePath;
            res["message"] = "Avatar basariyla guncellendi.";
            return crow::response(200, res);

        }
        catch (const std::exception& e) {
            return crow::response(500, "Sunucu hatasi.");
        }
            });
}