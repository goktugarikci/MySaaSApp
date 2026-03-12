#include "WsRoutes.h"
#include "../utils/Security.h"
#include "../utils/FileManager.h"
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <iostream>
#include <chrono>

// --- VİDEO VE SESLİ ODA (ROOM) İÇİN DEĞİŞKENLER ---
std::mutex video_call_mtx;
std::unordered_map<std::string, crow::websocket::connection*> active_video_users; // userId -> connection
std::unordered_map<crow::websocket::connection*, std::string> conn_to_video_user; // connection -> userId
std::unordered_map<std::string, std::unordered_set<std::string>> video_rooms;     // channelId -> [userId1, userId2...]

// --- YAZILI SOHBET (ROOM) İÇİN DEĞİŞKENLER (YENİ MİMARİ) ---
std::mutex chat_mtx;
// ARTIK KANALLAR YOK. Her kullanıcının kendi sabit bir odası (borusu) var.
std::unordered_map<std::string, crow::websocket::connection*> online_users; // userId -> connection

void WsRoutes::setup(crow::App<crow::CORSHandler>& app, DatabaseManager& db) {

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
                std::lock_guard<std::mutex> lock(video_call_mtx);

                if (type == "join-room" && msg.has("user_id") && msg.has("channel_id")) {
                    std::string userId = msg["user_id"].s();
                    std::string channelId = msg["channel_id"].s();

                    active_video_users[userId] = &conn;
                    conn_to_video_user[&conn] = userId;
                    video_rooms[channelId].insert(userId);

                    CROW_LOG_INFO << userId << " kullanicisi " << channelId << " sesli odasina katildi.";

                    crow::json::wvalue alertMsg;
                    alertMsg["type"] = "peer-joined";
                    alertMsg["user_id"] = userId;

                    for (const auto& peerId : video_rooms[channelId]) {
                        if (peerId != userId && active_video_users.count(peerId)) {
                            active_video_users[peerId]->send_text(alertMsg.dump());
                        }
                    }
                }
                else if ((type == "offer" || type == "answer" || type == "ice_candidate") && msg.has("target_id")) {
                    std::string targetId = msg["target_id"].s();
                    auto it = active_video_users.find(targetId);

                    if (it != active_video_users.end() && it->second != nullptr) {
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
    // 2. GENEL MESAJLAŞMA (JSON LOG VE GÜVENLİ TOKEN MİMARİSİ)
    // ==========================================================
    CROW_WEBSOCKET_ROUTE(app, "/ws/chat")
        .onopen([&](crow::websocket::connection& conn) {
            CROW_LOG_INFO << "Yeni WebSocket baglantisi (Chat) baslatildi.";
        })
        .onmessage([&db](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
            try {
                auto msg = crow::json::load(data);
                if (!msg || !msg.has("type")) return;

                std::lock_guard<std::mutex> lock(chat_mtx);
                std::string type = std::string(msg["type"].s());

                // 2.1 - KULLANICI GİRİŞİ (GÜVENLİ TOKEN MANTIĞI)
                if (type == "auth" && msg.has("token")) {
                    std::string token = std::string(msg["token"].s());
                    
                    // Token'ı çöz ve gerçek kullanıcı ID'sini bul (Frontend'e güvenmiyoruz!)
                    std::string realUserId = Security::verifyToken(token, db); 
                    
                    if (realUserId.empty()) {
                        conn.send_text("{\"error\": \"Gecersiz veya Suresi Dolmus Token\"}");
                        conn.close();
                        return;
                    }

                    online_users[realUserId] = &conn; 
                    CROW_LOG_INFO << "Kullanici sohbete baglandi: " << realUserId;
                    return;
                }

                // 2.2 - MESAJ GÖNDERME
                if (type == "message" && msg.has("sender_id") && msg.has("text")) {
                    std::string sId = std::string(msg["sender_id"].s());
                    std::string txt = std::string(msg["text"].s());
                    std::string contentType = msg.has("content_type") ? std::string(msg["content_type"].s()) : "Text";
                    bool isGroup = msg.has("is_group") ? msg["is_group"].b() : false;

                    // Güvenlik: Mesajı Disk İçin Şifrele
                    std::string encryptedMsg = Security::encryptMessage(txt);
                    
                    // Frontend'e gidecek Canlı Paket
                    crow::json::wvalue outMsg;
                    outMsg["type"] = "new_message";
                    outMsg["gönderenID"] = sId;
                    outMsg["Mesaj"] = txt; // Ekranda görünmesi için şifresiz iletilir
                    outMsg["MesajİçerikDurumu"] = contentType;
                    outMsg["is_group"] = isGroup;

                    // A) ÖZEL MESAJ (DM)
                    if (!isGroup && msg.has("target_id")) {
                        std::string tId = std::string(msg["target_id"].s());
                        outMsg["alıcıID"] = tId;
                        
                        // Dosyaya Şifreli Yaz
                        FileManager::savePrivateMessageJSON(sId, tId, encryptedMsg, contentType);

                        // Alıcı online ise DOĞRUDAN onun sabit odasına fırlat
                        if (online_users.count(tId) && online_users[tId] != nullptr) {
                            try { online_users[tId]->send_text(outMsg.dump()); } catch(...) {}
                        }
                    } 
                    // B) GRUP MESAJI
                    else if (isGroup && msg.has("group_id")) {
                        std::string gId = std::string(msg["group_id"].s());
                        outMsg["group_id"] = gId;

                        // Üyeleri Çek ve JSON dosyasına şifreli kaydet
                        auto members = db.getServerMembersDetails(gId);
                        int totalMembers = members.size();
                        FileManager::saveGroupMessageJSON(gId, sId, encryptedMsg, contentType, totalMembers);

                        // Gruptaki ONLINE kişilere mesajı canlı dağıt
                        for(const auto& member : members) {
                            std::string memberId = member.id; // GÜNCELLENDİ: member.user_id yerine member.id
                            if (memberId != sId && online_users.count(memberId) && online_users[memberId] != nullptr) {
                                try { online_users[memberId]->send_text(outMsg.dump()); } catch(...) {}
                            }
                        }
                    }
                }

                // 2.3 - "YAZIYOR..." BİLDİRİMİ (TYPING)
                if (type == "typing" && msg.has("sender_id") && msg.has("target_id")) {
                    std::string sId = std::string(msg["sender_id"].s());
                    std::string tId = std::string(msg["target_id"].s());

                    // Karşı taraf online ise "Yazıyor..." paketini ilet
                    if (online_users.count(tId) && online_users[tId] != nullptr) {
                        crow::json::wvalue typingMsg;
                        typingMsg["type"] = "typing";
                        typingMsg["user_id"] = sId;
                        try { online_users[tId]->send_text(typingMsg.dump()); } catch(...) {}
                    }
                }

                // 2.4 - "OKUNDU" BİLDİRİMİ (READ RECEIPT)
                if (type == "read_receipt" && msg.has("reader_id") && msg.has("sender_id")) {
                    std::string readerId = std::string(msg["reader_id"].s());
                    std::string senderId = std::string(msg["sender_id"].s()); // Mesajı asıl gönderen kişi

                    // 1. JSON dosyasında "Okundu" (true) olarak güncelle
                    FileManager::markMessagesAsRead(senderId, readerId);

                    // 2. Mesajı atan kişi online ise ona canlı olarak "Mavi Tik" yolla
                    if (online_users.count(senderId) && online_users[senderId] != nullptr) {
                        crow::json::wvalue readMsg;
                        readMsg["type"] = "messages_read";
                        readMsg["reader_id"] = readerId;
                        try { online_users[senderId]->send_text(readMsg.dump()); } catch(...) {}
                    }
                }
            }
            catch (...) { CROW_LOG_ERROR << "WS Chat Mesaj Hatasi"; }
        })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t code) {
            std::lock_guard<std::mutex> lock(chat_mtx);
            // Kullanıcı offline oldu, sistemden sil
            for (auto it = online_users.begin(); it != online_users.end(); ) {
                if (it->second == &conn) it = online_users.erase(it);
                else ++it;
            }
        });

    // ==========================================================
    // 3. WEBRTC REST API'LERİ (DEĞİŞTİRİLMEDİ)
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