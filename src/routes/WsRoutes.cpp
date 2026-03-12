#include "WsRoutes.h"
#include "../utils/Security.h"
#include "../utils/FileManager.h"
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <iostream>
#include <chrono>

// ==========================================================
// WEBSOCKET GLOBAL DEĞİŞKENLERİ (BELLEK İÇİ YÖNETİM)
// ==========================================================

// --- VİDEO VE SESLİ ARAMA ---
std::mutex video_call_mtx;
std::unordered_map<std::string, crow::websocket::connection*> active_video_users; // userId -> connection
std::unordered_map<crow::websocket::connection*, std::string> conn_to_video_user; // connection -> userId
std::unordered_map<std::string, std::unordered_set<std::string>> video_rooms;     // channelId -> [userId1, userId2...]

// --- YAZILI SOHBET ---
std::mutex chat_mtx;
// connection -> active_context_id (Kanal ID veya Karşı Kullanıcı ID)
std::unordered_map<crow::websocket::connection*, std::string> chat_user_channels;
// user_id -> connection (Doğrudan bildirimler için)
std::unordered_map<std::string, crow::websocket::connection*> online_chat_users;


void WsRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // ==========================================================
    // 1. GÖRÜNTÜLÜ VE SESLİ ARAMA SİNYALİZASYONU (ODA VE DM)
    // ==========================================================
    CROW_WEBSOCKET_ROUTE(app, "/ws/video-call")
        .onopen([&](crow::websocket::connection& conn) {
        CROW_LOG_INFO << "Yeni WebSocket baglantisi (Video/Ses) baslatildi.";
            })
        .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
        try {
            auto msg = crow::json::load(data);
            if (!msg || !msg.has("type")) return;

            std::string type = std::string(msg["type"].s());
            std::lock_guard<std::mutex> lock(video_call_mtx);

            // A) KULLANICIYI SİSTEME KAYDET
            if (type == "register" && msg.has("user_id")) {
                std::string userId = std::string(msg["user_id"].s());
                active_video_users[userId] = &conn;
                conn_to_video_user[&conn] = userId;
            }

            // B) BİREBİR (DM) KİŞİSEL ARAMA SİNYALLERİ (Ringing)
            else if (type == "call-request" && msg.has("caller_id") && msg.has("target_id")) {
                std::string targetId = std::string(msg["target_id"].s());
                if (active_video_users.count(targetId)) {
                    crow::json::wvalue out;
                    out["type"] = "incoming-call";
                    out["caller_id"] = std::string(msg["caller_id"].s());
                    if (msg.has("offer")) out["offer"] = msg["offer"];
                    active_video_users[targetId]->send_text(out.dump());
                }
            }

            // C) SUNUCU SESLİ ODASINA KATILMA
            else if (type == "join-room" && msg.has("user_id") && msg.has("channel_id")) {
                std::string userId = std::string(msg["user_id"].s());
                std::string channelId = std::string(msg["channel_id"].s());

                active_video_users[userId] = &conn;
                conn_to_video_user[&conn] = userId;
                video_rooms[channelId].insert(userId);

                crow::json::wvalue alertMsg;
                alertMsg["type"] = "peer-joined";
                alertMsg["user_id"] = userId;

                for (const auto& peerId : video_rooms[channelId]) {
                    if (peerId != userId && active_video_users.count(peerId)) {
                        active_video_users[peerId]->send_text(alertMsg.dump());
                    }
                }
            }

            // D) WEBRTC SİNYALİZASYON (SDP/ICE)
            else if ((type == "offer" || type == "answer" || type == "ice_candidate") && msg.has("target_id")) {
                std::string targetId = std::string(msg["target_id"].s());
                if (active_video_users.count(targetId)) {
                    crow::json::wvalue outMsg = msg;
                    if (conn_to_video_user.count(&conn)) {
                        outMsg["from_id"] = conn_to_video_user[&conn];
                    }
                    active_video_users[targetId]->send_text(outMsg.dump());
                }
            }
        }
        catch (...) { CROW_LOG_ERROR << "Video WS JSON Parse Hatasi."; }
            })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t code) {
        std::lock_guard<std::mutex> lock(video_call_mtx);
        if (conn_to_video_user.count(&conn)) {
            std::string userId = conn_to_video_user[&conn];
            for (auto& room : video_rooms) {
                if (room.second.count(userId)) {
                    room.second.erase(userId);
                    crow::json::wvalue disconnectMsg;
                    disconnectMsg["type"] = "peer-disconnected";
                    disconnectMsg["user_id"] = userId;
                    for (const auto& peerId : room.second) {
                        if (active_video_users.count(peerId)) active_video_users[peerId]->send_text(disconnectMsg.dump());
                    }
                }
            }
            active_video_users.erase(userId);
            conn_to_video_user.erase(&conn);
        }
            });


    // ==========================================================
    // 2. YAZILI SOHBET (DM VE GRUPLAR) - YENİ JSON MİMARİSİ
    // ==========================================================
    CROW_WEBSOCKET_ROUTE(app, "/ws/chat")
        .onopen([&](crow::websocket::connection& conn) {
        CROW_LOG_INFO << "WebSocket: Sohbet baglantisi acildi.";
            })
        .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
        try {
            auto msg = crow::json::load(data);
            if (!msg || !msg.has("type")) return;

            std::lock_guard<std::mutex> lock(chat_mtx);
            std::string type = std::string(msg["type"].s());

            // A) ABONE OLMA (Kanal veya DM Odasına Giriş)
            if (type == "subscribe" && msg.has("channel_id") && msg.has("user_id")) {
                chat_user_channels[&conn] = std::string(msg["channel_id"].s());
                online_chat_users[std::string(msg["user_id"].s())] = &conn;
                return;
            }

            // B) MESAJ GÖNDERME (Yeni JSON Formatı ile FileManager'a Yazma ve Yayınlama)
            else if (type == "message" && msg.has("sender_id") && msg.has("target_id") && msg.has("text")) {
                // C++ Cast işlemleri ile derleyici hatası önlendi
                std::string sId = std::string(msg["sender_id"].s());
                std::string tId = std::string(msg["target_id"].s()); // Grup ise Kanal ID, DM ise Karşı Kullanıcı ID
                std::string txt = std::string(msg["text"].s());

                // Ek içerik türü kontrolü (Dosya, Video, Fotoğraf veya Text)
                std::string contentType = msg.has("content_type") ? std::string(msg["content_type"].s()) : std::string("Text");

                // Eski 'is_server' yerine 'is_group' mantığı
                bool isGroup = false;
                if (msg.has("is_group")) isGroup = msg["is_group"].b();
                else if (msg.has("is_server")) isGroup = msg["is_server"].b();

                // Grup ID'si güvenli atama
                std::string groupId = msg.has("group_id") ? std::string(msg["group_id"].s()) : std::string("default_group");

                // 1. Şifrele ve Dosyaya Yaz
                std::string encrypted = Security::encryptMessage(txt);
                bool saved = false;

                if (isGroup) {
                    saved = FileManager::saveGroupMessage(groupId, tId, sId, encrypted, contentType);
                }
                else {
                    saved = FileManager::savePrivateMessage(sId, tId, encrypted, contentType);
                }

                if (saved) {
                    // 2. Anlık Yayın (Broadcast) - Yeni Şema Formatı
                    crow::json::wvalue outMsg;
                    outMsg["type"] = "new_message";
                    outMsg["gönderenID"] = sId;
                    outMsg["alıcıID"] = tId;
                    outMsg["Mesaj"] = txt; // Soket üzerinden açık gidip arayüzde renderlanması için
                    outMsg["MesajİçerikDurumu"] = contentType;
                    outMsg["is_group"] = isGroup;

                    auto now = std::chrono::system_clock::now();
                    outMsg["MesajGönderimTarihi"] = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());

                    for (auto const& [peer_conn, cId] : chat_user_channels) {
                        // Mesajın gitmesi gereken yer: Ya aynı kanal ID'si ya da DM ise karşı tarafın aktif bağlantısı
                        if (cId == tId || (!isGroup && cId == sId)) {
                            peer_conn->send_text(outMsg.dump());
                        }
                    }
                }
            }

            // C) YAZIYOR... (TYPING) SİNYALİ
            else if (type == "typing" && msg.has("channel_id") && msg.has("sender_id")) {
                std::string cId = std::string(msg["channel_id"].s());
                crow::json::wvalue outMsg;
                outMsg["type"] = "typing";
                outMsg["sender_id"] = std::string(msg["sender_id"].s());

                for (auto const& [peer_conn, room] : chat_user_channels) {
                    if (peer_conn != &conn && room == cId) {
                        peer_conn->send_text(outMsg.dump());
                    }
                }
            }
        }
        catch (...) { CROW_LOG_ERROR << "Chat WS JSON Parse Hatasi."; }
            })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t code) {
        std::lock_guard<std::mutex> lock(chat_mtx);
        chat_user_channels.erase(&conn);
        for (auto it = online_chat_users.begin(); it != online_chat_users.end(); ) {
            if (it->second == &conn) it = online_chat_users.erase(it);
            else ++it;
        }
            });

    // ==========================================================
    // 3. WEBRTC VE BİLDİRİM DESTEKLEYİCİLER
    // ==========================================================
    CROW_ROUTE(app, "/api/webrtc/ice-servers").methods("GET"_method)
        ([&db](const crow::request& req) {
        if (!Security::checkAuth(req, db)) return crow::response(401);
        crow::json::wvalue res;
        res["iceServers"][0]["urls"][0] = "stun:stun.l.google.com:19302";
        return crow::response(200, res);
            });

    CROW_WEBSOCKET_ROUTE(app, "/ws/notifications")
        .onopen([&](crow::websocket::connection& conn) { CROW_LOG_INFO << "Bildirim Soketi Acildi"; })
        .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {})
        .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t code) {});
}