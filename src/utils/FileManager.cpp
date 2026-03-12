#include "FileManager.h"
#include <fstream>
#include <random>
#include <sstream>
#include <iostream>
#include <chrono>
#include <sstream>
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

// JSON dosyasına yeni mesajı ekler
bool saveChatMessage(const std::string& userA, const std::string& userB, const crow::json::wvalue& messageObj) {
    std::string filePath = getChatFilePath(userA, userB);
    std::vector<crow::json::rvalue> existingMessages;

    // Dosya varsa oku
    if (fs::exists(filePath)) {
        std::ifstream inFile(filePath);
        if (inFile.is_open()) {
            std::stringstream buffer;
            buffer << inFile.rdbuf();
            auto parsed = crow::json::load(buffer.str());
            if (parsed && parsed.t() == crow::json::type::List) {
                for (const auto& item : parsed) {
                    existingMessages.push_back(item);
                }
            }
            inFile.close();
        }
    }

    // Yeni mesajı listeye ekle (Bunu yaparken crow::json::wvalue kullanacağız)
    std::vector<crow::json::wvalue> newArray;
    for (const auto& oldMsg : existingMessages) {
        newArray.push_back(oldMsg);
    }

    // messageObj (yeni atılan mesaj) listeye girer
    newArray.push_back(messageObj);

    // Dosyaya geri yaz
    std::ofstream outFile(filePath, std::ios::trunc);
    if (outFile.is_open()) {
        outFile << crow::json::wvalue(newArray).dump();
        outFile.close();
        return true;
    }
    return false;
}

// Tüm geçmişi okur ve arayüze (Frontend) göndermeye hazır hale getirir
crow::json::wvalue getChatHistory(const std::string& userA, const std::string& userB) {
    std::string filePath = getChatFilePath(userA, userB);

    if (fs::exists(filePath)) {
        std::ifstream inFile(filePath);
        if (inFile.is_open()) {
            std::stringstream buffer;
            buffer << inFile.rdbuf();
            auto parsed = crow::json::load(buffer.str());
            inFile.close();
            if (parsed) {
                return crow::json::wvalue(parsed);
            }
        }
    }
    // Dosya yoksa veya boşsa boş bir liste döndür
    return crow::json::wvalue(std::vector<crow::json::wvalue>());
}

// İki kullanıcı için benzersiz, alfabetik bir dosya adı üretir (Alice ve Bob / Bob ve Alice hep aynı dosyayı verir)
std::string getChatFilePath(const std::string& userA, const std::string& userB) {
    std::string first = (userA < userB) ? userA : userB;
    std::string second = (userA < userB) ? userB : userA;

    // Klasör yoksa oluştur
    if (!fs::exists("chat_data")) {
        fs::create_directory("chat_data");
    }
    return "chat_data/chat_" + first + "_" + second + ".json";
}
