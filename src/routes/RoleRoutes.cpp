#include "RoleRoutes.h"
#include "../utils/Security.h"

void RoleRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // ==========================================================
    // 1. SUNUCUDAKİ TÜM ROLLERİ GETİR
    // ==========================================================
    CROW_ROUTE(app, "/api/servers/<string>/roles").methods("GET"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        // Sunucuya erişim hakkı var mı kontrolü (basit)
        std::string myId = Security::getUserIdFromHeader(req);
        if (!db.isUserInServer(serverId, myId)) return crow::response(403, "Bu sunucuda degilsiniz.");

        auto roles = db.getServerRoles(serverId);
        crow::json::wvalue res;
        for (size_t i = 0; i < roles.size(); ++i) {
            res[i]["id"] = roles[i].id;
            res[i]["name"] = roles[i].name;
            res[i]["color"] = roles[i].color;
            res[i]["hierarchy"] = roles[i].hierarchy;
            res[i]["permissions"] = roles[i].permissions;
        }
        return crow::response(200, res);
            });

    // ==========================================================
// 5. ROL YÖNETİMİ - V2.0
// ==========================================================
    CROW_ROUTE(app, "/api/servers/<string>/roles").methods("POST"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("name")) return crow::response(400);

        std::string roleId = db.createServerRole(serverId, std::string(x["name"].s()), x.has("color") ? std::string(x["color"].s()) : "#000000", x.has("permissions") ? x["permissions"].i() : 0);
        if (!roleId.empty()) {
            db.logAction(Security::getUserIdFromHeader(req), "CREATE_ROLE", roleId, "Yeni sunucu rolu olusturuldu.");
            crow::json::wvalue res; res["role_id"] = roleId;
            return crow::response(201, res);
        }
        return crow::response(500);
            });

    // ==========================================================
    // 3. ROLÜ GÜNCELLE VE SİL
    // ==========================================================
    CROW_ROUTE(app, "/api/roles/<string>").methods("PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string roleId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        std::string serverId = db.getServerIdByRoleId(roleId);
        auto srv = db.getServerDetails(serverId);
        if (!srv || srv->owner_id != myId) return crow::response(403, "Yetkisiz islem.");

        if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("name") || !x.has("hierarchy") || !x.has("permissions")) return crow::response(400);

            if (db.updateRole(roleId, std::string(x["name"].s()), x["hierarchy"].i(), x["permissions"].i())) {
                return crow::response(200, "Rol guncellendi.");
            }
        }
        else {
            if (db.deleteRole(roleId)) return crow::response(200, "Rol silindi.");
        }
        return crow::response(500);
            });



    CROW_ROUTE(app, "/api/servers/<string>/members/<string>/roles").methods("POST"_method)
        ([&db](const crow::request& req, std::string serverId, std::string userId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("role_id")) return crow::response(400, "Yetki Yetersiz.");

        if (db.assignRoleToUser(serverId, userId, std::string(x["role_id"].s()))) {
            db.logAction(Security::getUserIdFromHeader(req), "ASSIGN_ROLE", userId, "Uyeye rol atandi.");
            return crow::response(200, "Rol atandi.");
        }
        return crow::response(500);
            });
}