#pragma once
#include <string>

struct SystemStats { int user_count; int server_count; int message_count; };

struct ServerLog {
    std::string id;          // Rota Beklentisi
    std::string server_id;
    std::string level;       // Rota Beklentisi
    std::string action;
    std::string details;
    std::string created_at;
};

struct ServerMemberDetail { std::string id; std::string name; std::string status; };
struct ServerInviteDTO { std::string server_id; std::string server_name; std::string inviter_name; std::string created_at; };
struct NotificationDTO { int id; std::string message; std::string type; std::string created_at; };

struct CardComment {
    std::string id;
    std::string card_id;
    std::string sender_id;   // Rota Beklentisi
    std::string sender_name; // Rota Beklentisi
    std::string content;
    std::string timestamp;   // Rota Beklentisi
};

struct CardTag {
    std::string id;          // Rota Beklentisi
    std::string tag_name;    // Rota Beklentisi
    std::string color;       // Rota Beklentisi
};