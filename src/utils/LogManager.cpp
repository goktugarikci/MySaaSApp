#include <mutex>
#include <thread>
#include <map>
#include <fstream>
#include "FileManager.h"

class LogManager {
private:
    static std::map<std::string, std::vector<std::string>> logPool; // ContextID -> Mesajlar
    static std::mutex logMtx;

public:
    static void startBackupWorker() {
        std::thread([]() {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                processBackup();
            }
            }).detach();
    }

    static void processBackup() {
        std::lock_guard<std::mutex> lock(logMtx);
        if (logPool.empty()) return;

        for (auto& [contextId, messages] : logPool) {
            // FileManager üzerinden toplu yazma işlemi yap
            // Örneğin: chat_data/backup_contextId.txt
            std::ofstream outFile("chat_data/backups/" + contextId + ".txt", std::ios::app);
            for (const auto& m : messages) {
                outFile << "[" << std::time(nullptr) << "] " << m << "\n";
            }
            messages.clear(); // Yazılanları temizle
        }
    }

    static void addToQueue(const std::string& sId, const std::string& tId, const std::string& msg) {
        std::lock_guard<std::mutex> lock(logMtx);
        std::string contextId = (sId < tId) ? sId + "_" + tId : tId + "_" + sId;
        logPool[contextId].push_back(msg);
    }
};