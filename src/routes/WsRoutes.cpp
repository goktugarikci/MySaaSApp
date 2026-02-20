#pragma once
#include <crow.h>
#include "../db/DatabaseManager.h"

// Gerçek Zamanlı (Real-Time) Mesajlaşma ve Kanban işlemleri için WebSocket yönlendirme sınıfı
class WsRoutes {
public:
    static void setup(crow::SimpleApp& app, DatabaseManager& db);
};