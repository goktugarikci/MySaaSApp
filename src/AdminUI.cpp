#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <vector>

using json = nlohmann::json;

// --- UYGULAMA DURUMU (STATE) ---
enum class AppState {
    LOGIN,
    DASHBOARD
};

// --- MENÜ DURUMU ---
enum class MenuState {
    STATS,
    SERVERS,
    USERS,
    LOGS,
    API_DOCS
};

AppState currentState = AppState::LOGIN;
MenuState currentMenu = MenuState::STATS;

std::string jwtToken = "";
std::string loggedInUserId = "";

// --- LOGIN BİLGİLERİ ---
char emailBuffer[128] = "admin@mysaas.com"; // Test kolaylığı için varsayılan eklenebilir
char passwordBuffer[128] = "";
std::string loginErrorMessage = "";

// --- DASHBOARD VERİLERİ ---
int totalUsers = 0;
int totalServers = 0;
int totalMessages = 0;
std::string latestLog = "Bekleniyor...";

// Backend'den istatistikleri çekmek için fonksiyon
void fetchSystemStats() {
    auto response = cpr::Get(
        cpr::Url{ "http://localhost:8080/api/admin/logs/system" },
        cpr::Header{ {"Authorization", "Bearer " + jwtToken} }
    );

    if (response.status_code == 200) {
        std::cout << "Sistem verileri basariyla cekildi!" << std::endl;
        totalUsers = 25; // TODO: Gerçek DB endpoint'ine bağlandığında burası json parse edilecek
        totalServers = 8;
        totalMessages = 4502;
        latestLog = "Veriler guncellendi (200 OK)";
    }
    else {
        latestLog = "Veri cekme hatasi. Kod: " + std::to_string(response.status_code);
    }
}

// Giriş İşlemi (Backend /api/auth/login rotasına istek atar)
void attemptLogin() {
    loginErrorMessage = "";

    json payload = {
        {"email", std::string(emailBuffer)},
        {"password", std::string(passwordBuffer)}
    };

    auto response = cpr::Post(
        cpr::Url{ "http://localhost:8080/api/auth/login" },
        cpr::Header{ {"Content-Type", "application/json"} },
        cpr::Body{ payload.dump() }
    );

    if (response.status_code == 200) {
        auto resJson = json::parse(response.text);
        if (resJson.contains("token")) {
            jwtToken = resJson["token"];
            loggedInUserId = resJson["user_id"];
            currentState = AppState::DASHBOARD;
            currentMenu = MenuState::STATS;
            fetchSystemStats(); // Dashboard'a geçince verileri yükle
        }
    }
    else if (response.status_code == 401) {
        loginErrorMessage = "Hata: E-posta veya sifre yanlis!";
    }
    else {
        loginErrorMessage = "Hata: Sunucuya baglanilamadi! (Sunucu acik mi?)";
    }
}

