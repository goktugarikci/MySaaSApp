#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <string>

// --- API ve Sistem Verileri İçin Değişkenler ---
int total_users = 1500;   // Backend /api/admin/stats üzerinden çekilecek
int total_servers = 340;
int total_messages = 8500;
float cpu_usage[90] = { 0 }; // Grafik için sahte/gerçek zamanlı CPU verisi
int time_offset = 0;

// Konsol İçin Değişkenler
char consoleInput[256] = "";
std::vector<std::string> consoleLog;

// Örnek Kullanıcı Verisi (Backend'den çekilecek)
struct AdminUser { std::string id; std::string name; std::string role; };
std::vector<AdminUser> userList = {
    {"aB3dE7xY9Z1kL0m", "Ahmet Yilmaz", "System Admin"},
    {"xZ9mK2pL4qR8vN1", "Mehmet Kaya", "User"},
    {"vN1xZ9mK2pL4qR8", "Ayse Demir", "User"}
};

// --- ARAYÜZ ÇİZİM FONKSİYONLARI ---

void DrawDashboard() {
    ImGui::Begin("Sistem Monitörü (Dashboard)");

    ImGui::Text("SaaS Genel Durumu");
    ImGui::Separator();

    // İstatistikler
    ImGui::Columns(3, "stats_columns", false);
    ImGui::Text("Kullanicilar\n%d", total_users); ImGui::NextColumn();
    ImGui::Text("Sunucular\n%d", total_servers); ImGui::NextColumn();
    ImGui::Text("Mesajlar\n%d", total_messages); ImGui::NextColumn();
    ImGui::Columns(1);

    ImGui::Spacing();
    ImGui::Separator();

    // Rastgele CPU Kullanım Simülasyonu Grafiği (Animasyonlu)
    cpu_usage[time_offset] = (float)(rand() % 100);
    time_offset = (time_offset + 1) % 90;
    ImGui::Text("CPU ve Bellek Yuku (%%)");
    ImGui::PlotLines("CPU", cpu_usage, 90, time_offset, "Kullanim", 0.0f, 100.0f, ImVec2(0, 80));

    // Progress Barlar
    ImGui::Text("Veritabani Dolulugu");
    ImGui::ProgressBar(0.45f, ImVec2(0.0f, 0.0f), "45% (15 GB / 33 GB)");

    ImGui::End();
}

void DrawUserManagement() {
    ImGui::Begin("Kullanıcı Yönetimi");

    if (ImGui::Button("Verileri Yenile (API'den Çek)")) {
        consoleLog.push_back("[SİSTEM] Kullanici verileri backend'den guncellendi.");
        // cpr::Get(cpr::Url{"http://localhost:8080/api/admin/users"}, cpr::Header{{"Authorization", "mock-jwt-token-ADMIN"}});
    }

    ImGui::Spacing();

    // Tablo Çizimi
    if (ImGui::BeginTable("UsersTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Ad Soyad");
        ImGui::TableSetupColumn("Rol");
        ImGui::TableSetupColumn("İşlemler");
        ImGui::TableHeadersRow();

        for (int i = 0; i < userList.size(); i++) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", userList[i].id.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%s", userList[i].name.c_str());
            ImGui::TableSetColumnIndex(2);

            // Rol renklendirmesi
            if (userList[i].role == "System Admin") ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Admin");
            else ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "User");

            ImGui::TableSetColumnIndex(3);
            std::string btnLabel = "Banla##" + std::to_string(i); // ImGui aynı isimli butonları karıştırmasın diye ## eklenir
            if (ImGui::Button(btnLabel.c_str())) {
                consoleLog.push_back("[UYARI] Kullanici yasaklandi: " + userList[i].name);
                // cpr::Post(cpr::Url{"http://localhost:8080/api/admin/users/" + userList[i].id + "/ban"});
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DrawConsole() {
    ImGui::Begin("Süper Admin Terminali");

    // Log Ekranı (Kaydırılabilir alan)
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& log : consoleLog) {
        if (log.find("[HATA]") != std::string::npos) ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "%s", log.c_str());
        else if (log.find("[UYARI]") != std::string::npos) ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "%s", log.c_str());
        else ImGui::TextUnformatted(log.c_str());
    }
    // Otomatik en alta kaydır
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::Separator();

    // Komut Giriş Alanı
    bool reclaim_focus = false;
    ImGui::PushItemWidth(-60); // Butona yer bırak
    if (ImGui::InputText("##Komut", consoleInput, IM_ARRAYSIZE(consoleInput), ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::string cmd = consoleInput;
        if (!cmd.empty()) {
            consoleLog.push_back("root@mysaas:~# " + cmd);
            // Basit Komut İşleme Mantığı
            if (cmd == "clear") consoleLog.clear();
            else if (cmd == "help") consoleLog.push_back("[BİLGİ] Gecerli komutlar: clear, help, /ban <id>");
            else consoleLog.push_back("[HATA] Gecersiz komut: " + cmd);
        }
        strcpy_s(consoleInput, ""); // Inputu temizle
        reclaim_focus = true;
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Gonder") || reclaim_focus) {
        ImGui::SetKeyboardFocusHere(-1); // Enter'a basınca veya Gonder deyince focusta kal
    }

    ImGui::End();
}

// --- ANA UYGULAMA (MAIN) ---
int main() {
    // 1. GLFW ve OpenGL Başlatma
    if (!glfwInit()) return -1;
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "MySaaS - Super Admin Arayüzü", NULL, NULL);
    if (!window) return -1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // V-Sync Aktif

    // 2. ImGui Başlatma
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark(); // Koyu (Hacker) Teması

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    consoleLog.push_back("[SİSTEM] Super Admin paneli baslatildi.");
    consoleLog.push_back("[SİSTEM] Backend ile baglanti kuruluyor... (http://localhost:8080)");

    // 3. Render Döngüsü
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Yeni Frame Başlat
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Arayüzleri Çiz
        DrawDashboard();
        DrawUserManagement();
        DrawConsole();

        // Render ve Ekranı Temizleme
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // 4. Temizlik
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}