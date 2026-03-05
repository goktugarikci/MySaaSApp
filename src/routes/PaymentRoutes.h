#pragma once
#include "crow.h"
#include "../db/DatabaseManager.h"

#include <crow/middlewares/cors.h>
class PaymentRoutes {
public:
    static void setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db);
};