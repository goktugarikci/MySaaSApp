#include "crow.h"
#include "db/DatabaseManager.h"
#include "utils/Security.h"
#include "utils/FileManager.h"
#include <cpr/cpr.h>
#include <unordered_map>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>

// --- YAPILANDIRMA ---
const std::string GOOGLE_CLIENT_ID = "BURAYA_GOOGLE_CLIENT_ID_YAZIN";
const std::string GOOGLE_CLIENT_SECRET = "BURAYA_GOOGLE_CLIENT_SECRET_YAZIN";
const std::string GOOGLE_REDIRECT_URI = "http://localhost:8080/api/auth/google/callback";

// --- GÖRÜNTÜLÜ SOHBET (WebRTC) HAVUZU ---
std::unordered_map<std::string, crow::websocket::connection*> active_video_calls;
std::mutex video_call_mtx;

// --- GÜVENLİK KATMANI (Middleware) ---
bool checkAuth(const crow::request& req, DatabaseManager& db, bool requireAdmin = false) {
    auto authHeader = req.get_header_value("Authorization");
    if (authHeader.empty() || authHeader.find("mock-jwt-token-") != 0) return false;

    std::string userId = authHeader.substr(15);
    if (userId.empty()) return false;

    if (userId == "aB3dE7xY9Z1kL0m") return true; // Süper Admin Bypass

    if (requireAdmin) return db.isSystemAdmin(userId);
    return true;
}

std::string getUserIdFromHeader(const crow::request& req) {
    auto authHeader = req.get_header_value("Authorization");
    if (authHeader.empty() || authHeader.find("mock-jwt-token-") != 0) return "";
    return authHeader.substr(15);
}

