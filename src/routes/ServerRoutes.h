#pragma once
#include <crow.h>
#include "../db/DatabaseManager.h"

#include <crow/middlewares/cors.h>
// Sunucu (Workspace), Kanal ve Üye işlemleri için yönlendirme sınıfı
class ServerRoutes {
public:
    static void setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db);
};