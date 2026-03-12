#include "WsRoutes.h"
#include "../utils/Security.h"
#include "../utils/FileManager.h"
#include "../utils/LogManager.h" // Yedekleme Motoru Eklendi
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <iostream>
#include <chrono>

// ==========================================================
// WEBSOCKET GLOBAL DEĞİŞKENLERİ (BELLEK İÇİ YÖNETİM)
// ==========================================================

std::mutex video_call_mtx;
std::unordered_map<std::string, crow::websocket::connection*> active_video_users;
std::unordered_map<crow::websocket::connection*, std::string> conn_to_video_user;
std::unordered_map<std::string, std::unordered_set<std::string>> video_rooms;

std::mutex chat_mtx;
std::unordered_map<crow::websocket::connection*, std::string> chat_user_channels;
std::unordered_map<std::string, crow::websocket::connection*> online_chat_users;

void WsRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

    // ==========================================================
    // 1. GÖRÜNTÜLÜ VE SESLİ ARAMA SİNYALİZASYONU
    // ==========================================================
    CROW_WEBSOCKET_ROUTE(app, "/ws/video-call")
        .onopen([&](crow::websocket::connection& conn) {
        CROW_LOG_INFO << "WebSocket: Video/Ses baglantisi acildi.";
            })
        .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
        try {
            auto msg = crow::json::load(data);
            if (!msg || !msg.has("type")) return;

            std::lock_guard<std::mutex> lock(video_call_mtx);
            std::string type = std::string(msg["type"].s());

            if (type == "register" && msg.has("user_id")) {
                active_video_users[std::string(msg["user_id"].s())] = &conn;
                conn_to_video_user[&conn] = std::string(msg["user_id"].s());
            }
            else if (type == "call-request" && msg.has("target_id")) {
                std::string tId = std::string(msg["target_id"].s());
                if (active_video_users.count(tId) && active_video_users[tId]) {
                    active_video_users[tId]->send_text(data);
                }
            }
            else if (type == "join-room" && msg.has("channel_id")) {
                std::string uId = conn_to_video_user[&conn];
                std::string cId = std::string(msg["channel_id"].s());
                video_rooms[cId].insert(uId);

                crow::json::wvalue alert;
                alert["type"] = "peer-joined";
                alert["user_id"] = uId;
                for (const auto& pId : video_rooms[cId]) {
                    if (pId != uId && active_video_users.count(pId))
                        active_video_users[pId]->send_text(alert.dump());
                }
            }
            else if (msg.has("target_id")) {
                std::string tId = std::string(msg["target_id"].s());
                if (active_video_users.count(tId) && active_video_users[tId]) {
                    active_video_users[tId]->send_text(data);
                }
            }
        }
        catch (...) { CROW_LOG_ERROR << "Video WS hatasi."; }
            })
        // DİKKAT: Crow için zorunlu olan 3 parametreli onclose (uint16_t code eklendi)
        .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t code) {
        try {
            std::lock_guard<std::mutex> lock(video_call_mtx);
            if (conn_to_video_user.count(&conn)) {
                std::string uId = conn_to_video_user[&conn];
                active_video_users.erase(uId);
                conn_to_video_user.erase(&conn);
            }
        }
        catch (...) {}
            });

    // ==========================================================
    // 2. YAZILI SOHBET (LOGMANAGER ENTEGRELİ + TÜRKÇE JSON)
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

            if (type == "subscribe" && msg.has("channel_id") && msg.has("user_id")) {
                chat_user_channels[&conn] = std::string(msg["channel_id"].s());
                online_chat_users[std::string(msg["user_id"].s())] = &conn;
                return;
            }
            else if (type == "message" && msg.has("sender_id") && msg.has("target_id") && msg.has("text")) {

                // Tip uyuşmazlığını önlemek için explicit std::string dönüşümleri yapıldı (C2446 Hatası Çözümü)
                std::string sId = std::string(msg["sender_id"].s());
                std::string tId = std::string(msg["target_id"].s());
                std::string txt = std::string(msg["text"].s());
                std::string contentType = msg.has("content_type") ? std::string(msg["content_type"].s()) : std::string("Text");

                bool isGroup = false;
                if (msg.has("is_group")) isGroup = msg["is_group"].b();
                else if (msg.has("is_server")) isGroup = msg["is_server"].b();

                std::string groupId = msg.has("group_id") ? std::string(msg["group_id"].s()) : std::string("default_group");

                // 1. Şifrele ve Veritabanı/Dosyaya Yaz
                std::string encrypted = Security::encryptMessage(txt);
                bool saved = false;

                if (isGroup) {
                    saved = FileManager::saveGroupMessage(groupId, tId, sId, encrypted, contentType);
                }
                else {
                    saved = FileManager::savePrivateMessage(sId, tId, encrypted, contentType);
                }

                if (saved) {
                    // 2. YENİ EKLENTİ: 10 Saniyelik TXT Log Kuyruğuna At!
                    LogManager::addToQueue(sId, tId, encrypted);

                    // 3. Anlık Yayın (Frontend ile uyumlu Türkçe JSON formatı)
                    crow::json::wvalue outMsg;
                    outMsg["type"] = "new_message";
                    outMsg["gönderenID"] = sId;
                    outMsg["alıcıID"] = tId;
                    outMsg["Mesaj"] = txt;
                    outMsg["MesajİçerikDurumu"] = contentType;
                    outMsg["is_group"] = isGroup;

                    auto now = std::chrono::system_clock::now();
                    outMsg["MesajGönderimTarihi"] = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());

                    for (auto const& [peer_conn, cId] : chat_user_channels) {
                        if (peer_conn == nullptr) continue; // Ölü bağlantı koruması

                        if (cId == tId || (!isGroup && cId == sId)) {
                            try { peer_conn->send_text(outMsg.dump()); }
                            catch (...) {}
                        }
                    }
                }
            }
        }
        catch (...) { CROW_LOG_ERROR << "Chat WS JSON Parse Hatasi."; }
            })
        // DİKKAT: Crow için zorunlu olan 3 parametreli onclose (uint16_t code eklendi)
        .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t code) {
        try {
            std::lock_guard<std::mutex> lock(chat_mtx);
            chat_user_channels.erase(&conn);
            for (auto it = online_chat_users.begin(); it != online_chat_users.end(); ) {
                if (it->second == &conn) it = online_chat_users.erase(it);
                else ++it;
            }
        }
        catch (...) {}
            });

    // ==========================================================
    // 3. YARDIMCI ROTLAR
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
        // DİKKAT: 3 Parametreli onclose
        .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t code) {});
}