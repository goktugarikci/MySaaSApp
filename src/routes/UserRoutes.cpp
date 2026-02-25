#include "UserRoutes.h"
#include "../utils/Security.h"

void UserRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // ==========================================================
    // 1. KULLANICI PROFİL İŞLEMLERİ
    // ==========================================================
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

    CROW_ROUTE(app, "/api/users/me/avatar").methods("PUT"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("avatar_url")) return crow::response(400);
        if (db.updateUserAvatar(Security::getUserIdFromHeader(req), std::string(x["avatar_url"].s()))) return crow::response(200, "Avatar guncellendi.");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/users/search").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        char* qParam = req.url_params.get("q");
        if (!qParam) return crow::response(400, "Arama terimi (q) eksik.");

        std::string query(qParam);
        if (query.length() < 3) return crow::response(400, "Arama terimi en az 3 karakter olmalidir.");

        auto users = db.searchUsers(query);
        crow::json::wvalue res;
        for (size_t i = 0; i < users.size(); i++) { res[i] = users[i].toJson(); }
        return crow::response(200, res);
            });

    // ==========================================================
    // 2. KULLANICI DURUMU & PING (AKTİFLİK)
    // ==========================================================
    CROW_ROUTE(app, "/api/user/ping").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        if (db.updateLastSeen(Security::getUserIdFromHeader(req))) return crow::response(200);
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/user/status").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto body = crow::json::load(req.body);
        if (!body || !body.has("status")) return crow::response(400);
        if (db.updateUserStatus(Security::getUserIdFromHeader(req), std::string(body["status"].s()))) return crow::response(200);
        return crow::response(500);
            });

    // ==========================================================
    // 3. ARKADAŞLIK İŞLEMLERİ (OPTİMİZE EDİLDİ VE ÇAKIŞMA TEMİZLENDİ)
    // ==========================================================
    CROW_ROUTE(app, "/api/friends").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto friends = db.getFriendsList(Security::getUserIdFromHeader(req));
        crow::json::wvalue res;
        for (size_t i = 0; i < friends.size(); i++) res[i] = friends[i].toJson();
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/friends/requests").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto reqs = db.getPendingRequests(Security::getUserIdFromHeader(req));
        crow::json::wvalue res;
        for (size_t i = 0; i < reqs.size(); i++) res[i] = reqs[i].toJson();
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/friends/request").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_id")) return crow::response(400);

        std::string myId = Security::getUserIdFromHeader(req);
        std::string targetId = std::string(x["target_id"].s());

        if (myId == targetId) return crow::response(400, "Kendinize arkadaslik istegi gonderemezsiniz.");

        if (db.sendFriendRequest(myId, targetId)) return crow::response(200, "Istek gonderildi.");
        return crow::response(400, "Istek gonderilemedi.");
            });

    // ARKADAŞLIK İSTEĞİNİ YANITLA (KABUL/RED) -> Tekil fonksiyon kullanıldı
    CROW_ROUTE(app, "/api/friends/requests/<string>").methods("PUT"_method)
        ([&db](const crow::request& req, std::string requesterId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("status")) return crow::response(400); // status: "accepted" veya "rejected"

        if (db.respondFriendRequest(requesterId, Security::getUserIdFromHeader(req), std::string(x["status"].s()))) {
            return crow::response(200, "Istek yanitlandi.");
        }
        return crow::response(500);
            });

    // ARKADAŞLIKTAN ÇIKAR
    CROW_ROUTE(app, "/api/friends/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string friendId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        if (db.removeFriend(Security::getUserIdFromHeader(req), friendId)) return crow::response(200, "Arkadasliktan cikarildi.");
        return crow::response(500);
            });

    // ==========================================================
    // 4. KULLANICI ENGELLEME
    // ==========================================================
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
            db.rejectOrRemoveFriend(targetId, myId); // Arkadaşlıktan çıkar ve engelle

            if (db.blockUser(myId, targetId)) return crow::response(201, "Kullanici engellendi.");
            return crow::response(500);
        }
            });

    CROW_ROUTE(app, "/api/friends/blocks/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string targetId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        if (db.unblockUser(Security::getUserIdFromHeader(req), targetId)) return crow::response(200, "Engel kaldirildi.");
        return crow::response(500);
            });

    // ==========================================================
    // 5. BİLDİRİMLER VE DAVETLER
    // ==========================================================
    CROW_ROUTE(app, "/api/users/me/server-invites").methods("GET"_method)
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

    CROW_ROUTE(app, "/api/notifications/<int>/read").methods("PUT"_method)
        ([&db](const crow::request& req, int notifId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        if (db.markNotificationAsRead(notifId)) return crow::response(200);
        return crow::response(500);
            });

    // ==========================================================
    // 6. ÖZEL MESAJLAŞMA (DM) İŞLEMLERİ
    // ==========================================================
    CROW_ROUTE(app, "/api/users/dm").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_id")) return crow::response(400, "Hedef kullanici ID eksik.");

        std::string myId = Security::getUserIdFromHeader(req);
        std::string targetId = std::string(x["target_id"].s());

        if (myId == targetId) return crow::response(400, "Kendinizle DM baslatamazsiniz.");

        std::string channelId = db.getOrCreateDMChannel(myId, targetId);
        if (!channelId.empty()) {
            crow::json::wvalue res;
            res["channel_id"] = channelId;
            return crow::response(200, res);
        }
        return crow::response(500, "DM kanali olusturulamadi.");
            });

    // DM Geçmişini Silme (Kanalı Kapatma)
    CROW_ROUTE(app, "/api/users/dm/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (db.deleteChannel(channelId)) {
            // BİREYSEL SOHBET SİLME LOGU
            db.logAction(myId, "DELETE_DM_HISTORY", channelId, "Kullanici ozel mesaj (DM) gecmisini sildi.");
            return crow::response(200, "DM gecmisi temizlendi.");
        }
        return crow::response(500);
            });
    // ==========================================================
    // V3.0 - AŞAMA 2: KULLANICI NOTLARI VE KAYDEDİLENLER
    // ==========================================================

    // KULLANICIYA ÖZEL NOT EKLE/GETİR
    CROW_ROUTE(app, "/api/users/<string>/notes").methods("GET"_method, "POST"_method)
        ([&db](const crow::request& req, std::string targetId) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (req.method == "GET"_method) {
            std::string note = db.getUserNote(myId, targetId);
            crow::json::wvalue res; res["note"] = note;
            return crow::response(200, res);
        }
        else {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("note")) return crow::response(400);

            if (db.addUserNote(myId, targetId, std::string(x["note"].s()))) {
                return crow::response(200, "Not basariyla kaydedildi.");
            }
            return crow::response(500);
        }
            });

    // KAYDEDİLEN MESAJLARI (FAVORİLER) GETİR
    CROW_ROUTE(app, "/api/users/me/saved-messages").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        auto msgs = db.getSavedMessages(myId);
        crow::json::wvalue res;
        for (size_t i = 0; i < msgs.size(); ++i) res[i] = msgs[i].toJson();
        return crow::response(200, res);
            });
    // ABONELİK İPTALİ (Free Seviyesine Düşürme)
    CROW_ROUTE(app, "/api/users/me/subscription").methods("DELETE"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        std::string myId = Security::getUserIdFromHeader(req);

        if (db.cancelSubscription(myId)) {
            db.logAction(myId, "CANCEL_SUBSCRIPTION", myId, "Kullanici aktif aboneligini iptal etti.");
            return crow::response(200, "Abonelik basariyla iptal edildi. Profiliniz ucretsiz (Free) seviyesine dusuruldu.");
        }
        return crow::response(500);
            });

}