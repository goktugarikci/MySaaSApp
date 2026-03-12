#include "FileManager.h"
#include <fstream>
#include <random>
#include <sstream>
#include <iostream>
#include <chrono>
#include <nlohmann/json.hpp>
#include <mutex> // Eğer yoksa bunu ekleyin


namespace fs = std::filesystem;

std::mutex fileMtx; // JSON dosyası yazma kilidimiz

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
// Klasör Yolu Oluşturucu (DM İçin)
std::string FileManager::generateChatFolderPath(const std::string& u1, const std::string& u2) {
    // Aynı sohbet için aynı klasör sırası garantisi
    std::string first = (u1 < u2) ? u1 : u2;
    std::string second = (u1 < u2) ? u2 : u1;

    // Hashlenmiş klasör ismi (Örn: Chat_3a5f..._8b1c...)
    std::string folderName = "Chat_" + std::to_string(std::hash<std::string>{}(first)) + "_" + std::to_string(std::hash<std::string>{}(second));
    std::string path = "uploads/sohbet/" + folderName;

    if (!fs::exists(path)) fs::create_directories(path);
    return path;
}

// Klasör Yolu Oluşturucu (Grup İçin)
std::string FileManager::generateGroupFolderPath(const std::string& groupId) {
    std::string path = "uploads/grups/" + groupId;
    if (!fs::exists(path)) fs::create_directories(path);
    return path;
}

// DM JSON Kayıt Motoru (Senin Şeman)
bool FileManager::savePrivateMessageJSON(const std::string& sId, const std::string& tId, const std::string& encMsg, const std::string& contentType) {
    try {
        std::lock_guard<std::mutex> lock(fileMtx); // Daha önce tanımladığınız file mutex
        std::string folderPath = generateChatFolderPath(sId, tId);
        std::string filePath = folderPath + "/history.json";

        nlohmann::json history = nlohmann::json::array();
        if (fs::exists(filePath)) {
            std::ifstream inFile(filePath);
            if (inFile.is_open()) {
                inFile >> history;
                inFile.close();
            }
        }

        // Yeni Mesaj Objesi
        nlohmann::json newMsg;
        newMsg["gönderenID"] = sId;
        newMsg["alıcıID"] = tId;
        newMsg["Mesaj"] = encMsg; // Şifreli
        newMsg["MesajDurumuOkundu"] = false;
        newMsg["MesajSilinmeDurumu"] = "null"; // Başlangıçta silinmemiş
        newMsg["MesajGönderimTarihi"] = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        newMsg["MesajEmoji"] = "null";
        newMsg["MesajİçerikDurumu"] = contentType;

        history.push_back(newMsg);

        std::ofstream outFile(filePath);
        outFile << history.dump(4);
        return true;
    }
    catch (...) { return false; }
}

// Grup JSON Kayıt Motoru (Senin Şeman)
bool FileManager::saveGroupMessageJSON(const std::string& groupId, const std::string& senderId, const std::string& encMsg, const std::string& contentType, int totalMembers) {
    try {
        std::lock_guard<std::mutex> lock(fileMtx);
        std::string folderPath = generateGroupFolderPath(groupId);
        std::string filePath = folderPath + "/history.json"; // Veya hash(ChatRoom_ID).json

        nlohmann::json history = nlohmann::json::array();
        if (fs::exists(filePath)) {
            std::ifstream inFile(filePath);
            if (inFile.is_open()) { inFile >> history; inFile.close(); }
        }

        nlohmann::json newMsg;
        newMsg["gönderenID"] = senderId;
        newMsg["OkunduBilgisiKisi"] = nlohmann::json::array(); // Boş dizi
        newMsg["okunmadıBilgisi"] = totalMembers - 1; // Gönderen hariç herkes
        newMsg["Mesaj"] = encMsg;
        newMsg["MesajİçerikDurumu"] = contentType;
        newMsg["MesajSilinmeDurumu"] = false;
        newMsg["MesajEmoji"] = "null";
        newMsg["MesajGönderimTarihi"] = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());

        history.push_back(newMsg);

        std::ofstream outFile(filePath);
        outFile << history.dump(4);
        return true;
    }
    catch (...) { return false; }
}