#pragma once
#include <string>
#include <nlohmann/json.hpp>

class FileManager {
public:
    // Gerekli ana klasörleri (uploads, sohbet, grups vb.) oluşturur
    static void initDirectories();

    // ==========================================================
    // YENİ MİMARİ: MESAJ KAYDETME FONKSİYONLARI
    // ==========================================================

    // Birebir (DM) mesajları kaydeder
    static bool savePrivateMessage(const std::string& senderId, const std::string& targetId, const std::string& encryptedMsg, const std::string& contentType);

    // Grup/Sunucu kanallarındaki mesajları kaydeder
    static bool saveGroupMessage(const std::string& groupId, const std::string& channelId, const std::string& senderId, const std::string& encryptedMsg, const std::string& contentType);

    // ==========================================================
    // YENİ MİMARİ: GEÇMİŞİ GETİRME FONKSİYONLARI
    // ==========================================================

    // İki kullanıcı arasındaki özel sohbet geçmişini getirir
    static std::string getPrivateChatHistory(const std::string& user1, const std::string& user2);

    // Bir grubun belirli bir kanalındaki sohbet geçmişini getirir
    static std::string getGroupChatHistory(const std::string& groupId, const std::string& channelId);

    // ==========================================================
    // EKSTRALAR (SİLME / GİZLEME)
    // ==========================================================

    // Bir mesajı global olarak "Silindi" durumuna çeker
    static bool toggleMessageVisibility(const std::string& contextId, const std::string& msgId, bool isGroup, const std::string& groupId = "");
};