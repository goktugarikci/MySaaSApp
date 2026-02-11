#include "crow.h"
#include "db/DatabaseManager.h"
#include "utils/Security.h"
#include "utils/FileManager.h"

// --- YETKİ KONTROL SİMÜLASYONU ---
bool checkPermission(DatabaseManager& db, int userId, int serverId, const std::string& permission) {
    // TODO: Burada veritabanından kullanıcının rolü ve yetkisi kontrol edilmeli.
    // Şimdilik Admin (ID=1) ise her şeye yetkisi var diyoruz.
    if (userId == 1) return true;
    return true; // Test kolaylığı için herkese yetki veriyoruz (Geliştirme aşaması)
}

int main() {
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
    // 1. KİMLİK DOĞRULAMA (AUTH)
    // =============================================================

    // KAYIT OL
    CROW_ROUTE(app, "/api/auth/register").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Hatali JSON");

        if (db.createUser(x["name"].s(), x["email"].s(), x["password"].s())) {
            return crow::response(201, "Kayit basarili");
        }
        return crow::response(400, "Kayit basarisiz (Email kullanimda olabilir)");
            });

    // GİRİŞ YAP
    CROW_ROUTE(app, "/api/auth/login").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400);

        if (db.loginUser(x["email"].s(), x["password"].s())) {
            // Gerçek projede burada JWT Token üretilip dönülmeli.
            auto user = db.getUser(x["email"].s());
            crow::json::wvalue res;
            res["token"] = "fake-jwt-token-123456"; // Simülasyon
            res["user_id"] = user->id;
            res["message"] = "Giris basarili";
            return crow::response(200, res);
        }
        return crow::response(401, "Hatali e-posta veya sifre");
            });

    // PROFİLİM
    CROW_ROUTE(app, "/api/users/me")
        ([&db](const crow::request& req) {
        // TODO: Token'dan UserID alınmalı. Test için ID=1.
        int myId = 1;
        auto user = db.getUserById(myId); // Bu fonksiyonun eklendiğini varsayıyoruz
        if (!user) return crow::response(404);

        crow::json::wvalue res;
        res["id"] = user->id;
        res["name"] = user->name;
        res["email"] = user->email;
        res["avatar_url"] = user->avatar_url;
        return crow::response(200, res);
            });

    // =============================================================
    // 2. SOSYAL ETKİLEŞİM (ARKADAŞLIK)
    // =============================================================

    // ARKADAŞ LİSTESİ
    CROW_ROUTE(app, "/api/friends")
        ([&db](const crow::request& req) {
        int myId = 1; // Token'dan gelecek
        auto friends = db.getFriendsList(myId);

        crow::json::wvalue res;
        for (size_t i = 0; i < friends.size(); i++) {
            res[i]["id"] = friends[i].id;
            res[i]["name"] = friends[i].name;
            res[i]["status"] = friends[i].status;
            res[i]["avatar_url"] = friends[i].avatar_url;
        }
        return crow::response(200, res);
            });

    // ARKADAŞLIK İSTEĞİ GÖNDER
    CROW_ROUTE(app, "/api/friends/request").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400);

        int myId = 1;
        int targetId = x["target_id"].i();

        if (db.sendFriendRequest(myId, targetId))
            return crow::response(200, "Istek gonderildi");
        return crow::response(400, "Istek gonderilemedi");
            });

    // İSTEĞİ KABUL ET
    CROW_ROUTE(app, "/api/friends/request/<int>").methods("PUT"_method)
        ([&db](const crow::request& req, int requesterId) {
        int myId = 1;
        // Body'den action: "accept" kontrolü yapılabilir
        if (db.acceptFriendRequest(requesterId, myId))
            return crow::response(200, "Arkadaslik kabul edildi");
        return crow::response(400, "Islem basarisiz");
            });

    // =============================================================
    // 3. SUNUCU YÖNETİMİ
    // =============================================================

    // SUNUCULARIMI GETİR
    CROW_ROUTE(app, "/api/servers")
        ([&db]() {
        int myId = 1;
        auto servers = db.getUserServers(myId);

        crow::json::wvalue res;
        for (size_t i = 0; i < servers.size(); i++) {
            res[i]["id"] = servers[i].id;
            res[i]["name"] = servers[i].name;
        }
        return crow::response(200, res);
            });

    // YENİ SUNUCU OLUŞTUR
    CROW_ROUTE(app, "/api/servers").methods("POST"_method)
        ([&db](const crow::request& req) {
        int myId = 1;
        auto x = crow::json::load(req.body);
        std::string name = x["name"].s();

        int srvId = db.createServer(name, myId);
        if (srvId > 0) {
            crow::json::wvalue res;
            res["id"] = srvId;
            res["message"] = "Sunucu olusturuldu";
            return crow::response(201, res);
        }
        return crow::response(500);
            });

    // SUNUCU KANALLARINI GETİR
    CROW_ROUTE(app, "/api/servers/<int>/channels")
        ([&db](int serverId) {
        auto channels = db.getServerChannels(serverId);
        crow::json::wvalue res;
        for (size_t i = 0; i < channels.size(); i++) {
            res[i]["id"] = channels[i].id;
            res[i]["name"] = channels[i].name;
            res[i]["type"] = channels[i].type; // 3 ise Kanban
        }
        return crow::response(200, res);
            });

    // =============================================================
    // 6. PROJE YÖNETİMİ (KANBAN / TRELLO)
    // =============================================================

    // PANOYU GETİR (Board + Lists + Cards)
    CROW_ROUTE(app, "/api/boards/<int>")
        ([&db](int channelId) {
        // 1. Yetki Kontrolü (Server ID bulunup bakılmalı)
        // if (!checkPermission(...)) return crow::response(403);

        auto board = db.getKanbanBoard(channelId);

        crow::json::wvalue res;
        for (size_t i = 0; i < board.size(); i++) {
            res[i]["id"] = board[i].id;
            res[i]["title"] = board[i].title;
            res[i]["position"] = board[i].position;

            // Kartları ekle
            for (size_t j = 0; j < board[i].cards.size(); j++) {
                res[i]["cards"][j]["id"] = board[i].cards[j].id;
                res[i]["cards"][j]["title"] = board[i].cards[j].title;
                res[i]["cards"][j]["priority"] = board[i].cards[j].priority;
            }
        }
        return crow::response(200, res);
            });

    // YENİ LİSTE EKLE
    CROW_ROUTE(app, "/api/boards/<int>/lists").methods("POST"_method)
        ([&db](const crow::request& req, int channelId) {
        auto x = crow::json::load(req.body);
        if (db.createKanbanList(channelId, x["title"].s()))
            return crow::response(200, "Liste eklendi");
        return crow::response(500);
            });

    // YENİ KART EKLE
    CROW_ROUTE(app, "/api/lists/<int>/cards").methods("POST"_method)
        ([&db](const crow::request& req, int listId) {
        auto x = crow::json::load(req.body);
        // title, desc, priority
        if (db.createKanbanCard(listId, x["title"].s(), x["description"].s(), x["priority"].i()))
            return crow::response(200, "Kart eklendi");
        return crow::response(500);
            });

    // KART TAŞIMA (SÜRÜKLE BIRAK)
    CROW_ROUTE(app, "/api/cards/<int>/move").methods("PUT"_method)
        ([&db](const crow::request& req, int cardId) {
        auto x = crow::json::load(req.body);
        int newListId = x["new_list_id"].i();
        int newPos = x["new_position"].i();

        if (db.moveCard(cardId, newListId, newPos))
            return crow::response(200, "Kart tasindi");
        return crow::response(500);
            });

    // =============================================================
    // 5. MESAJLAŞMA
    // =============================================================

    // MESAJ GÖNDER
    CROW_ROUTE(app, "/api/channels/<int>/messages").methods("POST"_method)
        ([&db](const crow::request& req, int channelId) {
        int myId = 1; // Token'dan gelecek
        auto x = crow::json::load(req.body);

        std::string content = x["content"].s();
        std::string attachment = "";
        if (x.has("attachment_url")) attachment = x["attachment_url"].s();

        if (db.sendMessage(channelId, myId, content, attachment))
            return crow::response(200, "Mesaj gonderildi");
        return crow::response(500);
            });

    // GEÇMİŞ MESAJLARI ÇEK
    CROW_ROUTE(app, "/api/channels/<int>/messages")
        ([&db](int channelId) {
        auto msgs = db.getChannelMessages(channelId, 50); // Son 50 mesaj

        crow::json::wvalue res;
        for (size_t i = 0; i < msgs.size(); i++) {
            res[i]["id"] = msgs[i].id;
            res[i]["sender_name"] = msgs[i].sender_name;
            res[i]["content"] = msgs[i].content;
            res[i]["avatar"] = msgs[i].sender_avatar;
            res[i]["attachment"] = msgs[i].attachment_url;
        }
        return crow::response(200, res);
            });

    // DOSYA YÜKLEME (ÖNCEKİ KODUN AYNI KALMASI GEREKİYOR)
    // POST /api/upload kısmını buraya önceki cevaptan ekleyebilirsiniz.

    app.port(8080).multithreaded().run();
    return 0;
}