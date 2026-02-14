// =============================================================
// 1. WINSOCK & WINDOWS API 
// =============================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h> 
#include <windows.h>  
#include <psapi.h>     
#include <tlhelp32.h>  
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#pragma comment(lib, "psapi.lib") 

// =============================================================
// 2. AĞ VE JSON KÜTÜPHANELERİ
// =============================================================
#include <cpr/cpr.h>
#include "crow.h"

// =============================================================
// 3. GRAFİK MOTORLARI
// =============================================================
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

// =============================================================
// 4. STANDART KÜTÜPHANELER
// =============================================================
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <filesystem>  
#include <cfloat>      
#include <sstream>
#include <cmath> // NaN kontrolü için eklendi

// --- AYARLAR ---
const std::string ADMIN_TOKEN = "mock-jwt-token-aB3dE7xY9Z1kL0m";
const std::string API_BASE_URL = "http://localhost:8080/api";

// --- SİSTEM DONANIM DEĞİŞKENLERİ ---
int time_offset = 0;
double last_update_time = 0.0;
float app_cpu_usage = 0.0f, app_ram_usage_mb = 0.0f, db_size_mb = 0.0f;
float app_cpu_graph[90] = { 0 }, app_ram_graph[90] = { 0 }, db_size_graph[90] = { 0 };
float current_cpu = 0.0f, current_ram_used_gb = 0.0f, current_ram_total_gb = 0.0f, current_ram_percent = 0.0f;
float current_disk_used_gb = 0.0f, current_disk_total_gb = 0.0f, current_disk_percent = 0.0f;

// Zaman değişkenlerine sıfır atandı
static FILETIME prevSysIdle = { 0 }, prevSysKernel = { 0 }, prevSysUser = { 0 };
static FILETIME prevProcKernel = { 0 }, prevProcUser = { 0 }, prevProcSysKernel = { 0 }, prevProcSysUser = { 0 };
static bool firstProcRun = true;

// --- API VERİ MODELLERİ VE DEĞİŞKENLERİ ---
struct AdminUser { std::string id, name, email, role, status, sub_level, created_at; };
// Uyarıları çözmek için değişkenlere varsayılan değer (= 0) atandı
struct AdminServer { std::string id, name, owner_id; int member_count = 0, active_count = 0; };

std::vector<AdminUser> userList;
std::vector<AdminServer> serverList;

std::mutex userListMutex, serverListMutex;
std::atomic<bool> isFetchingUsers(false), isFetchingServers(false);

char consoleInput[256] = "";
std::vector<std::string> consoleLog;

bool show_ban_list = false;
bool show_user_stats = false;
bool show_server_stats = false;
std::string active_stats_id = "";

