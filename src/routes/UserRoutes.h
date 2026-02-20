#pragma once
#include <crow.h>
#include "../db/DatabaseManager.h"

// Kullanıcı profili, arkadaşlık işlemleri ve bildirimler için yönlendirme sınıfı
class UserRoutes {
public:
    static void setup(crow::SimpleApp& app, DatabaseManager& db);
};