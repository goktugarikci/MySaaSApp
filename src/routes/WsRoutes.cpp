#include "WsRoutes.h"
#include "../db/DatabaseManager.h" // Include'u buraya aldık
#include "../utils/Security.h"
#include <crow/json.h>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <iostream>

namespace {
    std::unordered_map<std::string, std::unordered_set<crow::websocket::connection*>> channel_subscribers;
    std::mutex ws_mtx;
}

void WsRoutes::setup(crow::SimpleApp& app, DatabaseManager& db) {
    CROW_WEBSOCKET_ROUTE(app, "/ws/app")
        .onopen([&](crow::websocket::connection& conn) {
        std::cout << "[WS] Yeni bir istemci baglandi." << std::endl;
            })
        .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
        auto msg = crow::json::load(data);
        if (!msg || !msg.has("action")) return;

        std::string action = msg["action"].s();

        if (action == "subscribe_channel" && msg.has("channel_id")) {
            std::string channelId = msg["channel_id"].s();
            std::lock_guard<std::mutex> lock(ws_mtx);
            channel_subscribers[channelId].insert(&conn);
        }
        else if (action == "unsubscribe_channel" && msg.has("channel_id")) {
            std::string channelId = msg["channel_id"].s();
            std::lock_guard<std::mutex> lock(ws_mtx);
            channel_subscribers[channelId].erase(&conn);
        }
        else if (action == "send_message" && msg.has("channel_id") && msg.has("content") && msg.has("token")) {
            std::string token = msg["token"].s();
            std::string senderId;

            if (!Security::verifyJwt(token, senderId)) {
                conn.send_text("{\"error\": \"Gecersiz token\"}");
                return;
            }

            std::string channelId = msg["channel_id"].s();
            std::string content = msg["content"].s();
            std::string attachment = msg.has("attachment_url") ? std::string(msg["attachment_url"].s()) : std::string("");

            if (db.sendMessage(channelId, senderId, content, attachment)) {
                crow::json::wvalue response;
                response["type"] = "new_message";
                response["channel_id"] = channelId;
                response["sender_id"] = senderId;
                response["content"] = content;
                response["attachment_url"] = attachment;

                std::string responseStr = response.dump();

                std::lock_guard<std::mutex> lock(ws_mtx);
                if (channel_subscribers.count(channelId)) {
                    for (auto* client : channel_subscribers[channelId]) {
                        client->send_text(responseStr);
                    }
                }
            }
        }
        else if (action == "move_kanban_card" && msg.has("card_id") && msg.has("new_list_id") && msg.has("channel_id") && msg.has("token")) {
            std::string token = msg["token"].s();
            std::string userId;
            if (!Security::verifyJwt(token, userId)) return;

            std::string cardId = msg["card_id"].s();
            std::string newListId = msg["new_list_id"].s();
            int newPosition = msg.has("new_position") ? msg["new_position"].i() : 0;
            std::string channelId = msg["channel_id"].s();

            if (db.moveCard(cardId, newListId, newPosition)) {
                crow::json::wvalue response;
                response["type"] = "card_moved";
                response["card_id"] = cardId;
                response["new_list_id"] = newListId;
                response["new_position"] = newPosition;
                response["moved_by"] = userId;

                std::string responseStr = response.dump();

                std::lock_guard<std::mutex> lock(ws_mtx);
                if (channel_subscribers.count(channelId)) {
                    for (auto* client : channel_subscribers[channelId]) {
                        client->send_text(responseStr);
                    }
                }
            }
        }
            })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t code) {
        std::lock_guard<std::mutex> lock(ws_mtx);
        for (auto& pair : channel_subscribers) {
            pair.second.erase(&conn);
        }
        std::cout << "[WS] Istemci ayrildi." << std::endl;
            });
}