// =============================================================
// YARDIMCI FONKSİYONLAR (DONANIM)
// =============================================================
DWORD GetBackendProcessId() {
    // Uyarıyı çözmek için bellek sıfırlandı
    PROCESSENTRY32 pe32 = { 0 };
    pe32.dwSize = sizeof(PROCESSENTRY32);
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return 0;
    if (Process32First(hSnapshot, &pe32)) {
        do {
            if (std::string(pe32.szExeFile) == "MySaaSApp.exe") { CloseHandle(hSnapshot); return pe32.th32ProcessID; }
        } while (Process32Next(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot); return 0;
}

void UpdateHardwareMetrics() {
    FILETIME sysIdle, sysKernel, sysUser;
    if (GetSystemTimes(&sysIdle, &sysKernel, &sysUser)) {
        if (prevSysIdle.dwLowDateTime != 0 || prevSysIdle.dwHighDateTime != 0) {
            ULARGE_INTEGER iT, kT, uT, piT, pkT, puT;
            iT.LowPart = sysIdle.dwLowDateTime; iT.HighPart = sysIdle.dwHighDateTime;
            kT.LowPart = sysKernel.dwLowDateTime; kT.HighPart = sysKernel.dwHighDateTime;
            uT.LowPart = sysUser.dwLowDateTime; uT.HighPart = sysUser.dwHighDateTime;
            piT.LowPart = prevSysIdle.dwLowDateTime; piT.HighPart = prevSysIdle.dwHighDateTime;
            pkT.LowPart = prevSysKernel.dwLowDateTime; pkT.HighPart = prevSysKernel.dwHighDateTime;
            puT.LowPart = prevSysUser.dwLowDateTime; puT.HighPart = prevSysUser.dwHighDateTime;
            ULONGLONG totalSys = (kT.QuadPart - pkT.QuadPart) + (uT.QuadPart - puT.QuadPart);
            if (totalSys > 0) current_cpu = (float)(((totalSys - (iT.QuadPart - piT.QuadPart)) * 100.0) / totalSys);
            if (std::isnan(current_cpu) || std::isinf(current_cpu)) current_cpu = 0.0f; // NaN Koruması
        }
        prevSysIdle = sysIdle; prevSysKernel = sysKernel; prevSysUser = sysUser;
    }

    MEMORYSTATUSEX memInfo; memInfo.dwLength = sizeof(MEMORYSTATUSEX); GlobalMemoryStatusEx(&memInfo);
    current_ram_total_gb = memInfo.ullTotalPhys / (1024.0f * 1024.0f * 1024.0f);
    current_ram_used_gb = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024.0f * 1024.0f * 1024.0f);
    current_ram_percent = (current_ram_used_gb / current_ram_total_gb);

    ULARGE_INTEGER fba, tnb, tnfb;
    if (GetDiskFreeSpaceExA(NULL, &fba, &tnb, &tnfb)) {
        current_disk_total_gb = tnb.QuadPart / (1024.0f * 1024.0f * 1024.0f);
        current_disk_used_gb = current_disk_total_gb - (tnfb.QuadPart / (1024.0f * 1024.0f * 1024.0f));
        current_disk_percent = (current_disk_used_gb / current_disk_total_gb);
    }

    static DWORD prevPid = 0; DWORD pid = GetBackendProcessId();
    if (pid != 0) {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (hProc) {
            PROCESS_MEMORY_COUNTERS pmc;
            if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) app_ram_usage_mb = pmc.WorkingSetSize / (1024.0f * 1024.0f);
            FILETIME cT, eT, kT, uT;
            if (GetProcessTimes(hProc, &cT, &eT, &kT, &uT)) {
                if (!firstProcRun && pid == prevPid) {
                    ULONGLONG sysKDiff = (((ULARGE_INTEGER*)&sysKernel)->QuadPart - ((ULARGE_INTEGER*)&prevProcSysKernel)->QuadPart);
                    ULONGLONG sysUDiff = (((ULARGE_INTEGER*)&sysUser)->QuadPart - ((ULARGE_INTEGER*)&prevProcSysUser)->QuadPart);
                    ULONGLONG procKDiff = (((ULARGE_INTEGER*)&kT)->QuadPart - ((ULARGE_INTEGER*)&prevProcKernel)->QuadPart);
                    ULONGLONG procUDiff = (((ULARGE_INTEGER*)&uT)->QuadPart - ((ULARGE_INTEGER*)&prevProcUser)->QuadPart);
                    ULONGLONG totalSys = sysKDiff + sysUDiff;
                    ULONGLONG totalProc = procKDiff + procUDiff;

                    if (totalSys > 0) app_cpu_usage = (float)((totalProc * 100.0) / totalSys);
                    else app_cpu_usage = 0.0f;

                    // NaN Koruması eklendi
                    if (std::isnan(app_cpu_usage) || std::isinf(app_cpu_usage)) app_cpu_usage = 0.0f;
                }
                prevProcKernel = kT; prevProcUser = uT;
                prevProcSysKernel = sysKernel; prevProcSysUser = sysUser;
                firstProcRun = false;
            }
            CloseHandle(hProc);
        }
        prevPid = pid;
    }
    else { app_cpu_usage = 0.0f; app_ram_usage_mb = 0.0f; prevPid = 0; firstProcRun = true; }

    try {
        if (std::filesystem::exists("mysaasapp.db")) db_size_mb = std::filesystem::file_size("mysaasapp.db") / (1024.0f * 1024.0f);
        else db_size_mb = 0.0f;
    }
    catch (...) { db_size_mb = 0.0f; }
}

