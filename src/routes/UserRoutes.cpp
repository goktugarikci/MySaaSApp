#include "UserRoutes.h"
#include "../utils/Security.h"
#include <crow/json.h>

void UserRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // =============================================================
    // API: KENDİ PROFİL BİLGİLERİNİ GETİR (GET /api/users/me)
    // =============================================================
    CROW_ROUTE(app, "/api/users/me").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        std::string userId = Security::getUserIdFromHeader(&req);
        auto userOpt = db.getUserById(userId);

        if (userOpt) {
            crow::json::wvalue res;
            res["id"] = userOpt->id;
            res["name"] = userOpt->name;
            res["email"] = userOpt->email;
            res["status"] = userOpt->status;
            res["avatar_url"] = userOpt->avatarUrl;
            return crow::response(200, res);
        }
        return crow::response(404, "Kullanici bulunamadi");
            });

    // =============================================================
    // API: KULLANICI ARAMA (GET /api/users/search?q=aranacak_kelime)
    // =============================================================
    CROW_ROUTE(app, "/api/users/search").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto q = req.url_params.get("q");
        if (!q) return crow::response(400, "Arama terimi (q) gerekli");

        auto users = db.searchUsers(std::string(q));
        crow::json::wvalue res;
        for (size_t i = 0; i < users.size(); ++i) {
            res[i]["id"] = users[i].id;
            res[i]["name"] = users[i].name;
            res[i]["avatar_url"] = users[i].avatarUrl;
        }
        return crow::response(200, res);
            });

    // =============================================================
    // API: ARKADAŞLIK İSTEĞİ GÖNDER (POST /api/users/friends/request)
    // =============================================================
    CROW_ROUTE(app, "/api/users/friends/request").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("target_id")) return crow::response(400, "Hedef kullanici ID gerekli");

        std::string myId = Security::getUserIdFromHeader(&req);
        std::string targetId = body["target_id"].s();

        if (db.sendFriendRequest(myId, targetId)) {
            return crow::response(200, "Arkadaslik istegi gonderildi");
        }
        return crow::response(400, "Istek gonderilemedi veya zaten istek var");
            });

    // =============================================================
    // API: BEKLEYEN ARKADAŞLIK İSTEKLERİNİ GETİR (GET /api/users/friends/pending)
    // =============================================================
    CROW_ROUTE(app, "/api/users/friends/pending").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        std::string myId = Security::getUserIdFromHeader(&req);
        auto requests = db.getPendingRequests(myId);

        crow::json::wvalue res;
        for (size_t i = 0; i < requests.size(); ++i) {
            res[i]["requester_id"] = requests[i].requesterId;
            res[i]["requester_name"] = requests[i].requesterName;
            res[i]["requester_avatar"] = requests[i].requesterAvatar;
            res[i]["created_at"] = requests[i].createdAt;
        }
        return crow::response(200, res);
            });

    // =============================================================
    // API: ARKADAŞLIK İSTEĞİNİ KABUL ET VEYA REDDET (POST /api/users/friends/resolve)
    // =============================================================
    CROW_ROUTE(app, "/api/users/friends/resolve").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("requester_id") || !body.has("accept")) {
            return crow::response(400, "Eksik parametreler (requester_id, accept)");
        }

        std::string myId = Security::getUserIdFromHeader(&req);
        std::string requesterId = body["requester_id"].s();
        bool accept = body["accept"].b();

        if (accept) {
            if (db.acceptFriendRequest(requesterId, myId)) return crow::response(200, "Arkadaslik kabul edildi");
        }
        else {
            if (db.rejectOrRemoveFriend(requesterId, myId)) return crow::response(200, "Arkadaslik reddedildi");
        }
        return crow::response(400, "Islem basarisiz");
            });

    // =============================================================
    // API: ARKADAŞ LİSTESİNİ GETİR (GET /api/users/friends)
    // =============================================================
    CROW_ROUTE(app, "/api/users/friends").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        std::string myId = Security::getUserIdFromHeader(&req);
        auto friends = db.getFriendsList(myId);

        crow::json::wvalue res;
        for (size_t i = 0; i < friends.size(); ++i) {
            res[i]["id"] = friends[i].id;
            res[i]["name"] = friends[i].name;
            res[i]["status"] = friends[i].status;
            res[i]["avatar_url"] = friends[i].avatarUrl;
        }
        return crow::response(200, res);
            });

    // =============================================================
    // API: BİLDİRİMLERİ GETİR (GET /api/users/notifications)
    // =============================================================
    CROW_ROUTE(app, "/api/users/notifications").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        std::string myId = Security::getUserIdFromHeader(&req);
        auto notifs = db.getUserNotifications(myId);

        crow::json::wvalue res;
        for (size_t i = 0; i < notifs.size(); ++i) {
            res[i]["id"] = notifs[i].id;
            res[i]["message"] = notifs[i].message;
            res[i]["type"] = notifs[i].type;
            res[i]["created_at"] = notifs[i].createdAt;
        }
        return crow::response(200, res);
            });

    // =============================================================
    // API: BİLDİRİMİ OKUNDU İŞARETLE (POST /api/users/notifications/<int>/read)
    // =============================================================
    CROW_ROUTE(app, "/api/users/notifications/<int>/read").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req, int notifId) {
        if (!Security::checkAuth(&req, &db)) return crow::response(401, "Yetkisiz Erisim");

        if (db.markNotificationAsRead(notifId)) {
            return crow::response(200, "Bildirim okundu olarak isaretlendi");
        }
        return crow::response(400, "Islem basarisiz");
            });
}