#pragma once
#include <crow.h>
#include "../db/DatabaseManager.h"

#include <crow/middlewares/cors.h>
// Kanal mesajları ve Özel Mesaj (DM) işlemleri için yönlendirme sınıfı
class MessageRoutes {
public:
    static void setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db);
};