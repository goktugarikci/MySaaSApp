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
}