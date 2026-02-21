#include "UserRoutes.h"
#include "../utils/Security.h"

void UserRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // ==========================================================
    // 1. KULLANICI PROFİL İŞLEMLERİ
    // ==========================================================

    // Kendi Profilini Getir, Güncelle veya Sil
    CROW_ROUTE(app, "/api/users/me").methods("GET"_method, "PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (req.method == "GET"_method) {
            auto user = db.getUserById(myId);
            return user ? crow::response(200, user->toJson()) : crow::response(404, "Kullanici bulunamadi");
        }
        else if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("name") || !x.has("status")) return crow::response(400, "Eksik parametre");

            if (db.updateUserDetails(myId, std::string(x["name"].s()), std::string(x["status"].s()))) {
                return crow::response(200, "Profil guncellendi.");
            }
            return crow::response(500);
        }
        else {
            if (db.deleteUser(myId)) return crow::response(200, "Hesap silindi.");
            return crow::response(500);
        }
            });

    // Profil Fotoğrafını (Avatar) Güncelle
    CROW_ROUTE(app, "/api/users/me/avatar").methods("PUT"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("avatar_url")) return crow::response(400);

        if (db.updateUserAvatar(Security::getUserIdFromHeader(req), std::string(x["avatar_url"].s()))) {
            return crow::response(200, "Avatar guncellendi.");
        }
        return crow::response(500);
            });

    // Kullanıcı Arama
    CROW_ROUTE(app, "/api/users/search").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        char* qParam = req.url_params.get("q");
        std::string query = qParam ? qParam : "";
        if (query.length() < 3) return crow::response(400, "Arama terimi en az 3 karakter olmalidir.");

        auto users = db.searchUsers(query);
        crow::json::wvalue res;
        for (size_t i = 0; i < users.size(); i++) {
            res[i] = users[i].toJson();
        }
        return crow::response(200, res);
            });


    // ==========================================================
    // 2. KULLANICI DURUMU & PING (AKTİFLİK)
    // ==========================================================

    // Çevrimiçi kalmak için ping atma
    CROW_ROUTE(app, "/api/user/ping").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        if (db.updateLastSeen(Security::getUserIdFromHeader(req))) return crow::response(200);
        return crow::response(500);
            });

    // Durumu değiştirme (Online, Away vb.)
    CROW_ROUTE(app, "/api/user/status").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto body = crow::json::load(req.body);
        if (!body || !body.has("status")) return crow::response(400);

        if (db.updateUserStatus(Security::getUserIdFromHeader(req), std::string(body["status"].s()))) {
            return crow::response(200);
        }
        return crow::response(500);
            });


    // ==========================================================
    // 3. ARKADAŞLIK İŞLEMLERİ
    // ==========================================================

    // Mevcut arkadaşları listele
    CROW_ROUTE(app, "/api/friends")
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        auto friends = db.getFriendsList(Security::getUserIdFromHeader(req));
        crow::json::wvalue res;
        for (size_t i = 0; i < friends.size(); i++) res[i] = friends[i].toJson();
        return crow::response(200, res);
            });

    // Gelen istekleri listele
    CROW_ROUTE(app, "/api/friends/requests")
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        auto reqs = db.getPendingRequests(Security::getUserIdFromHeader(req));
        crow::json::wvalue res;
        for (size_t i = 0; i < reqs.size(); i++) res[i] = reqs[i].toJson();
        return crow::response(200, res);
            });

    // Yeni arkadaşlık isteği gönder
    CROW_ROUTE(app, "/api/friends/request").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_id")) return crow::response(400);

        if (db.sendFriendRequest(Security::getUserIdFromHeader(req), std::string(x["target_id"].s()))) {
            return crow::response(200, "Istek gonderildi.");
        }
        return crow::response(400, "Istek gonderilemedi.");
            });

    // Gelen arkadaşlık isteğini kabul et
    CROW_ROUTE(app, "/api/friends/request/<string>").methods("PUT"_method)
        ([&db](const crow::request& req, std::string requesterId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        if (db.acceptFriendRequest(requesterId, Security::getUserIdFromHeader(req))) {
            return crow::response(200, "Istek kabul edildi.");
        }
        return crow::response(400);
            });

    // Arkadaş sil / İsteği reddet
    CROW_ROUTE(app, "/api/friends/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string otherId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        if (db.rejectOrRemoveFriend(otherId, Security::getUserIdFromHeader(req))) return crow::response(200);
        return crow::response(500);
            });


    // ==========================================================
    // 4. KULLANICI ENGELLEME
    // ==========================================================

    // Engellenenleri listele veya yeni kişi engelle
    CROW_ROUTE(app, "/api/friends/blocks").methods("GET"_method, "POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (req.method == "GET"_method) {
            auto blockedList = db.getBlockedUsers(myId);
            crow::json::wvalue res;
            for (size_t i = 0; i < blockedList.size(); i++) res[i] = blockedList[i].toJson();
            return crow::response(200, res);
        }
        else {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("target_id")) return crow::response(400);

            std::string targetId = std::string(x["target_id"].s());
            db.rejectOrRemoveFriend(targetId, myId); // Engellemeden önce arkadaşlıktan çıkar

            if (db.blockUser(myId, targetId)) return crow::response(201, "Kullanici engellendi.");
            return crow::response(500);
        }
            });

    // Engeli kaldır
    CROW_ROUTE(app, "/api/friends/blocks/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string targetId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        if (db.unblockUser(Security::getUserIdFromHeader(req), targetId)) return crow::response(200, "Engel kaldirildi.");
        return crow::response(500);
            });


    // ==========================================================
    // 5. BİLDİRİMLER VE DAVETLER
    // ==========================================================

    // Gelen sunucu davetlerini görüntüle
    CROW_ROUTE(app, "/api/users/me/server-invites").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        auto invites = db.getPendingServerInvites(Security::getUserIdFromHeader(req));
        crow::json::wvalue res;
        for (size_t i = 0; i < invites.size(); i++) {
            res[i]["server_id"] = invites[i].server_id;
            res[i]["server_name"] = invites[i].server_name;
            res[i]["inviter_name"] = invites[i].inviter_name;
            res[i]["created_at"] = invites[i].created_at;
        }
        return crow::response(200, res);
            });

    // Sistem (Süre vb.) Bildirimlerini Görüntüle
    CROW_ROUTE(app, "/api/notifications").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        auto notifs = db.getUserNotifications(Security::getUserIdFromHeader(req));
        crow::json::wvalue res;
        for (size_t i = 0; i < notifs.size(); i++) {
            res[i]["id"] = notifs[i].id;
            res[i]["message"] = notifs[i].message;
            res[i]["type"] = notifs[i].type;
            res[i]["created_at"] = notifs[i].created_at;
        }
        return crow::response(200, res);
            });

    // Bildirimi Okundu Olarak İşaretle
    CROW_ROUTE(app, "/api/notifications/<int>/read").methods("PUT"_method)
        ([&db](const crow::request& req, int notifId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        if (db.markNotificationAsRead(notifId)) return crow::response(200);
        return crow::response(500);
            });
}