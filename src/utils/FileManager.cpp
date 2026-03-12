#include "FileManager.h"
#include "Security.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <ctime>
#include <iostream>

namespace fs = std::filesystem;

void FileManager::initDirectories() {
    try {
        // Yeni sisteme uygun klasör ağacı
        std::vector<std::string> dirs = {
            "uploads",
            "uploads/avatars",
            "uploads/kanban",
            "uploads/sohbet", // DM (Birebir) mesajlar için
            "uploads/grups"   // Grup/Sunucu mesajları için
        };

        for (const auto& dir : dirs) {
            if (!fs::exists(dir)) {
                fs::create_directories(dir);
            }
        }
    }
    catch (...) {
        // Hata loglanabilir
    }
}

// ==========================================================
// BİREBİR (DM) SOHBET KAYDEDİCİ
// ==========================================================
bool FileManager::savePrivateMessage(const std::string& senderId, const std::string& targetId, const std::string& encryptedMsg, const std::string& contentType) {
    try {
        // 1. Klasör adını alfabetik sıraya göre deterministik olarak belirle
        std::string first = (senderId < targetId) ? senderId : targetId;
        std::string second = (senderId < targetId) ? targetId : senderId;

        std::string h1 = Security::hashString(first);
        std::string h2 = Security::hashString(second);

        std::string folderPath = "uploads/sohbet/Chat_" + h1 + "_" + h2;

        if (!fs::exists(folderPath)) {
            fs::create_directories(folderPath);
        }

        std::string filePath = folderPath + "/history.json";
        nlohmann::json history = nlohmann::json::array();

        // 2. Mevcut geçmişi oku
        if (fs::exists(filePath)) {
            std::ifstream inFile(filePath);
            if (inFile.is_open()) {
                try { inFile >> history; }
                catch (...) { history = nlohmann::json::array(); }
                inFile.close();
            }
        }

        // 3. İstenen JSON Şemasına göre yeni nesneyi oluştur
        nlohmann::json newMsg;
        newMsg["MesajID"] = Security::generateId(18); // Silme/Düzenleme işlemleri için benzersiz ID
        newMsg["gönderenID"] = senderId;
        newMsg["alıcıID"] = targetId;
        newMsg["Mesaj"] = encryptedMsg;
        newMsg["MesajDurumuOkundu"] = false;
        newMsg["MesajSilinmeDurumu"] = "Yok"; // "Kişiden", "Alıcıdan", "Global"
        newMsg["MesajGönderimTarihi"] = std::to_string(std::time(nullptr));
        newMsg["MesajEmoji"] = "null";
        newMsg["MesajİçerikDurumu"] = contentType; // "Video", "Fotoğraf", "Text" vb.

        history.push_back(newMsg);

        // 4. Dosyaya yaz (Pretty Print 4 boşluk)
        std::ofstream outFile(filePath);
        if (!outFile.is_open()) return false;
        outFile << history.dump(4);
        outFile.close();

        return true;
    }
    catch (...) {
        return false;
    }
}

// ==========================================================
// GRUP / SUNUCU SOHBET KAYDEDİCİ
// ==========================================================
bool FileManager::saveGroupMessage(const std::string& groupId, const std::string& channelId, const std::string& senderId, const std::string& encryptedMsg, const std::string& contentType) {
    try {
        // 1. Klasör yolunu belirle
        std::string folderPath = "uploads/grups/" + groupId;
        if (!fs::exists(folderPath)) {
            fs::create_directories(folderPath);
        }

        std::string channelHash = Security::hashString(channelId);
        std::string filePath = folderPath + "/" + channelHash + ".json";

        nlohmann::json history = nlohmann::json::array();

        // 2. Mevcut geçmişi oku
        if (fs::exists(filePath)) {
            std::ifstream inFile(filePath);
            if (inFile.is_open()) {
                try { inFile >> history; }
                catch (...) { history = nlohmann::json::array(); }
                inFile.close();
            }
        }

        // 3. İstenen JSON Şemasına göre yeni grup mesajını oluştur
        nlohmann::json newMsg;
        newMsg["MesajID"] = Security::generateId(18);
        newMsg["gönderenID"] = senderId;
        newMsg["OkunduBilgisiKisi"] = nlohmann::json::array();
        newMsg["okunmadıBilgisi"] = 0;
        newMsg["Mesaj"] = encryptedMsg;
        newMsg["MesajİçerikDurumu"] = contentType;
        newMsg["MesajSilinmeDurumu"] = false; // Gruplarda silindi mi? true/false
        newMsg["MesajEmoji"] = "null";
        newMsg["MesajGönderimTarihi"] = std::to_string(std::time(nullptr));

        history.push_back(newMsg);

        // 4. Dosyaya yaz
        std::ofstream outFile(filePath);
        if (!outFile.is_open()) return false;
        outFile << history.dump(4);
        outFile.close();

        return true;
    }
    catch (...) {
        return false;
    }
}

