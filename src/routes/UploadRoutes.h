#pragma once
#include "crow.h"
#include "../db/DatabaseManager.h"

class UploadRoutes {
public:
    static void setup(crow::SimpleApp& app, DatabaseManager& db);
};