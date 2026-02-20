#pragma once
#include <string>

// Sistem & Raporlama Modelleri
struct SystemStats {
    int user_count;
    int server_count;
    int message_count;
};

struct UserReport {
    std::string id;
    std::string reporter_id;
    std::string content_id;
    std::string type;
    std::string reason;
    std::string status;
};

// Sunucu İçi Veri Modelleri
struct ServerInviteDTO {
    std::string server_id;
    std::string server_name;
    std::string inviter_name;
    std::string created_at;
};

struct ServerMemberDetail {
    std::string id;
    std::string name;
    std::string status;
};

struct ServerLog {
    std::string timestamp;
    std::string action;
    std::string details;
};

// Bildirim ve Loglama Modelleri
struct NotificationDTO {
    int id;
    std::string message;
    std::string type;
    std::string created_at;
};

struct SystemLogDTO {
    int id;
    std::string level;
    std::string action;
    std::string details;
    std::string created_at;
};

struct ArchivedMessageDTO {
    std::string id;
    std::string original_channel_id;
    std::string sender_id;
    std::string content;
    std::string deleted_at;
};

// [YENİ] Gelişmiş Mesajlaşma Modelleri
struct ReactionDTO {
    std::string message_id;
    std::string user_id;
    std::string emoji;
};

struct ThreadReplyDTO {
    std::string id;
    std::string parent_message_id;
    std::string sender_id;
    std::string sender_name;
    std::string content;
    std::string timestamp;
};

// [YENİ] Gelişmiş Kanban Modelleri
struct CardCommentDTO {
    std::string id;
    std::string card_id;
    std::string sender_id;
    std::string sender_name;
    std::string content;
    std::string timestamp;
};

struct CardTagDTO {
    std::string id;
    std::string card_id;
    std::string tag_name;
    std::string color;
};

struct SystemStats {
    int user_count;
    int server_count;
    int message_count;
};

struct ServerInviteDTO {
    std::string serverId;
    std::string serverName;
    std::string inviterName;
    std::string createdAt;
};

struct ServerMemberDetail {
    std::string id;
    std::string name;
    std::string status;
};

struct ServerLog {
    std::string createdAt;
    std::string action;
    std::string details;
};
