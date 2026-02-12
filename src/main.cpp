#include "crow.h"
#include "db/DatabaseManager.h"
#include "utils/Security.h"
#include "utils/FileManager.h"
#include <cpr/cpr.h> // Google Auth ve HTTP İstekleri için

// --- YAPILANDIRMA ---
const std::string GOOGLE_CLIENT_ID = "BURAYA_GOOGLE_CLIENT_ID_YAZIN";
const std::string GOOGLE_CLIENT_SECRET = "BURAYA_GOOGLE_CLIENT_SECRET_YAZIN";
const std::string GOOGLE_REDIRECT_URI = "http://localhost:8080/api/auth/google/callback";

// Yetki Seviyeleri ve Token Kontrolü
enum AuthResult { AUTHORIZED, UNAUTHORIZED, FORBIDDEN };

AuthResult validateAccess(const crow::request& req, DatabaseManager& db, UserRole requiredRole) {
    auto authHeader = req.get_header_value("Authorization");
    if (authHeader.empty() || authHeader.find("Bearer ") != 0) return UNAUTHORIZED;

    // "Bearer mock-jwt-token-ID" formatından ID'yi al
    std::string token = authHeader.substr(7);
    if (token.find("mock-jwt-token-") != 0) return UNAUTHORIZED;

    try {
        int userId = std::stoi(token.substr(15));
        auto user = db.getUserById(userId);
        if (!user) return UNAUTHORIZED;

        // Rol Kontrolü: Admin her şeye erişir (Hierarchy)
        if (user->is_system_admin) return AUTHORIZED;

        // Basit hiyerarşi kontrolü (Sayısal değer karşılaştırması)
        // requiredRole: 3 (ADMIN), user: 0 (USER) -> FORBIDDEN
        if (static_cast<int>(requiredRole) == 3 && !user->is_system_admin) return FORBIDDEN;

        return AUTHORIZED;
    }
    catch (...) {
        return UNAUTHORIZED;
    }
}


// --- GÜVENLİK KATMANI (Middleware) ---
// Bu fonksiyon, isteği yapan kişinin yetkili olup olmadığını kontrol eder.
bool checkAuth(const crow::request& req, DatabaseManager& db, bool requireAdmin = false) {
    // 1. Header Kontrolü (Authorization: Bearer <token>)
    auto authHeader = req.get_header_value("Authorization");
    if (authHeader.empty()) return false;

    // 2. Token Ayrıştırma (Basitlik için mock token kullanıyoruz: "mock-jwt-token-USERID")
    // Gerçek sistemde burada JWT Verify işlemi yapılır.
    std::string token = authHeader.substr(0, 15); // "mock-jwt-token-"
    if (token != "mock-jwt-token-") return false; // Geçersiz format

    std::string userIdStr = authHeader.substr(15);
    int userId = 0;
    try {
        userId = std::stoi(userIdStr);
    }
    catch (...) { return false; }

    if (userId <= 0) return false;

    // 3. Eğer Admin yetkisi gerekiyorsa DB'den kontrol et
    if (requireAdmin) {
        return db.isSystemAdmin(userId);
    }

    return true; // Giriş yapmış olması yeterli
}

// Token'dan ID dönen yardımcı (Güvenli kabul ettiğimiz yerlerde)
int getUserIdFromHeader(const crow::request& req) {
    auto authHeader = req.get_header_value("Authorization");
    if (authHeader.empty()) return 0;
    try {
        return std::stoi(authHeader.substr(15));
    }
    catch (...) { return 0; }
}

