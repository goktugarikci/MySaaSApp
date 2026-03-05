#pragma once
#include "crow.h"
#include "../db/DatabaseManager.h"

#include <crow/middlewares/cors.h>
// Kullanıcı profili, arama, arkadaşlık ve bildirim işlemleri
class UserRoutes {
public:
    static void setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db);
};