// ==========================================================
// GEÇMİŞ OKUMA FONKSİYONLARI (HAM JSON DÖNDÜRÜR)
// ==========================================================
std::string FileManager::getPrivateChatHistory(const std::string& user1, const std::string& user2) {
    std::string first = (user1 < user2) ? user1 : user2;
    std::string second = (user1 < user2) ? user2 : user1;

    std::string h1 = Security::hashString(first);
    std::string h2 = Security::hashString(second);
    std::string filePath = "uploads/sohbet/Chat_" + h1 + "_" + h2 + "/history.json";

    if (!fs::exists(filePath)) return "[]";

    try {
        std::ifstream inFile(filePath);
        if (!inFile.is_open()) return "[]";
        std::stringstream buffer;
        buffer << inFile.rdbuf();
        return buffer.str();
    }
    catch (...) {
        return "[]";
    }
}

std::string FileManager::getGroupChatHistory(const std::string& groupId, const std::string& channelId) {
    std::string channelHash = Security::hashString(channelId);
    std::string filePath = "uploads/grups/" + groupId + "/" + channelHash + ".json";

    if (!fs::exists(filePath)) return "[]";

    try {
        std::ifstream inFile(filePath);
        if (!inFile.is_open()) return "[]";
        std::stringstream buffer;
        buffer << inFile.rdbuf();
        return buffer.str();
    }
    catch (...) {
        return "[]";
    }
}

// ==========================================================
// MESAJ SİLME / GÖRÜNÜRLÜK GÜNCELLEME
// ==========================================================
bool FileManager::toggleMessageVisibility(const std::string& targetId, const std::string& msgId, bool isGroup, const std::string& groupId) {
    try {
        std::string filePath;

        // Klasör yolunu bul
        if (isGroup) {
            std::string channelHash = Security::hashString(targetId);
            filePath = "uploads/grups/" + groupId + "/" + channelHash + ".json";
        }
        else {
            // Şimdilik targetId içinde user1_user2 gönderildiğini varsayıyoruz, 
            // Veya api katmanından doğru formatlanmış path gelebilir.
            return false; // Özel mesaj silme mantığını arayüzden gelen veriye göre uyarlayabilirsiniz.
        }

        if (!fs::exists(filePath)) return false;

        nlohmann::json history;
        {
            std::ifstream inFile(filePath);
            if (!inFile.is_open()) return false;
            inFile >> history;
            inFile.close();
        }

        bool found = false;
        for (auto& msg : history) {
            if (msg.contains("MesajID") && msg["MesajID"].get<std::string>() == msgId) {
                if (isGroup) {
                    msg["MesajSilinmeDurumu"] = true; // Grup mesajları için boolean
                }
                else {
                    msg["MesajSilinmeDurumu"] = "Global"; // DM'ler için string durumu
                }
                found = true;
                break;
            }
        }

        if (found) {
            std::ofstream outFile(filePath);
            outFile << history.dump(4);
            return true;
        }
        return false;
    }
    catch (...) {
        return false;
    }
}