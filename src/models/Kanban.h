#pragma once
#include <string>
#include <vector>

// Kanban Kartı (Görev) Veri Modeli
struct KanbanCard {
    std::string id;
    std::string listId;
    std::string title;
    std::string description;
    int priority;
    int position;

    // Gelecekte eklenebilecek detaylı alanlar (DB'de var olanlar)
    std::string assigneeId;
    bool isCompleted;
    std::string attachmentUrl;
    std::string dueDate;
};

// Kanban Listesi (Sütun) Veri Modeli
struct KanbanList {
    std::string id;
    std::string title;
    int position;
    std::vector<KanbanCard> cards; // Bu listenin içindeki kartlar
};