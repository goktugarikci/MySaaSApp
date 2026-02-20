#pragma once
#include <crow.h>
#include "../db/DatabaseManager.h"

// Sunucu (Workspace), Kanal ve Üye işlemleri için yönlendirme sınıfı
class ServerRoutes {
public:
    static void setup(crow::SimpleApp& app, DatabaseManager& db);
};