int main() {
    FileManager::initDirectories();
    DatabaseManager db("mysaasapp.db");
    if (!db.open()) return -1;
    db.initTables();

    crow::SimpleApp app;

    // =============================================================
    // 1. KİMLİK DOĞRULAMA (Auth)
    // =============================================================

    CROW_ROUTE(app, "/api/auth/register").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Gecersiz JSON");
        if (!x.has("name") || !x.has("email") || !x.has("password")) return crow::response(400, "Eksik parametre");

        std::string email = std::string(x["email"].s());
        if (db.createUser(std::string(x["name"].s()), email, std::string(x["password"].s()))) {
            auto u = db.getUser(email);
            crow::json::wvalue res;
            res["message"] = "Kayit basarili";
            if (u) res["user_id"] = u->id;
            return crow::response(201, res);
        }
        return crow::response(400, "Kayit basarisiz");
            });

    CROW_ROUTE(app, "/api/auth/login").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "Gecersiz JSON");
        if (!body.has("email") || !body.has("password")) return crow::response(400, "Eksik bilgi");

        std::string userId = db.authenticateUser(std::string(body["email"].s()), std::string(body["password"].s()));
        if (!userId.empty()) {
            db.updateLastSeen(userId);
            crow::json::wvalue res;
            res["token"] = "mock-jwt-token-" + userId;
            res["user_id"] = userId;
            res["message"] = "Giris basarili. Durum: Online";
            return crow::response(200, res);
        }
        return crow::response(401, "Gecersiz e-posta veya sifre");
            });

    CROW_ROUTE(app, "/api/auth/logout").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        std::string userId = getUserIdFromHeader(req);
        if (userId.empty()) return crow::response(401);
        if (db.updateUserStatus(userId, "Offline")) return crow::response(200, "Cikis yapildi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/auth/google/url")
        ([]() {
        std::string url = "https://accounts.google.com/o/oauth2/v2/auth?client_id=" + GOOGLE_CLIENT_ID +
            "&redirect_uri=" + GOOGLE_REDIRECT_URI + "&response_type=code&scope=email%20profile";
        crow::json::wvalue res; res["url"] = url;
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/auth/google/callback").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("code")) return crow::response(400);

        cpr::Response r = cpr::Post(cpr::Url{ "https://oauth2.googleapis.com/token" },
            cpr::Payload{ {"client_id", GOOGLE_CLIENT_ID}, {"client_secret", GOOGLE_CLIENT_SECRET},
                         {"code", std::string(x["code"].s())}, {"grant_type", "authorization_code"},
                         {"redirect_uri", GOOGLE_REDIRECT_URI} });

        auto tokenJson = crow::json::load(r.text);
        if (!tokenJson || !tokenJson.has("access_token")) return crow::response(400, "Google token hatasi");

        cpr::Response userRes = cpr::Get(cpr::Url{ "https://www.googleapis.com/oauth2/v1/userinfo" }, cpr::Bearer{ std::string(tokenJson["access_token"].s()) });
        auto userInfo = crow::json::load(userRes.text);
        if (!userInfo) return crow::response(400);

        std::string googleId = std::string(userInfo["id"].s());
        auto existingUser = db.getUserByGoogleId(googleId);
        std::string userId = "";

        if (existingUser) {
            userId = existingUser->id;
        }
        else {
            if (db.createGoogleUser(std::string(userInfo["name"].s()), std::string(userInfo["email"].s()), googleId, std::string(userInfo["picture"].s()))) {
                auto newUser = db.getUserByGoogleId(googleId);
                if (newUser) userId = newUser->id;
            }
            else return crow::response(500);
        }

        db.updateLastSeen(userId);
        crow::json::wvalue res; res["user_id"] = userId; res["token"] = "mock-jwt-token-" + userId;
        return crow::response(200, res);
            });

    // ==========================================================
    // KALP ATIŞI VE DURUM GÜNCELLEME
    // ==========================================================
    CROW_ROUTE(app, "/api/user/ping").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        if (db.updateLastSeen(getUserIdFromHeader(req))) return crow::response(200);
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/user/status").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto body = crow::json::load(req.body);
        if (!body || !body.has("status")) return crow::response(400);
        if (db.updateUserStatus(getUserIdFromHeader(req), body["status"].s())) return crow::response(200);
        return crow::response(500);
            });

    // =============================================================
    // 2. KULLANICI PROFİLİ VE ARAMA
    // =============================================================

    CROW_ROUTE(app, "/api/users/me").methods("GET"_method, "PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        std::string myId = getUserIdFromHeader(req);

        if (req.method == "GET"_method) {
            auto user = db.getUserById(myId);
            return user ? crow::response(200, user->toJson()) : crow::response(404);
        }
        else if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("name") || !x.has("status")) return crow::response(400);
            if (db.updateUserDetails(myId, std::string(x["name"].s()), std::string(x["status"].s()))) return crow::response(200);
            return crow::response(500);
        }
        else {
            if (db.deleteUser(myId)) return crow::response(200);
            return crow::response(500);
        }
            });

    CROW_ROUTE(app, "/api/users/me/avatar").methods("PUT"_method)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("avatar_url")) return crow::response(400);
        if (db.updateUserAvatar(getUserIdFromHeader(req), std::string(x["avatar_url"].s()))) return crow::response(200);
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/users/search").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        char* qParam = req.url_params.get("q");
        std::string query = qParam ? qParam : "";
        if (query.length() < 3) return crow::response(400, "{\"error\": \"Arama terimi en az 3 karakter olmalidir.\"}");
        auto users = db.searchUsers(query);
        crow::json::wvalue res;
        for (size_t i = 0; i < users.size(); i++) res[i] = users[i].toJson();
        return crow::response(200, res);
            });

    // =============================================================
    // 3. DOSYA YÜKLEME VE SUNUMU
    // =============================================================

    CROW_ROUTE(app, "/api/upload").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        crow::multipart::message msg(req);
        std::string original_filename, file_content, upload_type;
        bool has_file = false, has_type = false;

        for (const auto& part : msg.parts) {
            auto header = part.get_header_object("Content-Disposition");
            if (header.params.count("name")) {
                if (header.params.at("name") == "file") {
                    if (header.params.count("filename")) original_filename = header.params.at("filename");
                    file_content = part.body; has_file = true;
                }
                else if (header.params.at("name") == "type") {
                    upload_type = part.body; has_type = true;
                }
            }
        }
        if (!has_file || !has_type) return crow::response(400);
        try {
            auto fType = (upload_type == "avatar") ? FileManager::FileType::AVATAR : FileManager::FileType::ATTACHMENT;
            std::string url = FileManager::saveFile(file_content, original_filename, fType);
            crow::json::wvalue res; res["url"] = url;
            return crow::response(200, res);
        }
        catch (const std::exception& e) { return crow::response(500, e.what()); }
            });

    CROW_ROUTE(app, "/uploads/<path>")
        ([](const crow::request& req, crow::response& res, std::string path) {
        std::string content = FileManager::readFile("/uploads/" + path);
        if (content.empty()) { res.code = 404; }
        else {
            res.code = 200;
            if (path.find(".png") != std::string::npos) res.set_header("Content-Type", "image/png");
            else if (path.find(".jpg") != std::string::npos) res.set_header("Content-Type", "image/jpeg");
            res.write(content);
        }
        res.end();
            });

    // =============================================================
    // 4. ARKADAŞLIK VE DAVET SİSTEMİ
    // =============================================================

    CROW_ROUTE(app, "/api/friends")
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto friends = db.getFriendsList(getUserIdFromHeader(req));
        crow::json::wvalue res; for (size_t i = 0; i < friends.size(); i++) res[i] = friends[i].toJson();
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/friends/requests")
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto reqs = db.getPendingRequests(getUserIdFromHeader(req));
        crow::json::wvalue res; for (size_t i = 0; i < reqs.size(); i++) res[i] = reqs[i].toJson();
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/friends/request").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_id")) return crow::response(400);
        if (db.sendFriendRequest(getUserIdFromHeader(req), std::string(x["target_id"].s()))) return crow::response(200);
        return crow::response(400);
            });

    CROW_ROUTE(app, "/api/friends/request/<string>").methods("PUT"_method)
        ([&db](const crow::request& req, std::string requesterId) {
        if (!checkAuth(req, db)) return crow::response(401);
        if (db.acceptFriendRequest(requesterId, getUserIdFromHeader(req))) return crow::response(200);
        return crow::response(400);
            });

    CROW_ROUTE(app, "/api/friends/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string otherId) {
        if (!checkAuth(req, db)) return crow::response(401);
        if (db.rejectOrRemoveFriend(otherId, getUserIdFromHeader(req))) return crow::response(200);
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/users/me/server-invites").methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto invites = db.getPendingServerInvites(getUserIdFromHeader(req));
        crow::json::wvalue res;
        for (size_t i = 0; i < invites.size(); i++) {
            res[i]["server_id"] = invites[i].server_id;
            res[i]["server_name"] = invites[i].server_name;
            res[i]["inviter_name"] = invites[i].inviter_name;
            res[i]["created_at"] = invites[i].created_at;
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/servers/<string>/invite").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req, std::string serverId) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto body = crow::json::load(req.body);
        if (!body || !body.has("target_user_id")) return crow::response(400, "{\"error\":\"target_user_id gerekli\"}");

        if (db.sendServerInvite(serverId, getUserIdFromHeader(req), std::string(body["target_user_id"].s()))) {
            return crow::response(200, "{\"message\":\"Davet basariyla gonderildi.\"}");
        }
        return crow::response(500, "{\"error\":\"Davet gonderilemedi.\"}");
            });

    CROW_ROUTE(app, "/api/servers/<string>/invites/resolve").methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req, std::string serverId) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto body = crow::json::load(req.body);
        if (!body || !body.has("accept")) return crow::response(400);

        if (db.resolveServerInvite(serverId, getUserIdFromHeader(req), body["accept"].b())) {
            return crow::response(200, "{\"message\":\"Islem basarili.\"}");
        }
        return crow::response(500);
            });

    // =============================================================
    // 5. SUNUCU, ROL VE KANAL YÖNETİMİ
    // =============================================================

    CROW_ROUTE(app, "/api/servers").methods("GET"_method, "POST"_method)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401, "{\"error\": \"Yetkisiz islem\"}");
        std::string myId = getUserIdFromHeader(req);

        if (req.method == "GET"_method) {
            auto servers = db.getUserServers(myId);
            crow::json::wvalue res; for (size_t i = 0; i < servers.size(); i++) res[i] = servers[i].toJson();
            return crow::response(200, res);
        }
        else {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("name")) return crow::response(400, "{\"error\": \"Sunucu adi gerekli!\"}");
            std::string sid = db.createServer(std::string(x["name"].s()), myId);
            if (!sid.empty()) {
                crow::json::wvalue res; res["server_id"] = sid;
                return crow::response(201, res);
            }
            return crow::response(403, "{\"error\": \"Limit dolmus olabilir.\"}");
        }
            });

    CROW_ROUTE(app, "/api/servers/<string>").methods("GET"_method, "PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string id) {
        if (!checkAuth(req, db)) return crow::response(401);
        std::string myId = getUserIdFromHeader(req);

        if (req.method == "GET"_method) {
            auto s = db.getServerDetails(id);
            return s ? crow::response(200, s->toJson()) : crow::response(404);
        }
        else if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("name")) return crow::response(400);

            // GÜVENLİK DUVARI: Sadece Kurucu
            auto srv = db.getServerDetails(id);
            if (!srv || srv->owner_id != myId) return crow::response(403, "Sadece kurucu guncelleyebilir.");

            std::string icon = x.has("icon_url") ? std::string(x["icon_url"].s()) : "";
            if (db.updateServer(id, std::string(x["name"].s()), icon)) return crow::response(200);
            return crow::response(500);
        }
        else {
            // GÜVENLİK DUVARI: Sadece Kurucu Silebilir
            auto srv = db.getServerDetails(id);
            if (!srv || srv->owner_id != myId) return crow::response(403, "Sadece kurucu silebilir.");

            if (db.deleteServer(id)) return crow::response(200);
            return crow::response(500);
        }
            });

    CROW_ROUTE(app, "/api/servers/join").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("invite_code")) return crow::response(400);
        if (db.joinServerByCode(getUserIdFromHeader(req), std::string(x["invite_code"].s()))) return crow::response(200);
        return crow::response(400);
            });

    CROW_ROUTE(app, "/api/servers/<string>/members/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string serverId, std::string targetUserId) {
        if (!checkAuth(req, db)) return crow::response(401);
        std::string myId = getUserIdFromHeader(req);

        // GÜVENLİK DUVARI: Sadece Kurucu Atabilir
        auto srv = db.getServerDetails(serverId);
        if (!srv || srv->owner_id != myId) return crow::response(403, "Sadece kurucu uye atabilir.");

        if (db.kickMember(serverId, targetUserId)) return crow::response(200, "Uye atildi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/servers/<string>/roles").methods("GET"_method, "POST"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!checkAuth(req, db)) return crow::response(401);
        std::string myId = getUserIdFromHeader(req);

        if (req.method == "GET"_method) {
            auto roles = db.getServerRoles(serverId);
            crow::json::wvalue res; for (size_t i = 0; i < roles.size(); i++) res[i] = roles[i].toJson();
            return crow::response(200, res);
        }
        else {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("name") || !x.has("hierarchy") || !x.has("permissions")) return crow::response(400);

            // GÜVENLİK DUVARI: Sadece Kurucu Rol Ekleyebilir
            auto srv = db.getServerDetails(serverId);
            if (!srv || srv->owner_id != myId) return crow::response(403, "Sadece kurucu rol ekleyebilir.");

            if (db.createRole(serverId, std::string(x["name"].s()), x["hierarchy"].i(), x["permissions"].i())) return crow::response(201);
            return crow::response(500);
        }
            });

    CROW_ROUTE(app, "/api/servers/<string>/channels").methods("GET"_method, "POST"_method)
        ([&db](const crow::request& req, std::string srvId) {
        if (!checkAuth(req, db)) return crow::response(401);
        std::string myId = getUserIdFromHeader(req);

        if (req.method == "GET"_method) {
            auto channels = db.getServerChannels(srvId);
            crow::json::wvalue res; for (size_t i = 0; i < channels.size(); i++) res[i] = channels[i].toJson();
            return crow::response(200, res);
        }
        else {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("name") || !x.has("type")) return crow::response(400);

            // GÜVENLİK DUVARI: Sadece Kurucu Kanal Açabilir
            auto srv = db.getServerDetails(srvId);
            if (!srv || srv->owner_id != myId) return crow::response(403, "Sadece kurucu kanal acabilir.");

            if (db.createChannel(srvId, std::string(x["name"].s()), x["type"].i())) return crow::response(201);
            return crow::response(403); // Muhtemelen Kanban limiti doldu
        }
            });

    CROW_ROUTE(app, "/api/channels/<string>").methods("PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string channelId) {
        if (!checkAuth(req, db)) return crow::response(401);
        std::string myId = getUserIdFromHeader(req);

        std::string serverId = db.getChannelServerId(channelId);
        if (serverId.empty()) return crow::response(404);

        // GÜVENLİK DUVARI: Sadece Kurucu Kanalı Düzenleyip / Silebilir
        auto srv = db.getServerDetails(serverId);
        if (!srv || srv->owner_id != myId) return crow::response(403, "Sadece kurucu islem yapabilir.");

        if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("name")) return crow::response(400);

            std::string newName = std::string(x["name"].s());
            if (db.updateChannel(channelId, newName)) {
                db.logServerAction(serverId, "KANAL_GUNCELLENDI", "Kanal adi degisti: " + newName);
                return crow::response(200);
            }
        }
        else {
            std::string oldName = db.getChannelName(channelId);
            if (db.deleteChannel(channelId)) {
                db.logServerAction(serverId, "KANAL_SILINDI", "Kanal silindi: " + oldName);
                return crow::response(200);
            }
        }
        return crow::response(500);
            });

    // =============================================================
    // 6. MESAJLAŞMA & BİREBİR SOHBET (DM)
    // =============================================================

    CROW_ROUTE(app, "/api/channels/<string>/messages").methods("GET"_method, "POST"_method)
        ([&db](const crow::request& req, std::string chId) {
        if (!checkAuth(req, db)) return crow::response(401);
        if (req.method == "GET"_method) {
            auto msgs = db.getChannelMessages(chId, 50);
            crow::json::wvalue res; for (size_t i = 0; i < msgs.size(); i++) res[i] = msgs[i].toJson();
            return crow::response(200, res);
        }
        else {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("content")) return crow::response(400);
            std::string att = x.has("attachment_url") ? std::string(x["attachment_url"].s()) : "";
            if (db.sendMessage(chId, getUserIdFromHeader(req), std::string(x["content"].s()), att)) return crow::response(201);
            return crow::response(500);
        }
            });

    CROW_ROUTE(app, "/api/messages/<string>").methods("PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string msgId) {
        if (!checkAuth(req, db)) return crow::response(401);
        if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("content")) return crow::response(400);
            if (db.updateMessage(msgId, std::string(x["content"].s()))) return crow::response(200);
        }
        else {
            if (db.deleteMessage(msgId)) return crow::response(200);
        }
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/dm/<string>").methods("POST"_method)
        ([&db](const crow::request& req, std::string targetUserId) {
        if (!checkAuth(req, db)) return crow::response(401);
        std::string channelId = db.getOrCreateDMChannel(getUserIdFromHeader(req), targetUserId);
        if (!channelId.empty()) {
            crow::json::wvalue res; res["channel_id"] = channelId;
            return crow::response(200, res);
        }
        return crow::response(500);
            });

    // =============================================================
    // 7. KANBAN / TRELLO PANO YÖNETİMİ
    // =============================================================

    CROW_ROUTE(app, "/api/boards/<string>")
        ([&db](const crow::request& req, std::string chId) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto board = db.getKanbanBoard(chId);
        crow::json::wvalue res; for (size_t i = 0; i < board.size(); i++) res[i] = board[i].toJson();
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/boards/<string>/lists").methods("POST"_method)
        ([&db](const crow::request& req, std::string chId) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("title")) return crow::response(400);
        if (db.createKanbanList(chId, std::string(x["title"].s()))) return crow::response(201);
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/lists/<string>").methods("PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string listId) {
        if (!checkAuth(req, db)) return crow::response(401);
        if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("title") || !x.has("position")) return crow::response(400);
            if (db.updateKanbanList(listId, std::string(x["title"].s()), x["position"].i())) return crow::response(200);
        }
        else {
            if (db.deleteKanbanList(listId)) return crow::response(200);
        }
        return crow::response(500);
            });
    

    CROW_ROUTE(app, "/api/cards/<string>").methods("PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!checkAuth(req, db)) return crow::response(401);
        if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("title") || !x.has("description") || !x.has("priority")) return crow::response(400);
            if (db.updateKanbanCard(cardId, std::string(x["title"].s()), std::string(x["description"].s()), x["priority"].i())) return crow::response(200);
        }
        else {
            if (db.deleteKanbanCard(cardId)) return crow::response(200);
        }
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/cards/<string>/move").methods("PUT"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("new_list_id") || !x.has("new_position")) return crow::response(400);
        if (db.moveCard(cardId, std::string(x["new_list_id"].s()), x["new_position"].i())) return crow::response(200);
        return crow::response(500);
            });

    // =============================================================
    // 8. GÖRÜNTÜLÜ KONUŞMA (WebRTC Signaling)
    // =============================================================

    CROW_WEBSOCKET_ROUTE(app, "/ws/video-call")
        .onopen([&](crow::websocket::connection& conn) { /* Baglanti acildi */ })
        .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
        auto msg = crow::json::load(data);
        if (!msg || !msg.has("type")) return;
        std::string type = msg["type"].s();

        if (type == "register" && msg.has("user_id")) {
            std::lock_guard<std::mutex> lock(video_call_mtx);
            active_video_calls[std::string(msg["user_id"].s())] = &conn;
        }
        else if ((type == "offer" || type == "answer" || type == "ice_candidate") && msg.has("target_id")) {
            std::lock_guard<std::mutex> lock(video_call_mtx);
            auto it = active_video_calls.find(std::string(msg["target_id"].s()));
            if (it != active_video_calls.end() && it->second != nullptr) it->second->send_text(data);
        }
            })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t code) {
        std::lock_guard<std::mutex> lock(video_call_mtx);
        for (auto it = active_video_calls.begin(); it != active_video_calls.end(); ) {
            if (it->second == &conn) it = active_video_calls.erase(it);
            else ++it;
        }
            });

    // =============================================================
    // 9. ÖDEME SİSTEMİ & MOBİL POS (mPOS)
    // =============================================================

    CROW_ROUTE(app, "/api/payments/create-checkout").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("amount")) return crow::response(400);
        std::string pid = "pay_web_" + std::to_string(std::time(nullptr));
        db.createPaymentRecord(getUserIdFromHeader(req), pid, (float)x["amount"].d(), "USD");
        crow::json::wvalue res; res["checkout_url"] = "https://checkout.stripe.com/pay/" + pid;
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/payments/mpos-charge").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("amount") || !x.has("source_token")) return crow::response(400);
        std::string pid = "pay_mpos_" + std::string(x["source_token"].s());
        db.createPaymentRecord(getUserIdFromHeader(req), pid, (float)x["amount"].d(), "TRY");
        db.updatePaymentStatus(pid, "success");
        crow::json::wvalue res; res["status"] = "success"; res["transaction_id"] = pid;
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/payments/history")
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto payments = db.getUserPayments(getUserIdFromHeader(req));
        crow::json::wvalue res;
        for (size_t i = 0; i < payments.size(); i++) {
            res[i]["id"] = payments[i].provider_payment_id;
            res[i]["amount"] = payments[i].amount;
            res[i]["status"] = payments[i].status;
        }
        return crow::response(200, res);
            });

    // =============================================================
    // 10. YÖNETİM (ADMIN) & RAPORLAMA
    // =============================================================

    CROW_ROUTE(app, "/api/admin/users")
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db, true)) return crow::response(403);
        auto users = db.getAllUsers();
        crow::json::wvalue res;
        for (size_t i = 0; i < users.size(); i++) res[i] = users[i].toJson();
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/admin/servers")
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db, true)) return crow::response(403);
        auto servers = db.getAllServers();
        crow::json::wvalue res;
        for (size_t i = 0; i < servers.size(); i++) res[i] = servers[i].toJson();
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/admin/stats")
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db, true)) return crow::response(403);
        auto stats = db.getSystemStats();
        crow::json::wvalue res;
        res["users"] = stats.user_count; res["servers"] = stats.server_count; res["messages"] = stats.message_count;
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/admin/users/<string>/ban").methods("POST"_method)
        ([&db](const crow::request& req, std::string userId) {
        if (!checkAuth(req, db, true)) return crow::response(403);
        if (db.banUser(userId)) return crow::response(200, "Kullanici yasaklandi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/admin/reports")
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db, true)) return crow::response(403);
        auto reports = db.getOpenReports();
        crow::json::wvalue res;
        for (size_t i = 0; i < reports.size(); i++) {
            res[i]["id"] = reports[i].id;
            res[i]["reporter_id"] = reports[i].reporter_id;
            res[i]["reason"] = reports[i].reason;
            res[i]["type"] = reports[i].type;
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/reports").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("content_id") || !x.has("type") || !x.has("reason")) return crow::response(400);
        if (db.createReport(getUserIdFromHeader(req), std::string(x["content_id"].s()), std::string(x["type"].s()), std::string(x["reason"].s())))
            return crow::response(201);
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/admin/users/<string>/subscription").methods("POST"_method)
        ([&db](const crow::request& req, std::string userId) {
        if (!checkAuth(req, db, true)) return crow::response(403);
        auto body = crow::json::load(req.body);
        if (!body || !body.has("level") || !body.has("days")) return crow::response(400, "Eksik parametre");
        if (db.updateUserSubscription(userId, body["level"].i(), body["days"].i())) {
            return crow::response(200, "Kullanici yetkisi yukseltildi.");
        }
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/admin/users/<string>/details")
        ([&db](const crow::request& req, std::string targetId) {
        if (!checkAuth(req, db, true)) return crow::response(403);
        crow::json::wvalue res;

        auto servers = db.getUserServers(targetId);
        for (size_t i = 0; i < servers.size(); i++) {
            res["servers"][i]["id"] = servers[i].id;
            res["servers"][i]["name"] = servers[i].name;
            res["servers"][i]["owner_id"] = servers[i].owner_id;
        }

        auto friends = db.getFriendsList(targetId);
        for (size_t i = 0; i < friends.size(); i++) {
            res["friends"][i]["id"] = friends[i].id;
            res["friends"][i]["name"] = friends[i].name;
            res["friends"][i]["email"] = friends[i].email;
        }

        auto payments = db.getUserPayments(targetId);
        for (size_t i = 0; i < payments.size(); i++) {
            res["payments"][i]["id"] = payments[i].provider_payment_id;
            res["payments"][i]["amount"] = payments[i].amount;
            res["payments"][i]["status"] = payments[i].status;
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/admin/servers/<string>/details")
        ([&db](const crow::request& req, std::string serverId) {
        if (!checkAuth(req, db, true)) return crow::response(403);
        crow::json::wvalue res;

        auto members = db.getServerMembersDetails(serverId);
        for (size_t i = 0; i < members.size(); i++) {
            res["members"][i]["id"] = members[i].id;
            res["members"][i]["name"] = members[i].name;
            res["members"][i]["status"] = members[i].status;
        }

        auto logs = db.getServerLogs(serverId);
        for (size_t i = 0; i < logs.size(); i++) {
            res["logs"][i]["time"] = logs[i].timestamp;
            res["logs"][i]["action"] = logs[i].action;
            res["logs"][i]["details"] = logs[i].details;
        }
        return crow::response(200, res);
            });

    // --- ARKADAŞ ENGELLEME CRUD ---
    CROW_ROUTE(app, "/api/friends/blocks").methods("GET"_method, "POST"_method)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        std::string myId = getUserIdFromHeader(req);

        if (req.method == "GET"_method) {
            // Engellenen kişileri listele
            auto blockedList = db.getBlockedUsers(myId);
            crow::json::wvalue res;
            for (size_t i = 0; i < blockedList.size(); i++) res[i] = blockedList[i].toJson();
            return crow::response(200, res);
        }
        else {
            // Birini engelle
            auto x = crow::json::load(req.body);
            if (!x || !x.has("target_id")) return crow::response(400);
            std::string targetId = std::string(x["target_id"].s());

            // Eğer arkadaşsa, önce arkadaşlıktan çıkar, sonra engelle
            db.rejectOrRemoveFriend(targetId, myId);
            if (db.blockUser(myId, targetId)) return crow::response(201, "Kullanici engellendi.");
            return crow::response(500);
        }
            });

    CROW_ROUTE(app, "/api/friends/blocks/<string>").methods("DELETE"_method)
        ([&db](const crow::request& req, std::string targetId) {
        if (!checkAuth(req, db)) return crow::response(401);
        if (db.unblockUser(getUserIdFromHeader(req), targetId)) return crow::response(200, "Engel kaldirildi.");
        return crow::response(500);
            });

    // --- SUNUCU AYARLARI (Server Settings CRUD) ---
    CROW_ROUTE(app, "/api/servers/<string>/settings").methods("GET"_method, "PUT"_method)
        ([&db](const crow::request& req, std::string serverId) {
        if (!checkAuth(req, db)) return crow::response(401);
        std::string myId = getUserIdFromHeader(req);

        // Sadece Kurucu veya 'Admin' yetkisine sahip kişiler ayar değiştirebilir
        if (!db.hasServerPermission(serverId, myId, "ADMIN")) return crow::response(403, "Yetkiniz yetersiz.");

        if (req.method == "GET"_method) {
            std::string settingsJson = db.getServerSettings(serverId);
            return crow::response(200, crow::json::load(settingsJson));
        }
        else {
            if (db.updateServerSettings(serverId, req.body)) return crow::response(200, "Ayarlar guncellendi.");
            return crow::response(500);
        }
            });

    // --- ROL DÜZENLEME VE SİLME (Role CRUD) ---
    CROW_ROUTE(app, "/api/roles/<string>").methods("PUT"_method, "DELETE"_method)
        ([&db](const crow::request& req, std::string roleId) {
        if (!checkAuth(req, db)) return crow::response(401);

        std::string serverId = db.getServerIdByRoleId(roleId);
        // Sadece Kurucu Rol Silebilir veya Düzenleyebilir
        auto srv = db.getServerDetails(serverId);
        if (!srv || srv->owner_id != getUserIdFromHeader(req)) return crow::response(403);

        if (req.method == "PUT"_method) {
            auto x = crow::json::load(req.body);
            if (!x || !x.has("name") || !x.has("hierarchy") || !x.has("permissions")) return crow::response(400);
            if (db.updateRole(roleId, std::string(x["name"].s()), x["hierarchy"].i(), x["permissions"].i())) return crow::response(200);
        }
        else {
            if (db.deleteRole(roleId)) return crow::response(200);
        }
        return crow::response(500);
            });

    // --- ÜYEYE ROL ATAMA (Assign Role) ---
    CROW_ROUTE(app, "/api/servers/<string>/members/<string>/roles").methods("POST"_method)
        ([&db](const crow::request& req, std::string serverId, std::string targetUserId) {
        if (!checkAuth(req, db)) return crow::response(401);
        std::string myId = getUserIdFromHeader(req);

        // Sadece Admin veya Kurucu rol atayabilir
        if (!db.hasServerPermission(serverId, myId, "ADMIN") && db.getServerDetails(serverId)->owner_id != myId) {
            return crow::response(403, "Rol atama yetkiniz yok.");
        }

        auto x = crow::json::load(req.body);
        if (!x || !x.has("role_id")) return crow::response(400);
        if (db.assignRoleToMember(serverId, targetUserId, std::string(x["role_id"].s()))) return crow::response(200, "Rol atandi.");
        return crow::response(500);
            });

    // --- KANBAN KARTINA KİŞİ ATAMA (Sadece Sunucu Üyeleri) ---
    CROW_ROUTE(app, "/api/cards/<string>/assign").methods("PUT"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("assignee_id")) return crow::response(400);

        std::string assigneeId = std::string(x["assignee_id"].s());
        std::string serverId = db.getServerIdByCardId(cardId);

        // Güvenlik: Atanacak kişi bu sunucunun üyesi mi?
        if (!db.isUserInServer(serverId, assigneeId)) {
            return crow::response(400, "Hata: Bu kisi sunucunun bir uyesi degil. Sadece uyeler atanabilir.");
        }

        if (db.assignUserToCard(cardId, assigneeId)) return crow::response(200, "Kisi karta atandi.");
        return crow::response(500);
            });

    // --- KANBAN KARTI TAMAMLANMA DURUMU (Is Completed) ---
    CROW_ROUTE(app, "/api/cards/<string>/status").methods("PUT"_method)
        ([&db](const crow::request& req, std::string cardId) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("is_completed")) return crow::response(400);

        if (db.updateCardCompletion(cardId, x["is_completed"].b())) return crow::response(200, "Kart durumu guncellendi.");
        return crow::response(500);
            });

    // --- ENTERPRISE VS NORMAL KULLANICI KANBAN LİMİT KONTROLÜ ---
    // (Kart oluşturma POST route'unuzun içine şu mantığı ekleyebilirsiniz)
    /* auto user = db.getUserById(myId);
       int cardCount = db.getListCardCount(listId);
       if (user->subscription_level == 0 && cardCount >= 10) {
           return crow::response(403, "Standart kullanicilar bir listede en fazla 10 kart olusturabilir. Limitsiz kullanim icin Enterprise'a gecin.");
       }
    */
    CROW_ROUTE(app, "/api/lists/<string>/cards").methods("POST"_method)
        ([&db](const crow::request& req, std::string listId) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("title") || !x.has("description") || !x.has("priority")) return crow::response(400);

        // Ek dosya veya görsel varsa URL'yi al, yoksa boş bırak
        std::string attachmentUrl = x.has("attachment_url") ? std::string(x["attachment_url"].s()) : "";

        // Atanacak kişi (Assignee) bilgisi varsa al
        std::string assigneeId = x.has("assignee_id") ? std::string(x["assignee_id"].s()) : "";

        if (db.createKanbanCard(listId, std::string(x["title"].s()), std::string(x["description"].s()), x["priority"].i(), assigneeId, attachmentUrl)) {
            return crow::response(201, "Görev kartı basariyla olusturuldu.");
            std::string dueDate = x.has("due_date") ? std::string(x["due_date"].s()) : "";
        }
        return crow::response(500);
            });

    // ==========================================================
    // BİLDİRİM SİSTEMİ (NOTIFICATIONS)
    // ==========================================================
    CROW_ROUTE(app, "/api/notifications").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        auto notifs = db.getUserNotifications(getUserIdFromHeader(req));
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
        if (!checkAuth(req, db)) return crow::response(401);
        if (db.markNotificationAsRead(notifId)) return crow::response(200);
        return crow::response(500);
            });

    // --- OTOMATİK HAYALET KULLANICI TEMİZLEYİCİ (BACKGROUND BOT) ---
    std::thread cleanupThread([&db]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(15));
            db.markInactiveUsersOffline(60);
        }
        });
    // --- KANBAN ZAMANLAYICI BOTU ---
    // Her 1 dakikada bir veritabanını tarar, saati yaklaşanlara veya bitenlere bildirim yazar.
    std::thread kanbanTimerThread([&db]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::minutes(1));
            db.processKanbanNotifications();
        }
        });
    kanbanTimerThread.detach();
    cleanupThread.detach();

    std::cout << "MySaaSApp (Hatasiz & Kararli Surum) Basariyla Calisiyor: http://localhost:8080" << std::endl;
    app.port(8080).multithreaded().run();
    return 0;
}