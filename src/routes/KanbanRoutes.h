#pragma once
#include <crow.h>
#include "../db/DatabaseManager.h"

// Trello/ClickUp benzeri Görev (Kanban) panosu işlemleri için yönlendirme sınıfı
class KanbanRoutes {
public:
    static void setup(crow::SimpleApp& app, DatabaseManager& db);
};