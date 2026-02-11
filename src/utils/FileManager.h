#pragma once
#include <string>
#include <vector>
#include <filesystem>

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

    // Dosyayı okur (Crow'un dosyayı sunması için)
    static std::string readFile(const std::string& filepath);

    // 100 MB kontrolü (100 * 1024 * 1024 byte)
    static const size_t MAX_FILE_SIZE = 104857600;
};