#include "crow.h"
#include "db/DatabaseManager.h"
#include "utils/FileManager.h"
#include <thread>
#include <chrono>

// Rota (Modül) Başlık Dosyaları
#include "routes/AdminRoutes.h"
#include "routes/AuthRoutes.h"
#include "routes/KanbanRoutes.h"
#include "routes/MessageRoutes.h"
#include "routes/ServerRoutes.h"
#include "routes/UserRoutes.h"
#include "routes/WsRoutes.h"
#include "routes/PaymentRoutes.h"
#include "routes/UploadRoutes.h"
#include "routes/RoleRoutes.h"
#include "routes/ReportRoutes.h"

int main() {
    // 1. Gerekli Klasörleri Oluştur
    FileManager::initDirectories();

    // 2. Veritabanı Bağlantısı
    DatabaseManager db("mysaasapp.db");
    if (!db.open()) return -1;
    db.initTables();

    crow::SimpleApp app;

    // 3. Modülleri (Rotaları) Sisteme Yükle
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
    // 4. Arka Plan Temizlik ve Bildirim Botları


    std::thread cleanupThread([&db]() {
        while (true) { std::this_thread::sleep_for(std::chrono::seconds(15)); db.markInactiveUsersOffline(60); }
        });
    cleanupThread.detach();

    std::thread kanbanTimerThread([&db]() {
        while (true) { std::this_thread::sleep_for(std::chrono::minutes(1)); db.processKanbanNotifications(); }
        });
    kanbanTimerThread.detach();

    // İstemcilere "uploads" klasöründeki dosyaları statik olarak servis et (Örn: /uploads/resim1.png)
    CROW_ROUTE(app, "/uploads/<string>")
        ([](const crow::request& req, crow::response& res, std::string filename) {
        res.set_static_file_info("uploads/" + filename);
        res.end();
            });

    // 5. Sunucuyu Başlat
    std::cout << "MySaaSApp (Moduler Yapi) Basariyla Calisiyor: http://localhost:8080" << std::endl;
    app.port(8080).multithreaded().run();

    return 0;
}