// ==============================================================
// API DOKÜMANTASYON TABLOSUNU ÇİZME (ImGui)
// ==============================================================
void DrawApiDocsMenu() {
    ImGui::Text("MySaaSApp API Referans Dokumantasyonu");
    ImGui::TextDisabled("Not: Auth disindaki tum isteklerde Header icerisinde su bulunmalidir:");
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.9f, 1.0f), "Authorization: Bearer <TOKEN>");

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Mevcut Token'imi Goster (Postman icin kopyala)")) {
        ImGui::InputTextMultiline("##token_display", (char*)jwtToken.c_str(), jwtToken.size(), ImVec2(-1, 50), ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Tablo Kurulumu
    static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;

    // Yüksekliği hesapla (Pencerenin kalan kısmı kadar)
    ImVec2 outer_size = ImVec2(0.0f, ImGui::GetContentRegionAvail().y - 20);

    if (ImGui::BeginTable("ApiDocsTable", 4, flags, outer_size)) {
        ImGui::TableSetupScrollFreeze(0, 1); // Başlığı sabitle
        ImGui::TableSetupColumn("Islem / Modul", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Metot", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Endpoint", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Ornek JSON / Parametre", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // Satır Ekleme Yardımcısı (Lambda)
        auto addRow = [](const char* name, const char* method, const char* endpoint, const char* json_desc) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextWrapped("%s", name);

            // Renkli HTTP Metotları
            ImGui::TableSetColumnIndex(1);
            std::string m(method);
            if (m == "GET") ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "GET");
            else if (m == "POST") ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "POST");
            else if (m == "PUT") ImGui::TextColored(ImVec4(0.2f, 0.6f, 0.9f, 1.0f), "PUT");
            else if (m == "DELETE") ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "DELETE");
            else ImGui::Text("%s", method);

            ImGui::TableSetColumnIndex(2); ImGui::TextWrapped("%s", endpoint);
            ImGui::TableSetColumnIndex(3); ImGui::TextWrapped("%s", json_desc);
            };

        // --- AUTH ---
        addRow("Kullanici Girisi", "POST", "/api/auth/login", "{\"email\":\"...\", \"password\":\"...\"}");
        addRow("Kullanici Kaydi", "POST", "/api/auth/register", "{\"name\":\"...\", \"email\":\"...\", \"password\":\"...\"}");
        addRow("Google Login", "POST", "/api/auth/google/callback", "{\"email\":\"...\", \"google_id\":\"...\"}");

        // --- USERS ---
        addRow("Kendi Profilim", "GET", "/api/users/me", "(Parametre yok)");
        addRow("Kullanici Arama", "GET", "/api/users/search?q=goktug", "URL Parametresi: q (Arama Terimi)");

        // --- SERVERS & CHANNELS ---
        addRow("Sunucularim", "GET", "/api/servers", "(Parametre yok)");
        addRow("Sunucu Olustur", "POST", "/api/servers", "{\"name\":\"Yeni Sunucum\"}");
        addRow("Kanal Olustur", "POST", "/api/servers/<id>/channels", "{\"name\":\"genel\", \"type\":0, \"is_private\":false} \n(Type -> 0:Text, 1:Voice, 3:Kanban)");
        addRow("Sunucu Uyleri", "GET", "/api/servers/<id>/members", "(Parametre yok)");

        // --- MESSAGING ---
        addRow("Mesaj Gonder (REST)", "POST", "/api/channels/<id>/messages", "{\"content\":\"Selam!\", \"attachment_url\":\"\"}");
        addRow("Gecmis Mesajlar", "GET", "/api/channels/<id>/messages?limit=50", "URL Param: limit (Opsiyonel)");
        addRow("Mesaj Sil", "DELETE", "/api/messages/<id>", "Sadece kendi mesajin veya Admin");
        addRow("Mesaja Tepki (Emoji)", "POST", "/api/messages/<id>/react", "{\"emoji\":\"👍\"}");
        addRow("Mesaja Yanit (Thread)", "POST", "/api/messages/<id>/reply", "{\"content\":\"Tesekkurler!\"}");

        // --- KANBAN ---
        addRow("Kanban Panosu Getir", "GET", "/api/channels/<id>/kanban", "Panodaki liste ve kartlari dondurur");
        addRow("Yeni Liste Ekle", "POST", "/api/channels/<id>/kanban/lists", "{\"title\":\"Yapilacaklar\"}");
        addRow("Yeni Kart (Gorev)", "POST", "/api/kanban/lists/<id>/cards", "{\"title\":\"Bug Fix\", \"description\":\"...\", \"priority\":2}");
        addRow("Kart Tasi (Move)", "POST", "/api/kanban/cards/<id>/move", "{\"new_list_id\":\"...\", \"new_position\":0}");
        addRow("Karta Yorum Ekle", "POST", "/api/kanban/cards/<id>/comments", "{\"content\":\"Bu islem tamamlandi.\"}");
        addRow("Karta Etiket Ekle", "POST", "/api/kanban/cards/<id>/tags", "{\"tag_name\":\"Acil\", \"color\":\"#FF0000\"}");

        // --- ADMIN ---
        addRow("Sistem Loglari", "GET", "/api/admin/logs/system", "Sadece Super Admin (JWT)");
        addRow("Sunucu Analizi", "GET", "/api/admin/users/<id>/servers", "Kullanicinin bulundugu sunucular ve rolleri");
        addRow("Silinen Mesaj Arsivi", "GET", "/api/admin/archive/messages", "Silinmis eski mesajlar");

        ImGui::EndTable();
    }
}

