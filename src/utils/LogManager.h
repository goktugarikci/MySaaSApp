#pragma once
#include <string>
#include <vector>

class LogManager {
public:
    // 10 saniyelik arka plan döngüsünü başlatır
    static void startBackupWorker();

    // RAM'deki mesajları diske yazar
    static void processBackup();

    // Yeni gelen mesajı RAM (Kuyruk) havuzuna ekler
    static void addToQueue(const std::string& sId, const std::string& tId, const std::string& msg);
};