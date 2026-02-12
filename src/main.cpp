#include "crow.h"
#include "db/DatabaseManager.h"
#include "utils/Security.h"
#include "utils/FileManager.h"

int main() {
    // 1. Dosya Sistemi ve Veritabanı Hazırlığı
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

        // Basit validasyon
        if (!x.has("name") || !x.has("email") || !x.has("password"))
            return crow::response(400, "Eksik bilgi: name, email, password gerekli.");

        if (db.createUser(x["name"].s(), x["email"].s(), x["password"].s())) {
            return crow::response(201, "Kayit basarili");
        }
        return crow::response(400, "Kayit basarisiz (Email kullanimda olabilir)");
            });

    // GİRİŞ YAP
    CROW_ROUTE(app, "/api/auth/login").methods("POST"_method)
        ([&db](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Hatali JSON");

        if (db.loginUser(x["email"].s(), x["password"].s())) {
            auto user = db.getUser(x["email"].s());

            crow::json::wvalue res;
            res["token"] = "mock-jwt-token-123456"; // Gerçek projede JWT üretilmeli
            res["user_id"] = user->id;
            res["name"] = user->name;
            res["avatar_url"] = user->avatar_url;
            return crow::response(200, res);
        }
        return crow::response(401, "Hatali e-posta veya sifre");
            });

    // PROFİL BİLGİLERİM
    CROW_ROUTE(app, "/api/users/me")
        ([&db](const crow::request& req) {
        // TODO: Token'dan UserID alınmalı. Test için ID=1.
        int myId = 1;

        // ID ile kullanıcı çekme fonksiyonu (getUserById) kullanılmalı
        // Şimdilik örnek veri dönüyoruz veya email ile çekebiliriz
        auto user = db.getUserById(myId);

        if (user) {
            crow::json::wvalue res;
            res["id"] = user->id;
            res["name"] = user->name;
            res["email"] = user->email;
            res["avatar_url"] = user->avatar_url;
            res["status"] = user->status;
            res["subscription_level"] = user->subscription_level;
            return crow::response(200, res);
        }
        return crow::response(404, "Kullanici bulunamadi");
            });

    // PROFİL FOTOĞRAFI GÜNCELLEME
    CROW_ROUTE(app, "/api/users/me/avatar").methods("PUT"_method)
        ([&db](const crow::request& req) {
        int myId = 1; // Token'dan gelecek
        auto x = crow::json::load(req.body);
        if (!x || !x.has("avatar_url")) return crow::response(400, "avatar_url gerekli");

        if (db.updateUserAvatar(myId, x["avatar_url"].s()))
            return crow::response(200, "Avatar guncellendi");
        return crow::response(500, "Veritabani hatasi");
            });

    // =============================================================
    // 2. DOSYA YÖNETİMİ (UPLOAD)
    // =============================================================

    CROW_ROUTE(app, "/api/upload").methods("POST"_method)
        ([&db](const crow::request& req) {
        crow::multipart::message msg(req);

        std::string original_filename, file_content, upload_type;
        bool has_file = false, has_type = false;

        // Multipart veriyi güvenli şekilde ayrıştır
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
            return crow::response(400, "Eksik parametre: file ve type gereklidir.");

        try {
            FileManager::FileType fType = (upload_type == "avatar") ?
                FileManager::FileType::AVATAR :
                FileManager::FileType::ATTACHMENT;

            std::string fileUrl = FileManager::saveFile(file_content, original_filename, fType);

            crow::json::wvalue result;
            result["url"] = fileUrl;
            return crow::response(200, result);
        }
        catch (const std::exception& e) {
            return crow::response(500, e.what());
        }
            });

    // STATİK DOSYA SUNUMU
    CROW_ROUTE(app, "/uploads/<path>")
        ([](const crow::request& req, crow::response& res, std::string path) {
        std::string fullPath = "/uploads/" + path;
        std::string content = FileManager::readFile(fullPath);

        if (content.empty()) {
            res.code = 404;
            res.write("Dosya yok");
        }
        else {
            res.code = 200;
            if (path.find(".png") != std::string::npos) res.set_header("Content-Type", "image/png");
            else if (path.find(".jpg") != std::string::npos) res.set_header("Content-Type", "image/jpeg");
            res.write(content);
        }
        res.end();
            });

    // =============================================================
    // 3. ARKADAŞLIK SİSTEMİ
    // =============================================================

    CROW_ROUTE(app, "/api/friends")
        ([&db]() {
        int myId = 1; // Token'dan gelecek
        auto friends = db.getFriendsList(myId);

        crow::json::wvalue res;
        for (size_t i = 0; i < friends.size(); i++) {
            res[i]["id"] = friends[i].id;
            res[i]["name"] = friends[i].name;
            res[i]["avatar_url"] = friends[i].avatar_url;
            res[i]["status"] = friends[i].status;
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/friends/request").methods("POST"_method)
        ([&db](const crow::request& req) {
        int myId = 1;
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_id")) return crow::response(400);

        if (db.sendFriendRequest(myId, x["target_id"].i()))
            return crow::response(200, "Istek gonderildi");
        return crow::response(400, "Istek gonderilemedi (Zaten arkadas veya istek var)");
            });

    CROW_ROUTE(app, "/api/friends/request/<int>").methods("PUT"_method)
        ([&db](const crow::request& req, int requesterId) {
        int myId = 1;
        if (db.acceptFriendRequest(requesterId, myId))
            return crow::response(200, "Kabul edildi");
        return crow::response(400, "Hata");
            });

    // =============================================================
    // 4. SUNUCU & KANAL YÖNETİMİ
    // =============================================================

    // SUNUCULARIM
    CROW_ROUTE(app, "/api/servers")
        ([&db]() {
        int myId = 1;
        auto servers = db.getUserServers(myId);

        crow::json::wvalue res;
        for (size_t i = 0; i < servers.size(); i++) {
            res[i]["id"] = servers[i].id;
            res[i]["name"] = servers[i].name;
            res[i]["icon_url"] = servers[i].icon_url;
            res[i]["created_at"] = servers[i].created_at;
            res[i]["member_count"] = servers[i].member_count;
        }
        return crow::response(200, res);
            });

    // YENİ SUNUCU (Limit Kontrollü)
    CROW_ROUTE(app, "/api/servers").methods("POST"_method)
        ([&db](const crow::request& req) {
        int myId = 1;
        auto x = crow::json::load(req.body);
        if (!x || !x.has("name")) return crow::response(400);

        int srvId = db.createServer(x["name"].s(), myId);
        if (srvId > 0) {
            crow::json::wvalue res;
            res["id"] = srvId;
            res["message"] = "Sunucu olusturuldu";
            return crow::response(201, res);
        }
        return crow::response(403, "Sunucu olusturulamadi (Limit asimi olabilir)");
            });

    // SUNUCU KANALLARI
    CROW_ROUTE(app, "/api/servers/<int>/channels")
        ([&db](int serverId) {
        auto channels = db.getServerChannels(serverId);
        crow::json::wvalue res;
        for (size_t i = 0; i < channels.size(); i++) {
            res[i]["id"] = channels[i].id;
            res[i]["name"] = channels[i].name;
            res[i]["type"] = channels[i].type;
        }
        return crow::response(200, res);
            });

    // YENİ KANAL (TodoList Limiti Var)
    CROW_ROUTE(app, "/api/servers/<int>/channels").methods("POST"_method)
        ([&db](const crow::request& req, int serverId) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("name") || !x.has("type")) return crow::response(400);

        if (db.createChannel(serverId, x["name"].s(), x["type"].i()))
            return crow::response(201, "Kanal olusturuldu");
        return crow::response(403, "Kanal olusturulamadi (TodoList limiti olabilir)");
            });

    // =============================================================
    // 5. MESAJLAŞMA
    // =============================================================

    CROW_ROUTE(app, "/api/channels/<int>/messages")
        ([&db](int channelId) {
        auto msgs = db.getChannelMessages(channelId, 50);
        crow::json::wvalue res;
        for (size_t i = 0; i < msgs.size(); i++) {
            res[i]["id"] = msgs[i].id;
            res[i]["sender_name"] = msgs[i].sender_name;
            res[i]["sender_avatar"] = msgs[i].sender_avatar;
            res[i]["content"] = msgs[i].content;
            res[i]["attachment_url"] = msgs[i].attachment_url;
            res[i]["timestamp"] = msgs[i].timestamp;
        }
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/channels/<int>/messages").methods("POST"_method)
        ([&db](const crow::request& req, int channelId) {
        int myId = 1;
        auto x = crow::json::load(req.body);
        if (!x || !x.has("content")) return crow::response(400);

        std::string attach = "";
        if (x.has("attachment_url")) attach = x["attachment_url"].s();

        if (db.sendMessage(channelId, myId, x["content"].s(), attach))
            return crow::response(200, "Gonderildi");
        return crow::response(500, "Hata");
            });

    // =============================================================
    // 6. KANBAN (TRELLO) PROJE YÖNETİMİ
    // =============================================================

    // PANOYU GETİR
    CROW_ROUTE(app, "/api/boards/<int>")
        ([&db](int channelId) {
        auto board = db.getKanbanBoard(channelId);

        crow::json::wvalue res;
        for (size_t i = 0; i < board.size(); i++) {
            res[i]["id"] = board[i].id;
            res[i]["title"] = board[i].title;
            res[i]["position"] = board[i].position;

            for (size_t j = 0; j < board[i].cards.size(); j++) {
                res[i]["cards"][j]["id"] = board[i].cards[j].id;
                res[i]["cards"][j]["title"] = board[i].cards[j].title;
                res[i]["cards"][j]["description"] = board[i].cards[j].description;
                res[i]["cards"][j]["priority"] = board[i].cards[j].priority;
                res[i]["cards"][j]["position"] = board[i].cards[j].position;
            }
        }
        return crow::response(200, res);
            });

    // YENİ LİSTE EKLE
    CROW_ROUTE(app, "/api/boards/<int>/lists").methods("POST"_method)
        ([&db](const crow::request& req, int channelId) {
        auto x = crow::json::load(req.body);
        if (db.createKanbanList(channelId, x["title"].s()))
            return crow::response(201, "Liste eklendi");
        return crow::response(500);
            });

    // YENİ KART EKLE
    CROW_ROUTE(app, "/api/lists/<int>/cards").methods("POST"_method)
        ([&db](const crow::request& req, int listId) {
        auto x = crow::json::load(req.body);
        if (db.createKanbanCard(listId, x["title"].s(), x["description"].s(), x["priority"].i()))
            return crow::response(201, "Kart eklendi");
        return crow::response(500);
            });

    // KART TAŞIMA (Sürükle-Bırak)
    CROW_ROUTE(app, "/api/cards/<int>/move").methods("PUT"_method)
        ([&db](const crow::request& req, int cardId) {
        auto x = crow::json::load(req.body);
        if (db.moveCard(cardId, x["new_list_id"].i(), x["new_position"].i()))
            return crow::response(200, "Tasindi");
        return crow::response(500);
            });

    // Sunucuyu Başlat
    std::cout << "MySaaSApp API baslatiliyor: http://localhost:8080\n";
    app.port(8080).multithreaded().run();
    return 0;
}