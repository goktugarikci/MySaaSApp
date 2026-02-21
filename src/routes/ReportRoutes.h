#pragma once
#include "crow.h"
#include "../db/DatabaseManager.h"

class ReportRoutes {
public:
    static void setup(crow::SimpleApp& app, DatabaseManager& db);
};