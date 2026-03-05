#pragma once
#include <crow.h>
#include "../db/DatabaseManager.h"

#include <crow/middlewares/cors.h>
// Gerçek Zamanlı (Real-Time) Mesajlaşma ve Kanban işlemleri için WebSocket yönlendirme sınıfı
class WsRoutes {
public:
    static void setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db);
};