// =============================================================
// ASENKRON API İSTEKLERİ
// =============================================================
void FetchUsersAsync() {
    if (isFetchingUsers) return;
    isFetchingUsers = true;
    consoleLog.push_back("[SISTEM] Backend'den kullanici verileri cekiliyor...");

    std::thread([]() {
        cpr::Response r = cpr::Get(cpr::Url{ API_BASE_URL + "/admin/users" }, cpr::Header{ {"Authorization", ADMIN_TOKEN} });
        if (r.status_code == 200) {
            try {
                auto j = crow::json::load(r.text);
                if (j && j.t() == crow::json::type::List) {
                    std::lock_guard<std::mutex> lock(userListMutex);
                    userList.clear();
                    for (const auto& item : j) {
                        AdminUser u;
                        if (item.has("id")) u.id = item["id"].s();
                        if (item.has("name")) u.name = item["name"].s();
                        if (item.has("email")) u.email = item["email"].s(); else u.email = "Bilinmiyor";
                        if (item.has("status")) u.status = item["status"].s(); else u.status = "Offline";
                        if (item.has("created_at")) u.created_at = item["created_at"].s(); else u.created_at = "-";

                        bool isAdmin = false;
                        if (item.has("is_system_admin")) {
                            if (item["is_system_admin"].t() == crow::json::type::True) isAdmin = true;
                            else if (item["is_system_admin"].t() == crow::json::type::Number && item["is_system_admin"].i() == 1) isAdmin = true;
                        }
                        u.role = isAdmin ? "System Admin" : "User";

                        int sub = 0;
                        if (item.has("subscription_level")) sub = item["subscription_level"].i();
                        if (sub == 1) u.sub_level = "Pro";
                        else if (sub == 2) u.sub_level = "Enterprise";
                        else u.sub_level = "Normal";

                        userList.push_back(u);
                    }
                    consoleLog.push_back("[BASARILI] Kullanicilar listelendi.");
                }
            }
            catch (...) { consoleLog.push_back("[HATA] JSON Parse Hatasi (Users)"); }
        }
        else if (r.status_code == 403) { consoleLog.push_back("[HATA] 403 Forbidden! Yetkisiz istek reddedildi."); }
        else { consoleLog.push_back("[HATA] API Baglantisi Yok. Sunucu Acik mi?"); }
        isFetchingUsers = false;
        }).detach();
}

void FetchServersAsync() {
    if (isFetchingServers) return;
    isFetchingServers = true;
    consoleLog.push_back("[SISTEM] Backend'den sunucu verileri cekiliyor...");

    std::thread([]() {
        cpr::Response r = cpr::Get(cpr::Url{ API_BASE_URL + "/admin/servers" }, cpr::Header{ {"Authorization", ADMIN_TOKEN} });
        if (r.status_code == 200) {
            try {
                auto j = crow::json::load(r.text);
                if (j && j.t() == crow::json::type::List) {
                    std::lock_guard<std::mutex> lock(serverListMutex);
                    serverList.clear();
                    for (const auto& item : j) {
                        AdminServer s;
                        if (item.has("id")) s.id = item["id"].s();
                        if (item.has("name")) s.name = item["name"].s();
                        if (item.has("owner_id")) s.owner_id = item["owner_id"].s();
                        if (item.has("member_count")) s.member_count = item["member_count"].i(); else s.member_count = 1;

                        s.active_count = s.member_count > 0 ? (rand() % s.member_count) + 1 : 0;
                        if (s.active_count > s.member_count) s.active_count = s.member_count;
                        serverList.push_back(s);
                    }
                    consoleLog.push_back("[BASARILI] Sunucular listelendi.");
                }
            }
            catch (...) { consoleLog.push_back("[HATA] JSON Parse Hatasi (Servers)"); }
        }
        isFetchingServers = false;
        }).detach();
}

// =============================================================
// KONSOL KOMUT İŞLEYİCİ
// =============================================================
std::vector<std::string> ParseCommand(const std::string& cmd) {
    std::istringstream iss(cmd);
    std::vector<std::string> tokens; std::string token;
    while (iss >> token) tokens.push_back(token);
    return tokens;
}

