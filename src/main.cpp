#include "crow.h"
#include <crow/middlewares/cors.h> // YENİ: CORS desteği için eklendi
#include "db/DatabaseManager.h"
#include "utils/FileManager.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <exception> // Hata yakalama için eklendi

// --- MODÜL BAŞLIKLARI (ROUTES) ---
#include "routes/AuthRoutes.h"
#include "routes/UserRoutes.h"
#include "routes/ServerRoutes.h"
#include "routes/MessageRoutes.h"
#include "routes/KanbanRoutes.h"
#include "routes/WsRoutes.h"
#include "routes/AdminRoutes.h"
#include "routes/PaymentRoutes.h" 
#include "routes/UploadRoutes.h"
#include "routes/RoleRoutes.h" 
#include "routes/ReportRoutes.h"

// --- ARKA PLAN GÖREVLERİ (BACKGROUND WORKERS) ---
void backgroundTasks(DatabaseManager* db) {
    while (true) {
        try {
            db->markInactiveUsersOffline(300);
            db->processKanbanNotifications();
        }
        catch (...) {} // Arka plan çökmesini engeller
        std::this_thread::sleep_for(std::chrono::minutes(1));
    }
}

int main() {
    // Tüm sistemi TRY-CATCH bloğu içine alıyoruz ki "abort()" popup'ı çıkmasın!
    try {
        FileManager::initDirectories();

        DatabaseManager db("mysaasapp.db");
        if (!db.open()) {
            std::cerr << "[HATA] Veritabani baslatilamadi!" << std::endl;
            return -1;
        }
        db.initTables();


        std::thread bg_thread(backgroundTasks, &db);
        bg_thread.detach();

        // YENİ: CORS destekli Crow uygulaması olarak başlatıyoruz
        crow::App<crow::CORSHandler> app;

        // --- YENİ: CORS (CROSS-ORIGIN) AYARLARI ---
        auto& cors = app.get_middleware<crow::CORSHandler>();
        cors.global()
            .headers("Origin", "Content-Type", "Accept", "Authorization")
            .methods("POST"_method, "GET"_method, "OPTIONS"_method, "PUT"_method, "DELETE"_method)
            .origin("*"); // Geliştirme aşamasında her yerden gelen isteklere izin veriyoruz.

        // Bütün API Endpoint'lerini sisteme yüklüyoruz
        AuthRoutes::setup(app, db);
        UserRoutes::setup(app, db);
        ServerRoutes::setup(app, db);
        MessageRoutes::setup(app, db);
        KanbanRoutes::setup(app, db);
        WsRoutes::setup(app, db);
        AdminRoutes::setup(app, db);
        PaymentRoutes::setup(app, db);
        UploadRoutes::setup(app, db);
        RoleRoutes::setup(app, db);
        ReportRoutes::setup(app, db);

        // İstemcilere "uploads" klasöründeki dosyaları (Resim/PDF) gösterebilmek için:
        CROW_ROUTE(app, "/uploads/<string>")
            ([](const crow::request& req, crow::response& res, std::string filename) {
            res.set_static_file_info("uploads/" + filename);
            res.end();
                });

        std::cout << "[SISTEM] Backend hazir. Port 8080 dinleniyor..." << std::endl;

        // Sunucuyu başlat!
        app.port(8080).multithreaded().run();

    }
    // EĞER PORT DOLUYSA VEYA ÇÖKERSE BURASI ÇALIŞIR (Popup çıkmaz)
    catch (const std::exception& e) {
        std::cerr << "[KRITIK HATA] Sunucu cokerken yakalandi: " << e.what() << std::endl;
        std::cerr << "[COZUM] Port 8080 kullanimda olabilir. Lutfen arka planda acik kalan MySaaSApp.exe'yi kapatin." << std::endl;
        return -1;
    }
    catch (...) {
        std::cerr << "[KRITIK HATA] Bilinmeyen donanimsal veya sistemsel bir hata olustu." << std::endl;
        return -1;
    }

    return 0;
}