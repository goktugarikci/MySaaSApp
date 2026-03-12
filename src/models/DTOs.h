#pragma once
#include <string>


struct ServerLog {
    std::string id;
    std::string server_id;
    std::string level;
    std::string action;
    std::string details;
    std::string timestamp; // Derleyicinin kızdığı eksik değişken buydu!
};
struct SystemStats {
    int total_users = 0;
    int total_servers = 0;
    int active_users = 0;
    int total_messages = 0;
};

// EKSİK OLAN UserReport BURAYA EKLENDİ
struct UserReport {
    std::string id;
    std::string reporter_id;
    std::string content_id;
    std::string type;
    std::string reason;
    std::string status;
};

struct ServerMemberDetail { std::string id; std::string name; std::string status; };
struct ServerInviteDTO { std::string server_id; std::string server_name; std::string inviter_name; std::string created_at; };
struct NotificationDTO { int id; std::string message; std::string type; std::string created_at; };

struct CardComment {
    std::string id;
    std::string card_id;
    std::string sender_id;
    std::string sender_name;
    std::string content;
    std::string timestamp;
};
struct CardChecklist {
    std::string id;
    std::string content;
    bool is_completed;
};
struct CardTag {
    std::string id;
    std::string tag_name;
    std::string color;
};
struct BannedUserRecord {
    std::string user_id;
    std::string reason;
    std::string date;
}; 
struct CardActivity {
    std::string id;
    std::string user_name;
    std::string action;
    std::string timestamp;
};