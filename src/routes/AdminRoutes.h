#pragma once
#include <crow.h>
#include "../db/DatabaseManager.h"
#include <crow/middlewares/cors.h>

// Süper Admin paneli için sistem logları ve arşivlenmiş verilerin yönetimi
class AdminRoutes {
public:
    static void setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db);
};