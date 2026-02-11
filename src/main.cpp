#include "crow.h"
#include <sqlite3.h>
#include <iostream>

int main() {
    // 1. SQLite Testi
    sqlite3* db;
    int rc = sqlite3_open("test.db", &db);
    if (rc) {
        std::cerr << "Veritabani hatasi: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }
    else {
        std::cout << "Veritabani basariyla acildi (SQLite Version: " << sqlite3_libversion() << ")" << std::endl;
        sqlite3_close(db);
    }

    // 2. Crow Web Sunucusu Testi
    crow::SimpleApp app;

    CROW_ROUTE(app, "/")([]() {
        return "SaaS Projesi Calisiyor! Veritabani baglantisi basarili.";
        });

    std::cout << "Sunucu 8080 portunda baslatiliyor..." << std::endl;
    app.port(8080).multithreaded().run();

    return 0;
}