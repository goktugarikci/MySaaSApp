#pragma once
#include <crow.h>
#include "../db/DatabaseManager.h"

#include <crow/middlewares/cors.h>
// Trello/ClickUp benzeri Görev (Kanban) panosu işlemleri için yönlendirme sınıfı
class KanbanRoutes {
public:
    static void setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db);
};