#pragma once
#include <string>
#include <vector>
#include <filesystem>

#include <nlohmann/json.hpp>

class FileManager {
public:
    // Dosya türleri
    enum class FileType {
        AVATAR,
        ATTACHMENT
    };

    // Klasörleri oluşturur (Program açılırken çağırılmalı)
    static void initDirectories();

    // Dosyayı kaydeder ve erişim URL'sini döner
    // part_content: Dosyanın ham verisi
    // filename: Orijinal dosya adı
    static std::string saveFile(const std::string& part_content, const std::string& original_filename, FileType type);
    static std::string generateChatFolderPath(const std::string& u1, const std::string& u2);
    static std::string generateGroupFolderPath(const std::string& groupId);
    static bool savePrivateMessageJSON(const std::string& sId, const std::string& tId, const std::string& encMsg, const std::string& contentType);
    static bool saveGroupMessageJSON(const std::string& groupId, const std::string& senderId, const std::string& encMsg, const std::string& contentType, int totalMembers);
    // --- YENİ EKLENECEKLER ---
    static bool markMessagesAsRead(const std::string& senderId, const std::string& targetId);
    static std::string getPrivateChatHistory(const std::string& u1, const std::string& u2);
    static std::string getGroupChatHistory(const std::string& groupId);
    // Dosyayı okur (Crow'un dosyayı sunması için)
    static std::string readFile(const std::string& filepath);

    // 100 MB kontrolü (100 * 1024 * 1024 byte)
    static const size_t MAX_FILE_SIZE = 104857600;
};