int main() {
    // 1. Başlangıç Hazırlıkları
    FileManager::initDirectories();

    DatabaseManager db("mysaasapp.db");
    if (db.open()) {
        std::cout << "Veritabani baglantisi basarili.\n";
        db.initTables();
    }
    else {
        std::cerr << "Veritabani acilamadi!\n";
        return -1;
    }

    crow::SimpleApp app;

    // =============================================================
    // 1. KİMLİK DOĞRULAMA (AUTH & GOOGLE)
    // =============================================================

    CROW_ROUTE(app, "/api/auth/register").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Hatali JSON");

        if (!x.has("name") || !x.has("email") || !x.has("password"))
            return crow::response(400, "Eksik bilgi: name, email, password gerekli.");

        if (db.createUser(x["name"].s(), x["email"].s(), x["password"].s())) {
            return crow::response(201, "Kayit basarili");
        }
        return crow::response(400, "Kayit basarisiz (Email kullanimda olabilir)");
            });

    CROW_ROUTE(app, "/api/auth/login").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400);

        if (db.loginUser(x["email"].s(), x["password"].s())) {
            auto user = db.getUser(x["email"].s());
            if (!user) return crow::response(500, "Kullanici verisi alinamadi");

            crow::json::wvalue res = user->toJson();
            res["token"] = "mock-jwt-token-" + std::to_string(user->id);
            return crow::response(200, res);
        }
        return crow::response(401, "Hatali e-posta veya sifre");
            });

    CROW_ROUTE(app, "/api/auth/google/url")
        ([]() {
        std::string url = "https://accounts.google.com/o/oauth2/v2/auth?"
            "client_id=" + GOOGLE_CLIENT_ID +
            "&redirect_uri=" + GOOGLE_REDIRECT_URI +
            "&response_type=code"
            "&scope=email%20profile";
        crow::json::wvalue res;
        res["url"] = url;
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/auth/google/callback").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("code")) return crow::response(400, "Code gerekli");
        std::string code = x["code"].s();

        cpr::Response r = cpr::Post(cpr::Url{ "https://oauth2.googleapis.com/token" },
            cpr::Payload{ {"client_id", GOOGLE_CLIENT_ID},
                         {"client_secret", GOOGLE_CLIENT_SECRET},
                         {"code", code},
                         {"grant_type", "authorization_code"},
                         {"redirect_uri", GOOGLE_REDIRECT_URI} });

        auto tokenJson = crow::json::load(r.text);
        if (!tokenJson || !tokenJson.has("access_token"))
            return crow::response(400, "Google token hatasi: " + r.text);

        std::string accessToken = tokenJson["access_token"].s();

        cpr::Response userRes = cpr::Get(cpr::Url{ "https://www.googleapis.com/oauth2/v1/userinfo" },
            cpr::Bearer{ accessToken });

        auto userInfo = crow::json::load(userRes.text);
        if (!userInfo) return crow::response(400, "Google user info hatasi");

        std::string googleId = userInfo["id"].s();
        std::string email = userInfo["email"].s();
        std::string name = userInfo["name"].s();
        std::string picture = userInfo["picture"].s();

        auto existingUser = db.getUserByGoogleId(googleId);
        int userId = 0;

        if (existingUser) {
            userId = existingUser->id;
        }
        else {
            if (db.createGoogleUser(name, email, googleId, picture)) {
                auto newUser = db.getUserByGoogleId(googleId);
                if (newUser) userId = newUser->id;
            }
            else {
                return crow::response(500, "Google kullanicisi kaydedilemedi");
            }
        }

        if (userId == 0) return crow::response(500, "Kullanici ID hatasi");

        crow::json::wvalue res;
        res["user_id"] = userId;
        res["token"] = "mock-jwt-google-" + std::to_string(userId);
        res["message"] = "Giris basarili";
        return crow::response(200, res);
            });

    // =============================================================
    // 2. KULLANICI YÖNETİMİ
    // =============================================================

    CROW_ROUTE(app, "/api/users/me")
        ([&db](const crow::request& req) {
        int myId = 1; // TODO: JWT'den al
        auto user = db.getUserById(myId);
        if (user) return crow::response(200, user->toJson());
        return crow::response(404, "Kullanici bulunamadi");
            });

    CROW_ROUTE(app, "/api/users/me").methods("PUT"_method)
        ([&db](const crow::request& req) {
        int myId = 1;
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400);

        if (db.updateUserDetails(myId, x["name"].s(), x["status"].s()))
            return crow::response(200, "Profil guncellendi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/users/me").methods("DELETE"_method)
        ([&db]() {
        int myId = 1;
        if (db.deleteUser(myId)) return crow::response(200, "Hesap silindi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/users/me/avatar").methods("PUT"_method)
        ([&db](const crow::request& req) {
        int myId = 1;
        auto x = crow::json::load(req.body);
        if (!x || !x.has("avatar_url")) return crow::response(400);

        if (db.updateUserAvatar(myId, x["avatar_url"].s()))
            return crow::response(200, "Avatar guncellendi");
        return crow::response(500);
            });

    // =============================================================
    // 3. DOSYA SİSTEMİ
    // =============================================================

    CROW_ROUTE(app, "/api/upload").methods("POST"_method)
        ([&db](const crow::request& req) {
        crow::multipart::message msg(req);
        std::string original_filename, file_content, upload_type;
        bool has_file = false, has_type = false;

        for (const auto& part : msg.parts) {
            const auto& content_disposition = part.get_header_object("Content-Disposition");
            if (content_disposition.params.count("name")) {
                std::string name = content_disposition.params.at("name");
                if (name == "file") {
                    if (content_disposition.params.count("filename"))
                        original_filename = content_disposition.params.at("filename");
                    file_content = part.body;
                    has_file = true;
                }
                else if (name == "type") {
                    upload_type = part.body;
                    has_type = true;
                }
            }
        }

        if (!has_file || !has_type)
            return crow::response(400, "Eksik parametre: file ve type");

        try {
            auto fType = (upload_type == "avatar") ? FileManager::FileType::AVATAR : FileManager::FileType::ATTACHMENT;
            std::string url = FileManager::saveFile(file_content, original_filename, fType);

            crow::json::wvalue result;
            result["url"] = url;
            return crow::response(200, result);
        }
        catch (const std::exception& e) {
            return crow::response(500, e.what());
        }
            });

    CROW_ROUTE(app, "/uploads/<path>")
        ([](const crow::request& req, crow::response& res, std::string path) {
        std::string content = FileManager::readFile("/uploads/" + path);
        if (content.empty()) { res.code = 404; res.write("Dosya yok"); }
        else {
            res.code = 200;
            if (path.find(".png") != std::string::npos) res.set_header("Content-Type", "image/png");
            else if (path.find(".jpg") != std::string::npos) res.set_header("Content-Type", "image/jpeg");
            res.write(content);
        }
        res.end();
            });

    // =============================================================
    // 4. ARKADAŞLIK
    // =============================================================

    CROW_ROUTE(app, "/api/friends")
        ([&db]() {
        int myId = 1;
        auto friends = db.getFriendsList(myId);
        crow::json::wvalue res;
        for (size_t i = 0; i < friends.size(); i++) res[i] = friends[i].toJson();
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/friends/requests")
        ([&db]() {
        int myId = 1;
        auto reqs = db.getPendingRequests(myId);
        crow::json::wvalue res;
        for (size_t i = 0; i < reqs.size(); i++) res[i] = reqs[i].toJson();
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/friends/request").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_id")) return crow::response(400);

        if (db.sendFriendRequest(1, x["target_id"].i())) return crow::response(200, "Istek gonderildi");
        return crow::response(400);
            });

    CROW_ROUTE(app, "/api/friends/request/<int>").methods("PUT"_method)
        ([&db](const crow::request& req, int requesterId) {
        if (db.acceptFriendRequest(requesterId, 1)) return crow::response(200, "Kabul edildi");
        return crow::response(400);
            });

    CROW_ROUTE(app, "/api/friends/<int>").methods("DELETE"_method)
        ([&db](int otherId) {
        if (db.rejectOrRemoveFriend(otherId, 1)) return crow::response(200, "Silindi/Reddedildi");
        return crow::response(500);
            });

    // =============================================================
    // 5. SUNUCU & KANAL
    // =============================================================

    CROW_ROUTE(app, "/api/servers")
        ([&db]() {
        auto servers = db.getUserServers(1);
        crow::json::wvalue res;
        for (size_t i = 0; i < servers.size(); i++) res[i] = servers[i].toJson();
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/servers/<int>")
        ([&db](int id) {
        auto s = db.getServerDetails(id);
        if (s) return crow::response(200, s->toJson());
        return crow::response(404);
            });

    CROW_ROUTE(app, "/api/servers").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400);

        int id = db.createServer(x["name"].s(), 1);
        if (id > 0) {
            crow::json::wvalue res; res["id"] = id;
            return crow::response(201, res);
        }
        return crow::response(403, "Limit asimi");
            });

    CROW_ROUTE(app, "/api/servers/<int>").methods("PUT"_method)
        ([&db](const crow::request& req, int id) {
        auto x = crow::json::load(req.body);
        if (db.updateServer(id, x["name"].s(), x["icon_url"].s())) return crow::response(200, "Guncellendi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/servers/<int>").methods("DELETE"_method)
        ([&db](int id) {
        if (db.deleteServer(id)) return crow::response(200, "Silindi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/servers/<int>/channels")
        ([&db](int srvId) {
        auto channels = db.getServerChannels(srvId);
        crow::json::wvalue res;
        for (size_t i = 0; i < channels.size(); i++) res[i] = channels[i].toJson();
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/servers/<int>/channels").methods("POST"_method)
        ([&db](const crow::request& req, int srvId) {
        auto x = crow::json::load(req.body);
        if (db.createChannel(srvId, x["name"].s(), x["type"].i())) return crow::response(201, "Olusturuldu");
        return crow::response(403, "Limit hatasi");
            });

    CROW_ROUTE(app, "/api/channels/<int>").methods("PUT"_method)
        ([&db](const crow::request& req, int id) {
        auto x = crow::json::load(req.body);
        if (db.updateChannel(id, x["name"].s())) return crow::response(200, "Guncellendi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/channels/<int>").methods("DELETE"_method)
        ([&db](int id) {
        if (db.deleteChannel(id)) return crow::response(200, "Silindi");
        return crow::response(500);
            });

    // =============================================================
    // 6. MESAJLAŞMA
    // =============================================================

    CROW_ROUTE(app, "/api/channels/<int>/messages")
        ([&db](int chId) {
        auto msgs = db.getChannelMessages(chId, 50);
        crow::json::wvalue res;
        for (size_t i = 0; i < msgs.size(); i++) res[i] = msgs[i].toJson();
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/channels/<int>/messages").methods("POST"_method)
        ([&db](const crow::request& req, int chId) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("content")) return crow::response(400);

        // HATA DÜZELTİLDİ: Ternary operatörü yerine güvenli kontrol
        std::string attach = "";
        if (x.has("attachment_url")) {
            attach = x["attachment_url"].s();
        }

        if (db.sendMessage(chId, 1, x["content"].s(), attach)) return crow::response(201, "Gonderildi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/messages/<int>").methods("PUT"_method)
        ([&db](const crow::request& req, int msgId) {
        auto x = crow::json::load(req.body);
        if (db.updateMessage(msgId, x["content"].s())) return crow::response(200, "Duzenlendi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/messages/<int>").methods("DELETE"_method)
        ([&db](int msgId) {
        if (db.deleteMessage(msgId)) return crow::response(200, "Silindi");
        return crow::response(500);
            });

    // =============================================================
    // 7. KANBAN / TRELLO
    // =============================================================

    CROW_ROUTE(app, "/api/boards/<int>")
        ([&db](int chId) {
        auto board = db.getKanbanBoard(chId);
        crow::json::wvalue res;
        for (size_t i = 0; i < board.size(); i++) res[i] = board[i].toJson();
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/boards/<int>/lists").methods("POST"_method)
        ([&db](const crow::request& req, int chId) {
        auto x = crow::json::load(req.body);
        if (db.createKanbanList(chId, x["title"].s())) return crow::response(201, "Liste eklendi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/lists/<int>").methods("PUT"_method)
        ([&db](const crow::request& req, int listId) {
        auto x = crow::json::load(req.body);
        if (db.updateKanbanList(listId, x["title"].s(), x["position"].i())) return crow::response(200, "Liste guncellendi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/lists/<int>").methods("DELETE"_method)
        ([&db](int listId) {
        if (db.deleteKanbanList(listId)) return crow::response(200, "Liste silindi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/lists/<int>/cards").methods("POST"_method)
        ([&db](const crow::request& req, int listId) {
        auto x = crow::json::load(req.body);
        if (db.createKanbanCard(listId, x["title"].s(), x["description"].s(), x["priority"].i()))
            return crow::response(201, "Kart eklendi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/cards/<int>").methods("PUT"_method)
        ([&db](const crow::request& req, int cardId) {
        auto x = crow::json::load(req.body);
        if (db.updateKanbanCard(cardId, x["title"].s(), x["description"].s(), x["priority"].i()))
            return crow::response(200, "Kart guncellendi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/cards/<int>/move").methods("PUT"_method)
        ([&db](const crow::request& req, int cardId) {
        auto x = crow::json::load(req.body);
        if (db.moveCard(cardId, x["new_list_id"].i(), x["new_position"].i()))
            return crow::response(200, "Kart tasindi");
        return crow::response(500);
            });

    CROW_ROUTE(app, "/api/cards/<int>").methods("DELETE"_method)
        ([&db](int cardId) {
        if (db.deleteKanbanCard(cardId)) return crow::response(200, "Kart silindi");
        return crow::response(500);
            });
    // =============================================================
    // 8. BİREBİR SOHBET (DM)
    // =============================================================

    // DM Başlat veya Getir
    CROW_ROUTE(app, "/api/dm/<int>").methods("POST"_method)
        ([&db](const crow::request& req, int targetUserId) {
        int myId = 1; // Token'dan alınacak
        int channelId = db.getOrCreateDMChannel(myId, targetUserId);

        if (channelId > 0) {
            crow::json::wvalue res;
            res["channel_id"] = channelId;
            return crow::response(200, res);
        }
        return crow::response(500, "DM olusturulamadi");
            });

    // =============================================================
    // 9. GÖRÜNTÜLÜ SOHBET / EKRAN PAYLAŞIMI (WebRTC Signaling)
    // =============================================================

    // Bu endpoint, görüntülü görüşme başlatmak isteyenlerin birbirini bulmasını sağlar.
    // İstemci (Front-end) buraya bağlanıp SDP ve ICE Candidate bilgilerini takas eder.

    CROW_WEBSOCKET_ROUTE(app, "/ws/call")
        .onopen([&](crow::websocket::connection& conn) {
        // Kullanıcı bağlandı, bir havuza ekle
            })
        .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
        // Gelen WebRTC sinyal verisini (Offer/Answer/Candidate) 
        // hedef kullanıcıya ilet.
        auto msg = crow::json::load(data);
        // Örnek: { "target_user_id": 5, "type": "offer", "sdp": "..." }

        // Hedef kullanıcıyı bul ve ona gönder...
            });
    // =============================================================
    // 8. SİSTEM YÖNETİCİSİ (ADMIN) API
    // =============================================================

    // İstatistikler
    CROW_ROUTE(app, "/api/admin/stats")
        ([&db]() {
        // TODO: IsSystemAdmin kontrolü yapılmalı
        auto stats = db.getSystemStats();
        crow::json::wvalue res;
        res["users"] = stats.user_count;
        res["servers"] = stats.server_count;
        res["messages"] = stats.message_count;
        return crow::response(200, res);
            });

    // Kullanıcıları Listele
    CROW_ROUTE(app, "/api/admin/users")
        ([&db]() {
        auto users = db.getAllUsers();
        crow::json::wvalue res;
        for (size_t i = 0; i < users.size(); i++) res[i] = users[i].toJson();
        return crow::response(200, res);
            });

    // Kullanıcı Banla
    CROW_ROUTE(app, "/api/admin/users/<int>/ban").methods("POST"_method)
        ([&db](int userId) {
        if (db.banUser(userId)) return crow::response(200, "Kullanici yasaklandi");
        return crow::response(500);
            });

    // =============================================================
    // 9. ÜYE VE ROL YÖNETİMİ API
    // =============================================================

    // Davet Kodu ile Katıl
    CROW_ROUTE(app, "/api/servers/join").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("invite_code")) return crow::response(400);

        int myId = 1; // Token'dan alınmalı
        if (db.joinServerByCode(myId, x["invite_code"].s()))
            return crow::response(200, "Sunucuya katildiniz");
        return crow::response(400, "Gecersiz kod veya zaten uyesiniz");
            });

    // Üye At (Kick)
    CROW_ROUTE(app, "/api/servers/<int>/members/<int>").methods("DELETE"_method)
        ([&db](int serverId, int userId) {
        // TODO: Yetki kontrolü (Requester Owner mı?)
        if (db.kickMember(serverId, userId)) return crow::response(200, "Uye atildi");
        return crow::response(500);
            });

    // Rolleri Listele
    CROW_ROUTE(app, "/api/servers/<int>/roles")
        ([&db](int serverId) {
        auto roles = db.getServerRoles(serverId);
        crow::json::wvalue res;
        for (size_t i = 0; i < roles.size(); i++) res[i] = roles[i].toJson();
        return crow::response(200, res);
            });

    // Yeni Rol Oluştur
    CROW_ROUTE(app, "/api/servers/<int>/roles").methods("POST"_method)
        ([&db](const crow::request& req, int serverId) {
        auto x = crow::json::load(req.body);
        if (db.createRole(serverId, x["name"].s(), x["hierarchy"].i(), x["permissions"].i()))
            return crow::response(201, "Rol olusturuldu");
        return crow::response(500);
            });

    // =============================================================
    // 10. BİREBİR SOHBET (DM) API
    // =============================================================

    // DM Kanalı Aç veya Getir
    CROW_ROUTE(app, "/api/dm/<int>").methods("POST"_method)
        ([&db](const crow::request& req, int targetUserId) {
        int myId = 1; // Token'dan alınmalı
        int channelId = db.getOrCreateDMChannel(myId, targetUserId);

        if (channelId > 0) {
            crow::json::wvalue res;
            res["channel_id"] = channelId;
            return crow::response(200, res);
        }
        return crow::response(500, "DM acilamadi");
            });

    // =============================================================
    // 11. ÖDEME SİSTEMİ (PAYMENT GATEWAY INTEGRATION)
    // =============================================================

    // Ödeme Başlat (Checkout)
    CROW_ROUTE(app, "/api/payments/create-checkout").methods("POST"_method)
        ([&db](const crow::request& req) {
        // Güvenlik: Sadece giriş yapmış kullanıcılar
        if (!checkAuth(req, db)) return crow::response(401);
        int myId = getUserIdFromHeader(req);

        auto x = crow::json::load(req.body);
        if (!x || !x.has("amount")) return crow::response(400);

        // 1. Harici Ödeme Sağlayıcıya (Stripe/Iyzico) İstek Atılır (Burada simüle ediyoruz)
        // cpr::Post("https://api.stripe.com/v1/payment_intents", ...);

        std::string providerId = "pay_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(myId);
        float amount = (float)x["amount"].d();

        // 2. Veritabanına "Bekliyor" olarak kaydet
        db.createPaymentRecord(myId, providerId, amount, "USD");

        crow::json::wvalue res;
        res["payment_token"] = providerId; // Frontend bu ID ile ödeme ekranını açar
        res["checkout_url"] = "https://checkout.stripe.com/pay/" + providerId;
        return crow::response(200, res);
            });

    // Webhook (Ödeme Sağlayıcıdan Gelen Onay)
    CROW_ROUTE(app, "/api/payments/webhook").methods("POST"_method)
        ([&db](const crow::request& req) {
        // Bu endpointi dış dünya çağırır, o yüzden kullanıcı token kontrolü yapılmaz.
        // Ancak Stripe Signature kontrolü yapılmalıdır.

        auto x = crow::json::load(req.body);
        if (x && x.has("payment_id") && x.has("status")) {
            std::string pid = x["payment_id"].s();
            std::string status = x["status"].s(); // 'success'

            db.updatePaymentStatus(pid, status);

            // Eğer başarılıysa kullanıcının aboneliğini güncelle
            if (status == "success") {
                // Burada payment_id'den user_id'yi bulup updateSubscription çağrılmalı
                // db.updateUserSubscription(userId, 1, 30);
            }
            return crow::response(200);
        }
        return crow::response(400);
            });

    // Ödeme Geçmişim
    CROW_ROUTE(app, "/api/payments/history")
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        int myId = getUserIdFromHeader(req);

        auto payments = db.getUserPayments(myId);
        crow::json::wvalue res;
        for (size_t i = 0; i < payments.size(); i++) {
            res[i]["id"] = payments[i].provider_payment_id;
            res[i]["amount"] = payments[i].amount;
            res[i]["status"] = payments[i].status;
        }
        return crow::response(200, res);
            });

    // =============================================================
    // 12. RAPORLAMA & DENETİM
    // =============================================================

    // İçerik Bildir (Kullanıcılar İçin)
    CROW_ROUTE(app, "/api/reports").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db)) return crow::response(401);
        int myId = getUserIdFromHeader(req);

        auto x = crow::json::load(req.body);
        // content_id, type (MESSAGE/USER), reason
        if (db.createReport(myId, x["content_id"].i(), x["type"].s(), x["reason"].s()))
            return crow::response(201, "Sikayet alindi");
        return crow::response(500);
            });

    // Raporları Listele (SADECE ADMIN)
    // DİKKAT: Burada requireAdmin = true gönderiyoruz.
    CROW_ROUTE(app, "/api/admin/reports")
        ([&db](const crow::request& req) {
        if (!checkAuth(req, db, true)) return crow::response(403, "Yetkisiz Erisim: Admin Degilsiniz");

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

    std::cout << "MySaaSApp Baslatildi: http://localhost:8080" << std::endl;
    app.port(8080).multithreaded().run();
    return 0;
}
