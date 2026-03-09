#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class FileManager {
public:
    // SİSTEM BAŞLATICI: Gerekli tüm klasörleri otomatik oluşturur
    static void initDirectories();

    // Mesaj kaydetme
    static bool saveChatMessage(const std::string& chatContext, const std::string& senderId,
        const std::string& msgId, const std::string& type,
        const std::string& encryptedContent, const std::string& mediaPath,
        bool isServer);

    // Geçmişi getirme
    static std::string getChatHistory(const std::string& chatContext, bool isServer);

    // Mesaj görünürlüğünü değiştirme (Admin gizleme)
    static bool toggleMessageVisibility(const std::string& chatContext, const std::string& msgId,
        bool isServer, bool visible);

    // Sunucu dosyası isimlendirme
    static bool renameServerFile(const std::string& oldId, const std::string& newId);
};