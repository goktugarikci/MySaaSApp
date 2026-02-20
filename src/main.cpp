#include "crow.h"
#include "db/DatabaseManager.h"
#include "utils/FileManager.h"
#include <thread>
#include <chrono>
#include <iostream>

// --- MODÜL BAŞLIKLARI (ROUTES) ---
#include "routes/AuthRoutes.h"
#include "routes/UserRoutes.h"
#include "routes/ServerRoutes.h"
#include "routes/MessageRoutes.h"
#include "routes/KanbanRoutes.h"
#include "routes/WsRoutes.h"

// --- ARKA PLAN GÖREVLERİ (BACKGROUND WORKERS) ---
// Sunucu çalıştığı sürece arka planda asenkron olarak iş yapan döngü
void backgroundTasks(DatabaseManager* db) {
    while (true) {
        // 1. Inaktif kullanıcıları Offline durumuna çek (Örn: Son 5 dk işlem yapmayanlar)
        db->markInactiveUsersOffline(300);

        // 2. Kanban kartlarının sürelerini kontrol et ve yaklaşan/geçen görevler için bildirim gönder
        db->processKanbanNotifications();

        // Bu işlemleri her 1 dakikada bir tekrarla
        std::this_thread::sleep_for(std::chrono::minutes(1));
    }
}

int main() {
    // 1. Gerekli Klasörleri Oluştur (Uploads, Avatars, Attachments vb.)
    FileManager::initDirectories();

    // 2. Veritabanı Bağlantısını Kur ve Tabloları Hazırla
    DatabaseManager db("mysaasapp.db");
    if (!db.open()) {
        std::cerr << "[KRİTİK HATA] Veritabani baslatilamadi!" << std::endl;
        return -1;
    }
    db.initTables();

    // 3. Arka Plan Görevlerini (Worker Thread) Başlat
    std::thread bg_thread(backgroundTasks, &db);
    bg_thread.detach(); // Ana thread'i bloklamaması için detach ediyoruz

    // 4. Crow Web Sunucusunu Başlat
    crow::SimpleApp app;

    // 5. API MODÜLLERİNİ (ENDPOINT'LERİ) SİSTEME YÜKLE
    AuthRoutes::setup(app, db);
    UserRoutes::setup(app, db);
    ServerRoutes::setup(app, db);
    MessageRoutes::setup(app, db);
    KanbanRoutes::setup(app, db);
    WsRoutes::setup(app, db);

    // 6. Başlangıç Bilgisi ve Sunucuyu Çalıştırma
    std::cout << "======================================================" << std::endl;
    std::cout << " [MySaaSApp] Backend Sunucusu Basariyla Baslatildi" << std::endl;
    std::cout << " Mimari : Moduler (REST + WebSocket)" << std::endl;
    std::cout << " Port   : 8080 | Multi-thread: Aktif" << std::endl;
    std::cout << "======================================================" << std::endl;

    app.port(8080).multithreaded().run();

    return 0;
}