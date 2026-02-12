#pragma once
#include <string>
#include <vector>
#include "crow.h"

// Kart Yapısı
struct KanbanCard {
    int id;
    int list_id;
    std::string title;
    std::string description;
    int priority;
    int position;

    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id;
        json["list_id"] = list_id;
        json["title"] = title;
        json["description"] = description;
        json["priority"] = priority;
        json["position"] = position;
        return json;
    }
};

// Liste (Sütun) Yapısı - (Eski adı KanbanListWithCards idi)
struct KanbanList {
    int id;
    std::string title;
    int position;
    std::vector<KanbanCard> cards; // Kartları içinde barındırır

    crow::json::wvalue toJson() const {
        crow::json::wvalue json;
        json["id"] = id;
        json["title"] = title;
        json["position"] = position;

        crow::json::wvalue cardArray;
        for (size_t i = 0; i < cards.size(); ++i) {
            cardArray[i] = cards[i].toJson();
        }
        json["cards"] = std::move(cardArray);

        return json;
    }
};