void ProcessConsoleCommand(const std::string& cmd) {
    if (cmd.empty()) return;
    consoleLog.push_back("root@mysaas:~# " + cmd);

    auto args = ParseCommand(cmd);
    std::string action = args[0];

    if (action == "clear") { consoleLog.clear(); }
    else if (action == "help") {
        consoleLog.push_back("[BILGI] Komutlar: clear, sync_users, sync_servers");
        consoleLog.push_back("[BILGI] -ban <id> <Süre> <Sebep> | -unban <id> | -banList");
        consoleLog.push_back("[BILGI] statsUser <id> | statsServer <id>");
    }
    else if (action == "-banList") { consoleLog.push_back("[SISTEM] Yasakli kullanicilar aciliyor..."); show_ban_list = true; }
    else if (action == "-ban") {
        if (args.size() < 2) { consoleLog.push_back("[HATA] Kullanim: -ban <id> <sure> <sebep>"); return; }
        consoleLog.push_back("[UYARI] " + args[1] + " yasaklaniyor...");
    }
    else if (action == "-unban") {
        if (args.size() < 2) { consoleLog.push_back("[HATA] Kullanim: -unban <id>"); return; }
        consoleLog.push_back("[BILGI] " + args[1] + " yasagi kaldiriliyor.");
    }
    else if (action == "statsUser" || action == "-statsUser") {
        if (args.size() < 2) { consoleLog.push_back("[HATA] Kullanim: statsUser <id>"); return; }
        active_stats_id = args[1]; show_user_stats = true;
    }
    else if (action == "statsServer") {
        if (args.size() < 2) { consoleLog.push_back("[HATA] Kullanim: statsServer <id>"); return; }
        active_stats_id = args[1]; show_server_stats = true;
    }
    else if (action == "sync_users") { FetchUsersAsync(); }
    else if (action == "sync_servers") { FetchServersAsync(); }
    else { consoleLog.push_back("[HATA] Gecersiz komut. 'help' yazin."); }
}

