#pragma once
#include <crow.h>
#include <crow/websocket.h>
#include "../db/DatabaseManager.h"

class WsRoutes {
public:
    static void setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db);
};