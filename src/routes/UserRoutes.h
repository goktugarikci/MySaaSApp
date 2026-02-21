#pragma once
#include "crow.h"
#include "../db/DatabaseManager.h"

// Kullanıcı profili, arama, arkadaşlık ve bildirim işlemleri
class UserRoutes {
public:
    static void setup(crow::SimpleApp& app, DatabaseManager& db);
};