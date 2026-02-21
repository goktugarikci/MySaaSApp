#pragma once
#include "crow.h"
#include "../db/DatabaseManager.h"

class RoleRoutes {
public:
    static void setup(crow::SimpleApp& app, DatabaseManager& db);
};