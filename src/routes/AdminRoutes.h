#pragma once
#include <crow.h>
#include "../db/DatabaseManager.h"

// Süper Admin paneli için sistem logları ve arşivlenmiş verilerin yönetimi
class AdminRoutes {
public:
    static void setup(crow::SimpleApp& app, DatabaseManager& db);
};