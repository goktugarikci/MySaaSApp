#pragma once
#include <string>

// İstatistikler
struct SystemStats {
    int user_count;
    int server_count;
    int message_count;
};

// Rapor Modeli
struct UserReport {
    std::string id;
    std::string reporter_id;
    std::string content_id;
    std::string type;
    std::string reason;
    std::string status;
};

// Sunucu Logları
struct ServerLog {
    std::string timestamp;
    std::string action;
    std::string details;
};

// Sunucu Üye Detayları
struct ServerMemberDetail {
    std::string id;
    std::string name;
    std::string status;
};

// Sunucu Davetleri
struct ServerInviteDTO {
    std::string server_id;
    std::string server_name;
    std::string inviter_name;
    std::string created_at;
};

// Sistem Bildirimleri
struct NotificationDTO {
    int id;
    std::string message;
    std::string type;
    std::string created_at;
};