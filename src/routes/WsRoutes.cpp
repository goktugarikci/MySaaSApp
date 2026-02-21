#include "crow.h"
#include "../db/DatabaseManager.h"
#include "../utils/Security.h"
#include <unordered_map>
#include <mutex>

// --- GLOBAL DEĞİŞKENLER (Bu dosya özelinde) ---
// WebRTC Sinyalleşmesi için aktif bağlantıları tutar.
// Key: UserID, Value: WebSocket Bağlantısı
std::unordered_map<std::string, crow::websocket::connection*> active_video_calls;
std::mutex video_call_mtx;

void setupWsRoutes(crow::SimpleApp& app, DatabaseManager& db) {

    // =============================================================
    // 8. GÖRÜNTÜLÜ KONUŞMA (WebRTC Signaling)
    // =============================================================
    CROW_WEBSOCKET_ROUTE(app, "/ws/video-call")
        .onopen([&](crow::websocket::connection& conn) {
        CROW_LOG_INFO << "Yeni WebSocket baglantisi (Video Call) baslatildi.";
            })
        .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
        try {
            auto msg = crow::json::load(data);

            // Mesajın geçerliliğini kontrol et
            if (!msg || !msg.has("type")) return;

            std::string type = msg["type"].s();

            // 1. KULLANICI KAYDI (Register)
            // Kullanıcı sayfayı açtığında ID'si ile sokete kaydolur.
            if (type == "register" && msg.has("user_id")) {
                std::string userId = msg["user_id"].s();

                std::lock_guard<std::mutex> lock(video_call_mtx);
                active_video_calls[userId] = &conn;

                CROW_LOG_INFO << "Kullanici video servisine kaydoldu: " << userId;
            }

            // 2. SİNYAL İLETİMİ (Offer / Answer / ICE Candidate)
            // Arayan kişiden gelen veriyi (SDP/ICE), hedef kişiye (target_id) iletir.
            else if ((type == "offer" || type == "answer" || type == "ice_candidate") && msg.has("target_id")) {
                std::string targetId = msg["target_id"].s();

                std::lock_guard<std::mutex> lock(video_call_mtx);
                auto it = active_video_calls.find(targetId);

                // Eğer hedef kullanıcı çevrimiçiyse mesajı ona gönder
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

        // Bağlantısı kopan kullanıcıyı haritadan (map) temizle
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
}