#include "WsRoutes.h"
#include "../utils/Security.h"
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <iostream>

// --- VİDEO VE SESLİ ODA (ROOM) İÇİN DEĞİŞKENLER ---
std::mutex video_call_mtx;
std::unordered_map<std::string, crow::websocket::connection*> active_video_users; // userId -> connection
std::unordered_map<crow::websocket::connection*, std::string> conn_to_video_user; // connection -> userId
std::unordered_map<std::string, std::unordered_set<std::string>> video_rooms;     // channelId -> [userId1, userId2...]

// --- YAZILI SOHBET (ROOM) İÇİN DEĞİŞKENLER ---
std::mutex chat_mtx;
std::unordered_map<crow::websocket::connection*, std::string> chat_user_channels; // connection -> channelId

void WsRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {

    // ==========================================================
    // 1. GÖRÜNTÜLÜ VE SESLİ ARAMA SİNYALİZASYONU (ODA DESTEKLİ)
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
            std::lock_guard<std::mutex> lock(video_call_mtx);

            // Odaya Katılma (Discord Sesli Kanala Giriş)
            if (type == "join-room" && msg.has("user_id") && msg.has("channel_id")) {
                std::string userId = msg["user_id"].s();
                std::string channelId = msg["channel_id"].s();

                active_video_users[userId] = &conn;
                conn_to_video_user[&conn] = userId;
                video_rooms[channelId].insert(userId);

                CROW_LOG_INFO << userId << " kullanicisi " << channelId << " sesli odasina katildi.";

                // Odadaki diğer kişilere "Yeni biri geldi, ona WebRTC teklifi atın" uyarısı gönder
                crow::json::wvalue alertMsg;
                alertMsg["type"] = "peer-joined";
                alertMsg["user_id"] = userId;

                for (const auto& peerId : video_rooms[channelId]) {
                    if (peerId != userId && active_video_users.count(peerId)) {
                        active_video_users[peerId]->send_text(alertMsg.dump());
                    }
                }
            }
            // WebRTC Sinyallerini (Kamera IP/Port bilgileri) sadece hedefe yolla
            else if ((type == "offer" || type == "answer" || type == "ice_candidate") && msg.has("target_id")) {
                std::string targetId = msg["target_id"].s();
                auto it = active_video_users.find(targetId);

                if (it != active_video_users.end() && it->second != nullptr) {
                    // Mesajı kimin gönderdiğini ekleyerek hedefe ilet
                    crow::json::wvalue outMsg = msg;
                    if (conn_to_video_user.count(&conn)) {
                        outMsg["from_id"] = conn_to_video_user[&conn];
                    }
                    it->second->send_text(outMsg.dump());
                }
            }
        }
        catch (const std::exception& e) {
            CROW_LOG_ERROR << "Video WebSocket hatasi: " << e.what();
        }
            })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t code) {
        std::lock_guard<std::mutex> lock(video_call_mtx);
        if (conn_to_video_user.count(&conn)) {
            std::string userId = conn_to_video_user[&conn];

            // Kullanıcıyı bulunduğu odadan çıkar ve odadakilere haber ver
            for (auto& room : video_rooms) {
                if (room.second.count(userId)) {
                    room.second.erase(userId);

                    crow::json::wvalue disconnectMsg;
                    disconnectMsg["type"] = "peer-disconnected";
                    disconnectMsg["user_id"] = userId;

                    for (const auto& peerId : room.second) {
                        if (active_video_users.count(peerId)) {
                            active_video_users[peerId]->send_text(disconnectMsg.dump());
                        }
                    }
                    break;
                }
            }
            active_video_users.erase(userId);
            conn_to_video_user.erase(&conn);
            CROW_LOG_INFO << "Kullanici video servisinden ayrildi: " << userId;
        }
            });

    // ==========================================================
    // 2. GENEL MESAJLAŞMA (İZOLE EDİLMİŞ KANALLAR)
    // ==========================================================
    CROW_WEBSOCKET_ROUTE(app, "/ws/chat")
        .onopen([&](crow::websocket::connection& conn) {
        CROW_LOG_INFO << "Yeni WebSocket baglantisi (Chat) baslatildi.";
            })
        .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
        try {
            auto msg = crow::json::load(data);
            if (!msg) return;

            std::lock_guard<std::mutex> lock(chat_mtx);

            // Kullanıcı "Ben şu kanala bağlandım" derse kaydet
            if (msg.has("type") && msg["type"].s() == "subscribe" && msg.has("channel_id")) {
                chat_user_channels[&conn] = msg["channel_id"].s();
                return;
            }

            // Gelen bir mesajı SADECE aynı kanaldaki (channel_id) kişilere gönder
            if (chat_user_channels.count(&conn)) {
                std::string myChannel = chat_user_channels[&conn];

                for (auto const& [peer_conn, channel] : chat_user_channels) {
                    if (peer_conn != &conn && channel == myChannel) {
                        peer_conn->send_text(data);
                    }
                }
            }
        }
        catch (...) {}
            })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t code) {
        std::lock_guard<std::mutex> lock(chat_mtx);
        chat_user_channels.erase(&conn);
            });

    // ==========================================================
    // 3. WEBRTC REST API'LERİ (Değiştirilmedi, Mükemmel Çalışıyor)
    // ==========================================================
    CROW_ROUTE(app, "/api/webrtc/ice-servers").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        crow::json::wvalue res;
        res["iceServers"][0]["urls"][0] = "stun:stun.l.google.com:19302";
        res["iceServers"][0]["urls"][1] = "stun:stun1.l.google.com:19302";
        return crow::response(200, res);
            });

    CROW_ROUTE(app, "/api/webrtc/metrics").methods("POST"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        auto x = crow::json::load(req.body);
        if (!x || !x.has("channel_id") || !x.has("latency")) return crow::response(400);

        std::string myId = Security::getUserIdFromHeader(req);
        int latency = x["latency"].i();
        float packetLoss = x.has("packet_loss") ? x["packet_loss"].d() : 0.0;
        std::string res = x.has("resolution") ? std::string(x["resolution"].s()) : "720p";

        if (db.logCallQuality(myId, std::string(x["channel_id"].s()), latency, packetLoss, res)) {
            crow::json::wvalue responseJson;
            if (latency > 300 || packetLoss > 5.0) responseJson["suggestion"] = "DOWNGRADE_RESOLUTION";
            else responseJson["suggestion"] = "KEEP_CURRENT";
            return crow::response(200, responseJson);
        }
        return crow::response(500);
            });
}