#pragma once
#include <crow.h>
#include "../db/DatabaseManager.h"

class MessageRoutes {
public:
    static void setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db);
};