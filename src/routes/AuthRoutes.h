#pragma once
#include <crow.h>
#include "../db/DatabaseManager.h"
#include <crow/middlewares/cors.h>

// Auth (Kimlik Doğrulama) işlemleri için yönlendirme (Routing) sınıfı
class AuthRoutes {
public:
    // Bu metod, main.cpp içinden çağrılarak tüm /api/auth/* rotalarını Crow'a kaydeder.
    static void setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db);
};