// =============================================================
// ARAYÜZ ÇİZİM FONKSİYONLARI VE POPUP PENCERELER
// =============================================================
void DrawBanListModal() {
    if (!show_ban_list) return;
    ImGui::SetNextWindowSize(ImVec2(700, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Sistem Yasaklari (Ban Listesi)", &show_ban_list)) {
        if (ImGui::BeginTable("BanTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Kullanici ID"); ImGui::TableSetupColumn("E-Posta");
            ImGui::TableSetupColumn("Ad Soyad"); ImGui::TableSetupColumn("Sure");
            ImGui::TableSetupColumn("Sebep"); ImGui::TableSetupColumn("Tarih");
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("xZ9mK2pL4qR8vN1");
            ImGui::TableSetColumnIndex(1); ImGui::Text("spammer@mail.com");
            ImGui::TableSetColumnIndex(2); ImGui::Text("Ali Veli");
            ImGui::TableSetColumnIndex(3); ImGui::TextColored(ImVec4(1, 0, 0, 1), "Süresiz");
            ImGui::TableSetColumnIndex(4); ImGui::Text("Reklam / Spam");
            ImGui::TableSetColumnIndex(5); ImGui::Text("2026-02-14");
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void DrawUserStatsModal() {
    if (!show_user_stats) return;
    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    std::string title = "Kullanici Istihbarati: " + active_stats_id;
    if (ImGui::Begin(title.c_str(), &show_user_stats)) {
        if (ImGui::BeginTabBar("UserStatsTabs")) {
            if (ImGui::BeginTabItem("Aktif Sunucular")) { ImGui::Text("Veriler yukleniyor..."); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Arkadaslar & DM Kayitlari")) { ImGui::Text("Veriler yukleniyor..."); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Odeme Gecmisi")) { ImGui::Text("Veriler yukleniyor..."); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Gecmis (Eski) Kayitlar")) { ImGui::Text("Veriler yukleniyor..."); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void DrawServerStatsModal() {
    if (!show_server_stats) return;
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    std::string title = "Sunucu Gozetimi (Canli): " + active_stats_id;
    if (ImGui::Begin(title.c_str(), &show_server_stats)) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "DIKKAT: Yonetici ayricaligi ile ozel sunucu loglari goruntuleniyor.");
        ImGui::Separator();
        ImGui::Columns(2, "ServerColumns"); ImGui::SetColumnWidth(0, 200);
        ImGui::Text("Kanallar");
        ImGui::BeginChild("ChannelsRegion", ImVec2(0, 0), true);
        ImGui::Selectable("# genel-sohbet", true); ImGui::Selectable("# duyurular"); ImGui::Selectable("# yardim");
        ImGui::EndChild();
        ImGui::NextColumn();
        ImGui::Text("Mesaj Loglari");
        ImGui::BeginChild("MessagesRegion", ImVec2(0, 0), true);
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[2026-02-14 14:00] Yonetici:"); ImGui::SameLine(); ImGui::Text("Log sistemi hazir!");
        ImGui::EndChild();
        ImGui::Columns(1);
    }
    ImGui::End();
}

void DrawDashboard() {
    ImGui::Begin("Sistem Monitoru (Gercek Zamanli Donanim & Veri)");

    int total_users = 0;
    int online_users = 0;
    int chatting_users = 0;

    {
        std::lock_guard<std::mutex> lock(userListMutex);
        total_users = userList.size();
        for (const auto& u : userList) { if (u.status == "Online") online_users++; }
    }
    {
        std::lock_guard<std::mutex> lock(serverListMutex);
        for (const auto& s : serverList) { chatting_users += s.active_count; }
    }

    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Canli Ag ve Kullanici Istatistikleri");
    ImGui::Separator(); ImGui::Spacing();
    ImGui::Columns(3, "network_stats", false);
    ImGui::Text("Toplam Kayitli Kullanici");
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%d Kisi", total_users);
    ImGui::NextColumn();
    ImGui::Text("Aktif (Online) Kullanici");
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%d Kisi", online_users);
    ImGui::NextColumn();
    ImGui::Text("Sohbetteki Kullanici Sayisi");
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d Kisi", chatting_users);
    ImGui::NextColumn();
    ImGui::Columns(1);
    ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();

    double current_time = ImGui::GetTime();
    if (current_time - last_update_time > 0.5) {
        UpdateHardwareMetrics();
        app_cpu_graph[time_offset] = app_cpu_usage;
        app_ram_graph[time_offset] = app_ram_usage_mb;
        db_size_graph[time_offset] = db_size_mb;
        time_offset = (time_offset + 1) % 90;
        last_update_time = current_time;
    }
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "MySaaSApp.exe (Sunucu) Anlik Kaynak Tuketimi");
    ImGui::Separator(); ImGui::Spacing();
    char appCpuOverlay[64]; sprintf_s(appCpuOverlay, "Sunucu CPU: %.2f%%", app_cpu_usage);
    ImGui::PlotLines("##AppCPU", app_cpu_graph, 90, time_offset, appCpuOverlay, 0.0f, 100.0f, ImVec2(ImGui::GetContentRegionAvail().x, 50));
    char appRamOverlay[64]; sprintf_s(appRamOverlay, "Sunucu RAM: %.2f MB", app_ram_usage_mb);
    ImGui::PlotLines("##AppRAM", app_ram_graph, 90, time_offset, appRamOverlay, 0.0f, FLT_MAX, ImVec2(ImGui::GetContentRegionAvail().x, 50));
    char dbOverlay[64]; sprintf_s(dbOverlay, "Veritabani (.db): %.3f MB", db_size_mb);
    ImGui::PlotLines("##DBSize", db_size_graph, 90, time_offset, dbOverlay, 0.0f, FLT_MAX, ImVec2(ImGui::GetContentRegionAvail().x, 50));
    ImGui::Spacing(); ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Genel Sunucu Makinesi Durumu");
    ImGui::Separator(); ImGui::Spacing();
    char ramOverlay[64]; sprintf_s(ramOverlay, "Sistem RAM: %.1f GB / %.1f GB (%.1f%%)", current_ram_used_gb, current_ram_total_gb, current_ram_percent * 100.0f);
    ImGui::ProgressBar(current_ram_percent, ImVec2(-1.0f, 0.0f), ramOverlay);
    ImGui::Spacing();
    char diskOverlay[64]; sprintf_s(diskOverlay, "Sistem Diski: %.1f GB / %.1f GB (%.1f%%)", current_disk_used_gb, current_disk_total_gb, current_disk_percent * 100.0f);
    ImGui::ProgressBar(current_disk_percent, ImVec2(-1.0f, 0.0f), diskOverlay);
    ImGui::End();
}

void DrawUserManagement() {
    ImGui::Begin("Kullanici Yonetimi (CRM)");
    if (ImGui::Button("Kullanicilari Yenile (Sync)")) FetchUsersAsync();
    if (isFetchingUsers) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 1, 0, 1), "  Yukleniyor..."); }
    ImGui::Spacing();
    if (ImGui::BeginTable("UsersTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("ID"); ImGui::TableSetupColumn("Ad Soyad"); ImGui::TableSetupColumn("E-Posta");
        ImGui::TableSetupColumn("Durum"); ImGui::TableSetupColumn("Abonelik"); ImGui::TableSetupColumn("Yetki"); ImGui::TableSetupColumn("Islemler");
        ImGui::TableHeadersRow();
        std::lock_guard<std::mutex> lock(userListMutex);
        for (int i = 0; i < userList.size(); i++) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", userList[i].id.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%s", userList[i].name.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%s", userList[i].email.c_str());
            ImGui::TableSetColumnIndex(3);
            if (userList[i].status == "Online") ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Cevrimici");
            else ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Cevrimdisi");
            ImGui::TableSetColumnIndex(4);
            if (userList[i].sub_level == "Enterprise") ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Enterprise");
            else if (userList[i].sub_level == "Pro") ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "Pro");
            else ImGui::Text("%s", userList[i].sub_level.c_str());
            ImGui::TableSetColumnIndex(5);
            if (userList[i].role == "System Admin") ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Admin");
            else ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "User");
            ImGui::TableSetColumnIndex(6);
            std::string btnLabel = "Islem##U" + std::to_string(i);
            if (ImGui::Button(btnLabel.c_str())) { ProcessConsoleCommand("statsUser " + userList[i].id); }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DrawServerManagement() {
    ImGui::Begin("Sunucu (Server) Yonetimi");
    if (ImGui::Button("Sunuculari Yenile (Sync)")) FetchServersAsync();
    if (isFetchingServers) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 1, 0, 1), "  Yukleniyor..."); }
    ImGui::Spacing();
    if (ImGui::BeginTable("ServersTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("ID"); ImGui::TableSetupColumn("Sunucu Adi"); ImGui::TableSetupColumn("Kurucu ID");
        ImGui::TableSetupColumn("Uyeler / Aktif"); ImGui::TableSetupColumn("Islemler");
        ImGui::TableHeadersRow();
        std::lock_guard<std::mutex> lock(serverListMutex);
        for (int i = 0; i < serverList.size(); i++) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", serverList[i].id.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", serverList[i].name.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%s", serverList[i].owner_id.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d Kayitli (", serverList[i].member_count); ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%d Aktif", serverList[i].active_count); ImGui::SameLine(); ImGui::Text(")");
            ImGui::TableSetColumnIndex(4);
            std::string btnLabel = "Incele##S" + std::to_string(i);
            if (ImGui::Button(btnLabel.c_str())) { ProcessConsoleCommand("statsServer " + serverList[i].id); }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DrawConsole() {
    ImGui::Begin("Super Admin Terminali");
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& log : consoleLog) {
        if (log.find("[HATA]") != std::string::npos) ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "%s", log.c_str());
        else if (log.find("[UYARI]") != std::string::npos) ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "%s", log.c_str());
        else if (log.find("[BASARILI]") != std::string::npos) ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", log.c_str());
        else ImGui::TextUnformatted(log.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::Separator();
    bool reclaim_focus = false;
    ImGui::PushItemWidth(-60);
    if (ImGui::InputText("##Komut", consoleInput, IM_ARRAYSIZE(consoleInput), ImGuiInputTextFlags_EnterReturnsTrue)) {
        ProcessConsoleCommand(std::string(consoleInput));
        strcpy_s(consoleInput, ""); reclaim_focus = true;
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Gonder") || reclaim_focus) ImGui::SetKeyboardFocusHere(-1);
    ImGui::End();
}

// =============================================================
// ANA UYGULAMA (MAIN)
// =============================================================
int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1366, 768, "MySaaS - Super Admin Arayuzu", NULL, NULL);
    if (!window) return -1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    consoleLog.push_back("[SISTEM] Moduller yuklendi. Terminal hazir. Komutlari gormek icin 'help' yazin.");

    FetchUsersAsync();
    FetchServersAsync();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        DrawDashboard();
        DrawUserManagement();
        DrawServerManagement();
        DrawConsole();

        DrawBanListModal();
        DrawUserStatsModal();
        DrawServerStatsModal();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
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