int main() {
    // GLFW Başlatma
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "MySaaSApp - Kurumsal Yönetim Paneli", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // VSync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // Özel Admin Teması (Modern Koyu Mavi Tema)
    ImGui::StyleColorsDark();
    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.16f, 0.29f, 0.48f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.16f, 0.29f, 0.48f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    ImGui::GetStyle().WindowRounding = 4.0f;
    ImGui::GetStyle().FrameRounding = 4.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Ana Döngü
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

        // -------------------------------------------------------------
        // EKRAN 1: GİRİŞ (LOGIN) EKRANI
        // -------------------------------------------------------------
        if (currentState == AppState::LOGIN) {
            ImGui::Begin("Giris", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

            ImVec2 windowSize = ImGui::GetWindowSize();
            ImGui::SetCursorPos(ImVec2(windowSize.x * 0.5f - 175.0f, windowSize.y * 0.5f - 120.0f));

            ImGui::BeginChild("LoginForm", ImVec2(350, 280), true, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "M Y S A A S A P P   S E C U R E");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("E-Posta Adresi");
            ImGui::InputText("##email", emailBuffer, IM_ARRAYSIZE(emailBuffer));

            ImGui::Spacing();

            ImGui::Text("Yonetici Sifresi");
            ImGui::InputText("##password", passwordBuffer, IM_ARRAYSIZE(passwordBuffer), ImGuiInputTextFlags_Password);

            ImGui::Spacing(); ImGui::Spacing();

            if (ImGui::Button("Guvenli Giris Yap", ImVec2(-1, 35))) {
                attemptLogin();
            }

            if (!loginErrorMessage.empty()) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                ImGui::TextWrapped("%s", loginErrorMessage.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::EndChild();
            ImGui::End();
        }
        // -------------------------------------------------------------
        // EKRAN 2: YÖNETİM PANELİ (DASHBOARD)
        // -------------------------------------------------------------
        else if (currentState == AppState::DASHBOARD) {
            ImGui::Begin("Dashboard", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

            // --- ÜST BAR ---
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.9f, 1.0f), "MySaaSApp Yonetim Paneli");
            ImGui::SameLine();
            ImGui::TextDisabled(" | (Aktif Yonetici: %s)", loggedInUserId.c_str());
            ImGui::SameLine(ImGui::GetWindowWidth() - 100);
            if (ImGui::Button("Cikis Yap", ImVec2(80, 25))) {
                jwtToken = ""; loggedInUserId = "";
                currentState = AppState::LOGIN;
            }
            ImGui::Separator();

            // --- SOL MENÜ VE SAĞ İÇERİK ---
            ImGui::Columns(2, "AnaKolonlar", false);
            ImGui::SetColumnWidth(0, 220.0f); // Sol menü genişliği

            // SOL MENÜ ALANI
            ImGui::BeginChild("LeftMenu", ImVec2(0, 0), true);
            ImGui::TextDisabled("ANA MENÜ");
            ImGui::Spacing();

            if (ImGui::Selectable("Sistem İstatistikleri", currentMenu == MenuState::STATS)) currentMenu = MenuState::STATS;
            if (ImGui::Selectable("Sunucu Yönetimi", currentMenu == MenuState::SERVERS)) currentMenu = MenuState::SERVERS;
            if (ImGui::Selectable("Kullanıcı Yönetimi", currentMenu == MenuState::USERS)) currentMenu = MenuState::USERS;
            if (ImGui::Selectable("Sistem Logları (Audit)", currentMenu == MenuState::LOGS)) currentMenu = MenuState::LOGS;

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextDisabled("GELİŞTİRİCİ");
            ImGui::Spacing();

            if (ImGui::Selectable("API Dokümantasyonu", currentMenu == MenuState::API_DOCS)) currentMenu = MenuState::API_DOCS;

            ImGui::EndChild();

            // SAĞ İÇERİK ALANI
            ImGui::NextColumn();
            ImGui::BeginChild("RightContent", ImVec2(0, 0), false);

            if (currentMenu == MenuState::STATS) {
                ImGui::Text("SISTEM GENEL DURUMU");
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::Text("Toplam Kayitli Kullanici : %d", totalUsers);
                ImGui::Text("Aktif Sunucu Sayisi      : %d", totalServers);
                ImGui::Text("Gonderilen Toplam Mesaj  : %d", totalMessages);
                ImGui::Spacing();
                ImGui::TextDisabled("Son islem: %s", latestLog.c_str());
                ImGui::Spacing();
                if (ImGui::Button("Verileri Yenile", ImVec2(150, 30))) fetchSystemStats();
            }
            else if (currentMenu == MenuState::API_DOCS) {
                // YENİ EKLENEN API DOKÜMANTASYON EKRANI
                DrawApiDocsMenu();
            }
            else {
                ImGui::Text("Yapim Asamasinda...");
                ImGui::TextDisabled("Bu modul ilerleyen guncellemelerde aktif edilecektir.");
            }

            ImGui::EndChild();
            ImGui::Columns(1);
            ImGui::End();
        }

        ImGui::PopStyleVar();

        // Render
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}