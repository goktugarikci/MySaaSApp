#include "RoleRoutes.h"

void RoleRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {
    // ==========================================================
    // MİMARİ GÜNCELLEME (V3.0)
    // ==========================================================
    // NOT: Sistem mimarisini RESTful standartlarına uydurmak ve Route Collision 
    // (Çakışma) hatalarını önlemek amacıyla tüm Rol (Role) yönetimi işlemleri 
    // ServerRoutes.cpp dosyasına (/api/servers/<id>/roles) taşınmıştır.
    // 
    // Bu dosya, ileride Global Sistem Rolleri (Örn: Süper Admin, Moderatör) 
    // eklenebileceği öngörülerek hazır tutulmuştur.
}