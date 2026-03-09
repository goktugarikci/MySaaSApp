#include "FileManager.h"
#include "Security.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <ctime>

namespace fs = std::filesystem;

// Mesajı JSON dosyasına şifreli olarak kaydeder
bool FileManager::saveChatMessage(const std::string& chatContext, const std::string& senderId, const std::string& msgId, const std::string& type, const std::string& encryptedContent, const std::string& mediaPath, bool isServer) {
    try {
        // 1. Klasör hiyerarşisini oluştur
        std::string directory = isServer ? "uploads/servers" : "chat_data";
        if (!fs::exists(directory)) fs::create_directories(directory);

        // 2. Dosya adını belirle (DM ise context zaten dm_ID1_ID2 formatında gelir)
        std::string filename = directory + "/" + chatContext + ".json";
        nlohmann::json chatHistory = nlohmann::json::array();

        // 3. Mevcut geçmişi oku
        if (fs::exists(filename)) {
            std::ifstream inFile(filename);
            if (inFile.is_open()) {
                try {
                    inFile >> chatHistory;
                }
                catch (...) {
                    chatHistory = nlohmann::json::array();
                }
                inFile.close();
            }
        }

        // 4. Yeni mesaj objesini hazırla
        nlohmann::json newMsg;
        newMsg["message_id"] = msgId;
        newMsg["sender_id"] = senderId;
        newMsg["content_type"] = type;
        newMsg["content"] = encryptedContent; // Zaten şifrelenmiş olarak gelir
        newMsg["media_path"] = mediaPath;
        newMsg["timestamp"] = std::time(nullptr);
        newMsg["is_visible"] = true; // Admin silme kontrolü için varsayılan true

        chatHistory.push_back(newMsg);

        // 5. Dosyayı diske yaz (Pretty Print: 4)
        std::ofstream outFile(filename);
        if (!outFile.is_open()) return false;
        outFile << chatHistory.dump(4);
        outFile.close();

        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

// Sohbet geçmişini ham string (JSON) olarak döndürür
std::string FileManager::getChatHistory(const std::string& chatContext, bool isServer) {
    std::string directory = isServer ? "uploads/servers" : "chat_data";
    std::string filename = directory + "/" + chatContext + ".json";

    if (!fs::exists(filename)) return "[]";

    try {
        std::ifstream inFile(filename);
        if (!inFile.is_open()) return "[]";
        std::stringstream buffer;
        buffer << inFile.rdbuf();
        return buffer.str();
    }
    catch (...) {
        return "[]";
    }
}

// Mesajı kalıcı olarak silmez, sadece görünmez yapar (Admin yetkisi için)
bool FileManager::toggleMessageVisibility(const std::string& chatContext, const std::string& msgId, bool isServer, bool visible) {
    try {
        std::string directory = isServer ? "uploads/servers" : "chat_data";
        std::string filename = directory + "/" + chatContext + ".json";

        if (!fs::exists(filename)) return false;

        nlohmann::json chatHistory;
        {
            std::ifstream inFile(filename);
            if (!inFile.is_open()) return false;
            inFile >> chatHistory;
            inFile.close();
        }

        bool found = false;
        for (auto& msg : chatHistory) {
            if (msg.contains("message_id") && msg["message_id"].get<std::string>() == msgId) {
                msg["is_visible"] = visible;
                found = true;
                break;
            }
        }

        if (found) {
            std::ofstream outFile(filename);
            if (!outFile.is_open()) return false;
            outFile << chatHistory.dump(4);
            outFile.close();
            return true;
        }
        return false;
    }
    catch (...) {
        return false;
    }
}

// Bir dosyanın adını değiştirir (Örn: Sunucu adı değiştiğinde klasör güncelleme)
bool FileManager::renameServerFile(const std::string& oldId, const std::string& newId) {
    try {
        std::string oldPath = "uploads/servers/" + oldId + ".json";
        std::string newPath = "uploads/servers/" + newId + ".json";
        if (fs::exists(oldPath)) {
            fs::rename(oldPath, newPath);
            return true;
        }
        return false;
    }
    catch (...) {
        return false;
    }
}