#include "FileManager.h"
#include <fstream>
#include <random>
#include <sstream>
#include <iostream>
#include <chrono>

namespace fs = std::filesystem;

// Rastgele dosya ismi üretici (UUID benzeri)
std::string generateUniqueFilename(const std::string& extension) {
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 9999);

    std::stringstream ss;
    ss << now << "_" << dis(gen) << extension;
    return ss.str();
}

void FileManager::initDirectories() {
    if (!fs::exists("public/uploads/avatars")) fs::create_directories("public/uploads/avatars");
    if (!fs::exists("public/uploads/attachments")) fs::create_directories("public/uploads/attachments");
}

std::string FileManager::saveFile(const std::string& part_content, const std::string& original_filename, FileType type) {
    // 1. Boyut Kontrolü
    if (part_content.size() > MAX_FILE_SIZE) {
        throw std::runtime_error("Dosya boyutu 100 MB sinirini asiyor.");
    }

    // 2. Uzantıyı Al
    std::string ext = fs::path(original_filename).extension().string();

    // 3. Hedef Klasörü Belirle
    std::string directory;
    std::string url_prefix;

    if (type == FileType::AVATAR) {
        directory = "public/uploads/avatars/";
        url_prefix = "/uploads/avatars/";
    }
    else {
        directory = "public/uploads/attachments/";
        url_prefix = "/uploads/attachments/";
    }

    // 4. Benzersiz İsim Oluştur
    std::string new_filename = generateUniqueFilename(ext);
    std::string full_path = directory + new_filename;

    // 5. Dosyayı Diske Yaz
    std::ofstream out_file(full_path, std::ios::binary);
    if (!out_file.is_open()) {
        throw std::runtime_error("Dosya diske yazilamadi.");
    }
    out_file.write(part_content.data(), part_content.size());
    out_file.close();

    // 6. Web'den erişilecek yolu dön
    return url_prefix + new_filename;
}

std::string FileManager::readFile(const std::string& filepath) {
    std::ifstream ifs("public" + filepath, std::ios::binary);
    if (!ifs.is_open()) return "";

    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}