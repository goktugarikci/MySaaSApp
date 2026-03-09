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
    if (!fs::exists("chat_data")) fs::create_directory("chat_data"); // Sohbet klasörü eklendi
}

std::string FileManager::saveFile(const std::string& part_content, const std::string& original_filename, FileType type) {
    if (part_content.size() > MAX_FILE_SIZE) throw std::runtime_error("Dosya boyutu 100 MB sinirini asiyor.");
    std::string ext = fs::path(original_filename).extension().string();
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

    std::string new_filename = generateUniqueFilename(ext);
    std::string full_path = directory + new_filename;

    std::ofstream out_file(full_path, std::ios::binary);
    if (!out_file.is_open()) throw std::runtime_error("Dosya diske yazilamadi.");
    out_file.write(part_content.data(), part_content.size());
    out_file.close();

    return url_prefix + new_filename;
}

std::string FileManager::readFile(const std::string& filepath) {
    std::ifstream ifs("public" + filepath, std::ios::binary);
    if (!ifs.is_open()) return "";
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}


std::string FileManager::getChatFilePath(const std::string& userA, const std::string& userB) {
    std::string first = (userA < userB) ? userA : userB;
    std::string second = (userA < userB) ? userB : userA;
    if (!fs::exists("chat_data")) fs::create_directory("chat_data");
    return "chat_data/chat_" + first + "_" + second + ".json";
}

bool FileManager::saveChatMessage(const std::string& userA, const std::string& userB, const std::string& senderId, const std::string& msgId, const std::string& contentType, const std::string& content, const std::string& mediaPath) {
    std::string filePath = getChatFilePath(userA, userB);
    std::vector<crow::json::wvalue> newArray;

    if (fs::exists(filePath)) {
        std::ifstream inFile(filePath);
        if (inFile.is_open()) {
            std::stringstream buffer; buffer << inFile.rdbuf();
            auto parsed = crow::json::load(buffer.str());
            inFile.close();
            if (parsed && parsed.t() == crow::json::type::List) {
                for (const auto& item : parsed) newArray.push_back(crow::json::wvalue(item));
            }
        }
    }

    crow::json::wvalue newMsg;
    newMsg["message_id"] = msgId;
    newMsg["sender_id"] = senderId;
    newMsg["content_type"] = contentType;
    newMsg["content"] = content;
    newMsg["media_path"] = mediaPath;
    newMsg["timestamp"] = "2026-03-09 14:30:00";
    newMsg["is_read"] = false;
    newMsg["is_recalled"] = false;
    newArray.push_back(std::move(newMsg));

    std::ofstream outFile(filePath, std::ios::trunc);
    if (outFile.is_open()) {
        outFile << crow::json::wvalue(newArray).dump();
        outFile.close();
        return true;
    }
    return false;
}

// E2291 ÇÖZÜMÜ: Dosyayı düz metin olarak okur
std::string FileManager::getChatHistoryString(const std::string& userA, const std::string& userB) {
    std::string filePath = getChatFilePath(userA, userB);
    if (fs::exists(filePath)) {
        std::ifstream inFile(filePath);
        if (inFile.is_open()) {
            std::stringstream buffer; buffer << inFile.rdbuf();
            return buffer.str();
        }
    }
    return "[]";
}

bool FileManager::recallChatMessage(const std::string& userA, const std::string& userB, const std::string& msgId) {
    std::string filePath = getChatFilePath(userA, userB);
    if (!fs::exists(filePath)) return false;

    std::ifstream inFile(filePath);
    std::stringstream buffer; buffer << inFile.rdbuf();
    inFile.close();

    auto parsed = crow::json::load(buffer.str());
    if (!parsed || parsed.t() != crow::json::type::List) return false;

    std::vector<crow::json::wvalue> newArray;
    bool found = false;
    for (const auto& item : parsed) {
        crow::json::wvalue msg(item);
        if (item.has("message_id") && item["message_id"].s() == msgId) {
            msg["is_recalled"] = true;
            msg["content"] = "";
            found = true;
        }
        newArray.push_back(std::move(msg));
    }

    if (found) {
        std::ofstream outFile(filePath, std::ios::trunc);
        outFile << crow::json::wvalue(newArray).dump();
        return true;
    }
    return false;
}