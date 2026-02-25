#include "WsRoutes.h"
#include "../utils/Security.h"
#include <unordered_map>
#include <unordered_set>
#include <mutex>

// --- VİDEO ARAMA İÇİN DEĞİŞKENLER ---
std::unordered_map<std::string, crow::websocket::connection*> active_video_calls;
std::mutex video_call_mtx;

// --- GENEL SOHBET & BİLDİRİM İÇİN DEĞİŞKENLER (YENİ) ---
std::unordered_set<crow::websocket::connection*> active_chat_users;
std::mutex chat_mtx;

void WsRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // ==========================================================
    // 1. GÖRÜNTÜLÜ VE SESLİ ARAMA SİNYALİZASYONU
    // ==========================================================
    CROW_WEBSOCKET_ROUTE(app, "/ws/video-call")
        .onopen([&](crow::websocket::connection& conn) {
        CROW_LOG_INFO << "Yeni WebSocket baglantisi (Video Call) baslatildi.";
            })
        .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
        try {
            auto msg = crow::json::load(data);
            if (!msg || !msg.has("type")) return;

            std::string type = msg["type"].s();

            if (type == "register" && msg.has("user_id")) {
                std::string userId = msg["user_id"].s();
                std::lock_guard<std::mutex> lock(video_call_mtx);
                active_video_calls[userId] = &conn;
                CROW_LOG_INFO << "Kullanici video servisine kaydoldu: " << userId;
            }
            else if ((type == "offer" || type == "answer" || type == "ice_candidate") && msg.has("target_id")) {
                std::string targetId = msg["target_id"].s();
                std::lock_guard<std::mutex> lock(video_call_mtx);
                auto it = active_video_calls.find(targetId);

                if (it != active_video_calls.end() && it->second != nullptr) {
                    it->second->send_text(data);
                    CROW_LOG_INFO << "Sinyal iletildi (" << type << ") -> " << targetId;
                }
                else {
                    CROW_LOG_WARNING << "Hedef kullanici bulunamadi veya cevrimdisi: " << targetId;
                }
            }
        }
        catch (const std::exception& e) {
            CROW_LOG_ERROR << "WebSocket mesaj isleme hatasi: " << e.what();
        }
            })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t code) {
        std::lock_guard<std::mutex> lock(video_call_mtx);
        for (auto it = active_video_calls.begin(); it != active_video_calls.end(); ) {
            if (it->second == &conn) {
                CROW_LOG_INFO << "Kullanici video servisinden ayrildi: " << it->first;
                it = active_video_calls.erase(it);
            }
            else {
                ++it;
            }
        }
            });

    // ==========================================================
    // 2. GENEL MESAJLAŞMA VE SİSTEM BİLDİRİMLERİ (YENİ EKLENDİ)
    // ==========================================================
    CROW_WEBSOCKET_ROUTE(app, "/ws/chat")
        .onopen([&](crow::websocket::connection& conn) {
        std::lock_guard<std::mutex> lock(chat_mtx);
        active_chat_users.insert(&conn);
        CROW_LOG_INFO << "Yeni WebSocket baglantisi (Genel Chat) baslatildi.";

        // Kullanıcı bağlandığında karşılama mesajı gönder
        conn.send_text("{\"type\": \"system\", \"message\": \"Gercek zamanli chat sunucusuna baglandiniz.\"}");
            })
        .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
        // Gelen mesajları, Typing (Yazıyor...) bildirimlerini veya Kanban güncellemelerini
        // o an bağlı olan DİĞER tüm kullanıcılara canlı olarak yayınla (Broadcast)
        std::lock_guard<std::mutex> lock(chat_mtx);
        for (auto u : active_chat_users) {
            if (u != &conn) {
                u->send_text(data);
            }
        }
            })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t code) {
        std::lock_guard<std::mutex> lock(chat_mtx);
        active_chat_users.erase(&conn);
        CROW_LOG_INFO << "Kullanici chat servisinden ayrildi.";
            });

    // ==========================================================
    // WEBRTC OPTİMİZASYONLARI (STUN/TURN VE QoS)
    // ==========================================================

    // 1. DİNAMİK STUN/TURN SUNUCU BİLGİLERİ DAĞITIMI
    // Frontend WebRTC bağlantısını başlatmadan önce bu endpoint'ten sunucu listesini almalıdır.
    CROW_ROUTE(app, "/api/webrtc/ice-servers").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);

        crow::json::wvalue res;
        // P2P IP bulucu standart ücretsiz Google sunucuları (STUN)
        res["iceServers"][0]["urls"][0] = "stun:stun.l.google.com:19302";
        res["iceServers"][0]["urls"][1] = "stun:stun1.l.google.com:19302";

        // Gelişmiş sistemlerde buraya şifreli TURN sunucuları (Görüntü aktarma röleleri) eklenir.
        // Örn: Twilio Network Traversal Service API'si ile üretilen anlık şifreler.
        /*
        res["iceServers"][1]["urls"][0] = "turn:turn.mysaas.com:3478";
        res["iceServers"][1]["username"] = "temp_user_xyz";
        res["iceServers"][1]["credential"] = "temp_pass_123";
        */

        return crow::response(200, res);
            });

    // 2. KULLANICI AĞ KALİTESİ (QoS) BİLDİRİMİ
    // Frontend'in her 30 saniyede bir ping ve paket kaybı durumunu backend'e bildirmesi içindir.
    CROW_ROUTE(app, "/api/webrtc/metrics").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("channel_id") || !x.has("latency")) return crow::response(400);

        std::string myId = Security::getUserIdFromHeader(req);
        int latency = x["latency"].i(); // milisaniye cinsinden ping
        float packetLoss = x.has("packet_loss") ? x["packet_loss"].d() : 0.0;
        std::string res = x.has("resolution") ? std::string(x["resolution"].s()) : "720p";

        if (db.logCallQuality(myId, std::string(x["channel_id"].s()), latency, packetLoss, res)) {
            // Eğer paket kaybı veya ping çok yüksekse (Örn: ping > 500ms), 
            // sunucu Frontend'e "Kaliteyi Düşür (Video çözünürlüğünü azalt)" uyarısı fırlatabilir.
            crow::json::wvalue responseJson;
            if (latency > 300 || packetLoss > 5.0) {
                responseJson["suggestion"] = "DOWNGRADE_RESOLUTION";
            }
            else {
                responseJson["suggestion"] = "KEEP_CURRENT";
            }
            return crow::response(200, responseJson);
        }
        return crow::response(500);
            });

};