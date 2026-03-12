#include "LogManager.h"
#include "FileManager.h"
#include <mutex>
#include <thread>
#include <map>
#include <fstream>
#include <chrono>
#include <filesystem>

// Statik değişkenlerin tanımlanması
std::map<std::string, std::vector<std::string>> logPool;
std::mutex logMtx;
namespace fs = std::filesystem;

void LogManager::startBackupWorker() {
    // Yedekleme klasörünü garantiye al
    if (!fs::exists("chat_data/backups")) {
        fs::create_directories("chat_data/backups");
    }

    std::thread([]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            processBackup();
        }
        }).detach();
}

void LogManager::processBackup() {
    std::lock_guard<std::mutex> lock(logMtx);
    if (logPool.empty()) return;

    for (auto& [contextId, messages] : logPool) {
        std::ofstream outFile("chat_data/backups/" + contextId + "_backup.txt", std::ios::app);
        if (outFile.is_open()) {
            for (const auto& m : messages) {
                outFile << "[" << std::time(nullptr) << "] " << m << "\n";
            }
            outFile.close();
        }
        messages.clear(); // Yazılanları temizle
    }
}

void LogManager::addToQueue(const std::string& sId, const std::string& tId, const std::string& msg) {
    std::lock_guard<std::mutex> lock(logMtx);
    std::string contextId = (sId < tId) ? sId + "_" + tId : tId + "_" + sId;
    logPool[contextId].push_back("Gonderen: " + sId + " | Sifreli Icerik: " + msg);
}