#pragma once
#include <crow.h>
#include "../db/DatabaseManager.h"

// Kanal mesajları ve Özel Mesaj (DM) işlemleri için yönlendirme sınıfı
class MessageRoutes {
public:
    static void setup(crow::SimpleApp& app, DatabaseManager& db);
};