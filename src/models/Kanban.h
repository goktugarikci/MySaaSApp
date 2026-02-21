#pragma once
#include <string>
#include <vector>
#include <crow.h> // JSON dönüşümleri için eklendi

// Kanban Kartı (Görev) Veri Modeli
struct KanbanCard {
    std::string id;
    std::string listId;
    std::string title;
    std::string description;
    int priority;
    int position;

    // Detaylı alanlar
    std::string assigneeId;
    bool isCompleted;
    std::string attachmentUrl;
    std::string dueDate;

    // Yönlendirme (Router) sınıfları için JSON'a dönüştürme metodu
    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id;
        json["list_id"] = listId;
        json["title"] = title;
        json["description"] = description;
        json["priority"] = priority;
        json["position"] = position;

        // Boş olmayan/varsayılan olmayan verileri de ekle
        if (!assigneeId.empty()) json["assignee_id"] = assigneeId;
        json["is_completed"] = isCompleted;
        if (!attachmentUrl.empty()) json["attachment_url"] = attachmentUrl;
        if (!dueDate.empty()) json["due_date"] = dueDate;

        return json;
    }
};

// Kanban Listesi (Sütun) Veri Modeli
struct KanbanList {
    std::string id;
    std::string title;
    int position;
    std::vector<KanbanCard> cards; // Bu listenin içindeki görev kartları

    // Liste ve içindeki kartları JSON'a dönüştürme metodu
    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id;
        json["title"] = title;
        json["position"] = position;

        // İç içe (Nested) JSON oluştur: Liste içindeki kartları da diziye (array) çevir
        std::vector<crow::json::wvalue> cardsJson;
        for (const auto& card : cards) {
            cardsJson.push_back(card.toJson());
        }
        json["cards"] = std::move(cardsJson);

        return json;
    }
};