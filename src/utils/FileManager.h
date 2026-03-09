#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <crow/json.h>

class FileManager {
public:
    enum class FileType { AVATAR, ATTACHMENT };
    static void initDirectories();
    static std::string saveFile(const std::string& part_content, const std::string& original_filename, FileType type);
    static std::string readFile(const std::string& filepath);
    static const size_t MAX_FILE_SIZE = 104857600;

    // ==========================================================
    // ŞİFRELİ SOHBET FONKSİYONLARI (Artık Sınıfın İçinde!)
    // ==========================================================
    static std::string getChatFilePath(const std::string& userA, const std::string& userB);
    static bool saveChatMessage(const std::string& userA, const std::string& userB, const std::string& senderId, const std::string& msgId, const std::string& contentType, const std::string& content, const std::string& mediaPath);

    // E2291 ÇÖZÜMÜ: wvalue yerine String dönüyoruz!
    static std::string getChatHistoryString(const std::string& userA, const std::string& userB);
    static bool recallChatMessage(const std::string& userA, const std::string& userB, const std::string& msgId);
};