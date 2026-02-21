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
#include <nlohmann/json.hpp> 

using json = nlohmann::json;

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
#include <cmath> 

// --- UYGULAMA DURUMU VE OTOMATİK KİMLİK (SUPER ADMIN BYPASS) ---
std::string jwtToken = "mock-jwt-token-aB3dE7xY9Z1kL0m"; // Otomatik yetki anahtarı
std::string loggedInUserId = "aB3dE7xY9Z1kL0m";
const std::string API_BASE_URL = "http://localhost:8080/api";

// --- SİSTEM DONANIM DEĞİŞKENLERİ ---
int time_offset = 0;
double last_update_time = 0.0;
float app_cpu_usage = 0.0f, app_ram_usage_mb = 0.0f, db_size_mb = 0.0f;
float app_cpu_graph[90] = { 0 }, app_ram_graph[90] = { 0 }, db_size_graph[90] = { 0 };
float current_cpu = 0.0f, current_ram_used_gb = 0.0f, current_ram_total_gb = 0.0f, current_ram_percent = 0.0f;
float current_disk_used_gb = 0.0f, current_disk_total_gb = 0.0f, current_disk_percent = 0.0f;

// --- SUNUCU KONTROL DEĞİŞKENLERİ VE PIPE MİMARİSİ ---
bool is_server_running = false;
std::string server_uptime_str = "00:00:00";
std::vector<std::string> http_traffic_log;
std::mutex httpLogMutex;
HANDLE g_hChildStd_OUT_Rd = NULL;

static FILETIME prevSysIdle = { 0 }, prevSysKernel = { 0 }, prevSysUser = { 0 };
static FILETIME prevProcKernel = { 0 }, prevProcUser = { 0 }, prevProcSysKernel = { 0 }, prevProcSysUser = { 0 };
static bool firstProcRun = true;

// --- API VERİ MODELLERİ VE DEĞİŞKENLERİ ---
struct AdminUser { std::string id, name, email, role, status, sub_level, created_at; };
struct AdminServer { std::string id, name, owner_id; int member_count = 0; };
struct UserStatServer { std::string id, name, owner_id; };
struct UserStatFriend { std::string id, name, email; };
struct UserStatPayment { std::string id, status; float amount; };
struct ActiveServerMember { std::string id, name, status; };
struct ServerLogData { std::string time, action, details; };

std::vector<AdminUser> userList;
std::vector<AdminServer> serverList;
std::vector<UserStatServer> activeStatsServers;
std::vector<UserStatFriend> activeStatsFriends;
std::vector<UserStatPayment> activeStatsPayments;
std::vector<ActiveServerMember> activeServerMembersList;
std::vector<ServerLogData> activeServerLogsList;

std::mutex userListMutex, serverListMutex, statsMutex;
std::atomic<bool> isFetchingUsers(false), isFetchingServers(false), isFetchingStats(false), isFetchingServerStats(false);

char consoleInput[256] = "";
std::vector<std::string> consoleLog;

// --- PENCERE GÖRÜNÜRLÜK DURUMLARI ---
bool show_server_control = true;
bool show_dashboard = true;
bool show_user_management = true;
bool show_server_management = true;
bool show_terminal = true;
bool show_ban_list = false;
bool show_user_stats = false;
bool show_server_stats = false;
std::string active_stats_id = "";

// =============================================================
// YARDIMCI FONKSİYONLAR (SUNUCU VE DONANIM)
// =============================================================
DWORD GetBackendProcessId() {
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

void ReadPipeThread() {
    DWORD dwRead;
    CHAR chBuf[1024];
    std::string bufferStr = "";
    while (true) {
        bool bSuccess = ReadFile(g_hChildStd_OUT_Rd, chBuf, sizeof(chBuf) - 1, &dwRead, NULL);
        if (!bSuccess || dwRead == 0) break;

        chBuf[dwRead] = '\0';
        bufferStr += chBuf;

        size_t pos;
        while ((pos = bufferStr.find('\n')) != std::string::npos) {
            std::string line = bufferStr.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            std::lock_guard<std::mutex> lock(httpLogMutex);
            http_traffic_log.push_back(line);

            if (http_traffic_log.size() > 1000) http_traffic_log.erase(http_traffic_log.begin());
            bufferStr.erase(0, pos + 1);
        }
    }
    CloseHandle(g_hChildStd_OUT_Rd);
    g_hChildStd_OUT_Rd = NULL;
}

void StartBackendServer() {
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    HANDLE hChildStd_OUT_Wr = NULL;

    if (!CreatePipe(&g_hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &saAttr, 0)) {
        std::lock_guard<std::mutex> lock(httpLogMutex);
        http_traffic_log.push_back("[HATA] Iletisim borusu (Pipe) olusturulamadi.");
        return;
    }

    SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    si.cb = sizeof(STARTUPINFOA);
    si.hStdError = hChildStd_OUT_Wr;
    si.hStdOutput = hChildStd_OUT_Wr;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;

    if (CreateProcessA("MySaaSApp.exe", NULL, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        CloseHandle(hChildStd_OUT_Wr);
        is_server_running = true;

        std::thread(ReadPipeThread).detach();
        std::lock_guard<std::mutex> lock(httpLogMutex);
        http_traffic_log.push_back("[SISTEM] MySaaSApp.exe Arka planda basariyla baslatildi. Port: 8080");
    }
    else {
        std::lock_guard<std::mutex> lock(httpLogMutex);
        http_traffic_log.push_back("[HATA] Sunucu baslatilamadi! MySaaSApp.exe ayni klasorde mi?");
    }
}

void StopBackendServer() {
    DWORD pid = GetBackendProcessId();
    if (pid != 0) {
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProc) {
            TerminateProcess(hProc, 0);
            CloseHandle(hProc);
            std::lock_guard<std::mutex> lock(httpLogMutex);
            http_traffic_log.push_back("[SISTEM] Sunucu zorla kapatildi ve baglanti koptu.");
            is_server_running = false;
        }
    }
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
            if (std::isnan(current_cpu) || std::isinf(current_cpu)) current_cpu = 0.0f;
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
        is_server_running = true;
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (hProc) {
            PROCESS_MEMORY_COUNTERS pmc;
            if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) app_ram_usage_mb = pmc.WorkingSetSize / (1024.0f * 1024.0f);
            FILETIME cT, eT, kT, uT;
            if (GetProcessTimes(hProc, &cT, &eT, &kT, &uT)) {
                FILETIME ftNow; GetSystemTimeAsFileTime(&ftNow);
                ULARGE_INTEGER uct, unow;
                uct.LowPart = cT.dwLowDateTime; uct.HighPart = cT.dwHighDateTime;
                unow.LowPart = ftNow.dwLowDateTime; unow.HighPart = ftNow.dwHighDateTime;
                ULONGLONG diffSec = (unow.QuadPart - uct.QuadPart) / 10000000;
                int h = diffSec / 3600; int m = (diffSec % 3600) / 60; int s = diffSec % 60;
                char buf[64]; sprintf_s(buf, "%02d:%02d:%02d", h, m, s);
                server_uptime_str = buf;

                if (!firstProcRun && pid == prevPid) {
                    ULONGLONG sysKDiff = (((ULARGE_INTEGER*)&sysKernel)->QuadPart - ((ULARGE_INTEGER*)&prevProcSysKernel)->QuadPart);
                    ULONGLONG sysUDiff = (((ULARGE_INTEGER*)&sysUser)->QuadPart - ((ULARGE_INTEGER*)&prevProcSysUser)->QuadPart);
                    ULONGLONG procKDiff = (((ULARGE_INTEGER*)&kT)->QuadPart - ((ULARGE_INTEGER*)&prevProcKernel)->QuadPart);
                    ULONGLONG procUDiff = (((ULARGE_INTEGER*)&uT)->QuadPart - ((ULARGE_INTEGER*)&prevProcUser)->QuadPart);
                    ULONGLONG totalSys = sysKDiff + sysUDiff;
                    ULONGLONG totalProc = procKDiff + procUDiff;
                    if (totalSys > 0) app_cpu_usage = (float)((totalProc * 100.0) / totalSys); else app_cpu_usage = 0.0f;
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
    else {
        is_server_running = false; server_uptime_str = "00:00:00";
        app_cpu_usage = 0.0f; app_ram_usage_mb = 0.0f; prevPid = 0; firstProcRun = true;
    }

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
        cpr::Response r = cpr::Get(cpr::Url{ API_BASE_URL + "/admin/logs/system" }, cpr::Header{ {"Authorization", jwtToken} });
        if (r.status_code == 200) {
            try {
                auto j = json::parse(r.text, nullptr, false);
                if (!j.is_discarded() && j.is_array()) {
                    std::lock_guard<std::mutex> lock(userListMutex);
                    userList.clear();
                    for (const auto& item : j) {
                        AdminUser u;
                        u.id = item.value("id", "N/A");
                        u.name = item.value("name", "Unknown");
                        u.email = item.value("email", "N/A");
                        u.status = item.value("status", "Offline");
                        u.role = item.value("is_system_admin", 0) == 1 ? "System Admin" : "User";
                        int sub = item.value("subscription_level", 0);
                        u.sub_level = (sub == 2) ? "Enterprise" : (sub == 1) ? "Pro" : "Normal";
                        userList.push_back(u);
                    }
                    consoleLog.push_back("[BASARILI] Kullanicilar listelendi.");
                }
            }
            catch (...) { consoleLog.push_back("[HATA] JSON Parse Hatasi (Users)"); }
        }
        else { consoleLog.push_back("[HATA] API Baglantisi Yok. Sunucu Acik mi?"); }
        isFetchingUsers = false;
        }).detach();
}

void FetchServersAsync() {
    if (isFetchingServers) return;
    isFetchingServers = true;
    consoleLog.push_back("[SISTEM] Backend'den sunucu verileri cekiliyor...");

    std::thread([]() {
        cpr::Response r = cpr::Get(cpr::Url{ API_BASE_URL + "/servers" }, cpr::Header{ {"Authorization", jwtToken} });
        if (r.status_code == 200) {
            try {
                auto j = json::parse(r.text, nullptr, false);
                if (!j.is_discarded() && j.is_array()) {
                    std::lock_guard<std::mutex> lock(serverListMutex);
                    serverList.clear();
                    for (const auto& item : j) {
                        AdminServer s;
                        s.id = item.value("id", "N/A");
                        s.name = item.value("name", "Bilinmeyen Sunucu");
                        s.owner_id = item.value("owner_id", "N/A");
                        s.member_count = item.value("member_count", 0);
                        serverList.push_back(s);
                    }
                    consoleLog.push_back("[BASARILI] Sunucular listelendi.");
                }
            }
            catch (...) { consoleLog.push_back("[HATA] JSON Parse Hatasi (Servers)"); }
        }
        else {
            consoleLog.push_back("[HATA] API Baglantisi Yok Veya Yetkisiz!");
        }
        isFetchingServers = false;
        }).detach();
}

void FetchUserStatsAsync(std::string userId) {
    if (isFetchingStats) return;
    isFetchingStats = true;

    {
        std::lock_guard<std::mutex> lock(statsMutex);
        activeStatsServers.clear(); activeStatsFriends.clear(); activeStatsPayments.clear();
    }

    std::thread([userId]() {
        cpr::Response r = cpr::Get(cpr::Url{ API_BASE_URL + "/admin/users/" + userId + "/servers" },
            cpr::Header{ {"Authorization", jwtToken} });
        if (r.status_code == 200) {
            try {
                auto j = json::parse(r.text, nullptr, false);
                if (!j.is_discarded()) {
                    std::lock_guard<std::mutex> lock(statsMutex);
                    if (j.contains("servers") && j["servers"].is_array()) {
                        for (const auto& s : j["servers"]) {
                            activeStatsServers.push_back({
                                s.value("server_id", ""),
                                s.value("server_name", ""),
                                s.value("owner_id", "")
                                });
                        }
                    }
                }
            }
            catch (...) {}
        }
        isFetchingStats = false;
        }).detach();
}

void FetchServerStatsAsync(std::string serverId) {
    if (isFetchingServerStats) return;
    isFetchingServerStats = true;

    {
        std::lock_guard<std::mutex> lock(statsMutex);
        activeServerMembersList.clear();
        activeServerLogsList.clear();
    }

    std::thread([serverId]() {
        cpr::Response r = cpr::Get(cpr::Url{ API_BASE_URL + "/admin/servers/" + serverId + "/detailed_members" },
            cpr::Header{ {"Authorization", jwtToken} });
        if (r.status_code == 200) {
            try {
                auto j = json::parse(r.text, nullptr, false);
                if (!j.is_discarded()) {
                    std::lock_guard<std::mutex> lock(statsMutex);
                    if (j.contains("members") && j["members"].is_array()) {
                        for (const auto& m : j["members"]) {
                            activeServerMembersList.push_back({
                                m.value("user_id", ""),
                                m.value("name", ""),
                                m.value("status", "Offline")
                                });
                        }
                    }
                }
            }
            catch (...) {}
        }
        isFetchingServerStats = false;
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
        consoleLog.push_back("========== YONETIM KOMUTLARI ==========");
        consoleLog.push_back(" SUNUCU MOTORU: start | stop | restart");
        consoleLog.push_back(" PANEL: clear | refresh | uptime | sync_users | sync_servers");
        consoleLog.push_back(" KULLANICI: adduser <Ad_Soyad> <Email> <Sifre>");
        consoleLog.push_back(" KULLANICI: deluser <ID>");
        consoleLog.push_back(" KULLANICI: role <ID> [gun_sayisi]");
        consoleLog.push_back(" DENETIM: -banList | statsUser <ID> | statsServer <ID>");
        consoleLog.push_back("=======================================");
    }
    else if (action == "refresh") { FetchUsersAsync(); FetchServersAsync(); }
    else if (action == "uptime") {
        if (is_server_running) consoleLog.push_back("[BILGI] Sunucu Uptime Suresi: " + server_uptime_str);
        else consoleLog.push_back("[UYARI] Sunucu su an KAPALI.");
    }
    else if (action == "sync_users") { FetchUsersAsync(); }
    else if (action == "sync_servers") { FetchServersAsync(); }
    else if (action == "start") { if (!is_server_running) StartBackendServer(); }
    else if (action == "stop") { if (is_server_running) StopBackendServer(); }
    else if (action == "restart") {
        std::thread([]() {
            if (is_server_running) StopBackendServer();
            std::this_thread::sleep_for(std::chrono::seconds(2));
            StartBackendServer();
            }).detach();
    }
    else if (action == "-banList") { show_ban_list = true; }
    else if (action == "statsUser") {
        if (args.size() < 2) return;
        active_stats_id = args[1]; show_user_stats = true;
        FetchUserStatsAsync(active_stats_id);
    }
    else if (action == "statsServer") {
        if (args.size() < 2) return;
        active_stats_id = args[1]; show_server_stats = true;
        FetchServerStatsAsync(active_stats_id);
    }
    else { consoleLog.push_back("[HATA] Gecersiz komut."); }
}

void DrawMainMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Paneller")) {
            ImGui::MenuItem("Sunucu Kontrol Merkezi", NULL, &show_server_control);
            ImGui::MenuItem("Sistem Monitoru (Canli Veri)", NULL, &show_dashboard);
            ImGui::MenuItem("Kullanici Yonetimi (CRM)", NULL, &show_user_management);
            ImGui::MenuItem("Sunucu Yonetimi", NULL, &show_server_management);
            ImGui::MenuItem("Super Admin Terminali", NULL, &show_terminal);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

// =============================================================
// ESNEK ARAYÜZ ÇİZİM FONKSİYONLARI VE PANELLER
// =============================================================

void DrawServerControlPanel() {
    if (!show_server_control) return;
    ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(450, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin(">> SUNUCU KONTROL MERKEZI <<", &show_server_control);
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Backend Motoru: MySaaSApp.exe (Gizli Mod)");
    ImGui::Separator();
    ImGui::Text("Sunucu Durumu: "); ImGui::SameLine();
    if (is_server_running) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "AKTIF (RUNNING)");
        ImGui::Text("Acik Kalma Suresi (Uptime): %s", server_uptime_str.c_str());
    }
    else {
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "KAPALI (STOPPED)");
        ImGui::Text("Acik Kalma Suresi (Uptime): 00:00:00 (Sifirlandi)");
    }
    ImGui::Spacing();
    if (ImGui::Button("Sunucuyu Baslat (Start)", ImVec2(180, 30))) { if (!is_server_running) StartBackendServer(); }
    ImGui::SameLine();
    if (ImGui::Button("Sunucuyu Durdur (Stop)", ImVec2(180, 30))) { if (is_server_running) StopBackendServer(); }
    ImGui::Spacing(); ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Canli CMD Ekran Loglari (Gelen Veriler)");
    ImGui::Separator();

    ImGui::BeginChild("HTTPLogRegion", ImVec2(0, 0), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> lock(httpLogMutex);
        for (const auto& log : http_traffic_log) {
            if (log.find("401") != std::string::npos || log.find("404") != std::string::npos || log.find("500") != std::string::npos || log.find("[HATA]") != std::string::npos || log.find("error") != std::string::npos)
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "%s", log.c_str());
            else if (log.find("200") != std::string::npos || log.find("201") != std::string::npos || log.find("Basariyla") != std::string::npos || log.find("[BASARILI]") != std::string::npos)
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", log.c_str());
            else if (log.find("POST") != std::string::npos)
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%s", log.c_str());
            else if (log.find("[INFO]") != std::string::npos || log.find("GET") != std::string::npos)
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", log.c_str());
            else
                ImGui::TextUnformatted(log.c_str());
        }
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

void DrawConsole() {
    if (!show_terminal) return;
    ImGui::SetNextWindowPos(ImVec2(10, 340), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(450, 415), ImGuiCond_FirstUseEver);
    ImGui::Begin(">> SUPER ADMIN TERMINALI <<", &show_terminal);

    ImGui::BeginChild("ScrollingRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& log : consoleLog) {
        if (log.find("[HATA]") != std::string::npos) ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "%s", log.c_str());
        else if (log.find("[UYARI]") != std::string::npos) ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "%s", log.c_str());
        else if (log.find("[BASARILI]") != std::string::npos || log.find("[BILGI]") != std::string::npos) ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", log.c_str());
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

void DrawServerManagement() {
    if (!show_server_management) return;
    ImGui::SetNextWindowPos(ImVec2(470, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(880, 200), ImGuiCond_FirstUseEver);
    ImGui::Begin(">> SUNUCU (SERVER) YONETIMI <<", &show_server_management);
    if (ImGui::Button("Sunuculari Yenile (Sync)")) FetchServersAsync();
    if (isFetchingServers) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 1, 0, 1), "  Yukleniyor..."); }
    ImGui::Spacing();
    if (ImGui::BeginTable("ServersTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("ID"); ImGui::TableSetupColumn("Sunucu Adi"); ImGui::TableSetupColumn("Kurucu ID");
        ImGui::TableSetupColumn("Uye Sayisi"); ImGui::TableSetupColumn("Islemler");
        ImGui::TableHeadersRow();
        std::lock_guard<std::mutex> lock(serverListMutex);
        for (int i = 0; i < serverList.size(); i++) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(std::string("s_id_" + std::to_string(i)).c_str());
            ImGui::Selectable(serverList[i].id.c_str());
            if (ImGui::BeginPopupContextItem()) { if (ImGui::Selectable("Sunucu ID Kopyala")) ImGui::SetClipboardText(serverList[i].id.c_str()); ImGui::EndPopup(); }
            ImGui::PopID();

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(std::string("s_name_" + std::to_string(i)).c_str());
            ImGui::Selectable(serverList[i].name.c_str());
            if (ImGui::BeginPopupContextItem()) { if (ImGui::Selectable("Sunucu Adini Kopyala")) ImGui::SetClipboardText(serverList[i].name.c_str()); ImGui::EndPopup(); }
            ImGui::PopID();

            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(std::string("s_owner_" + std::to_string(i)).c_str());
            ImGui::Selectable(serverList[i].owner_id.c_str());
            if (ImGui::BeginPopupContextItem()) { if (ImGui::Selectable("Kurucu ID Kopyala")) ImGui::SetClipboardText(serverList[i].owner_id.c_str()); ImGui::EndPopup(); }
            ImGui::PopID();

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d Kayitli Uye", serverList[i].member_count);

            ImGui::TableSetColumnIndex(4);
            std::string btnLabel = "Incele##S" + std::to_string(i);
            if (ImGui::Button(btnLabel.c_str())) { ProcessConsoleCommand("statsServer " + serverList[i].id); }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DrawUserManagement() {
    if (!show_user_management) return;
    ImGui::SetNextWindowPos(ImVec2(470, 240), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(880, 250), ImGuiCond_FirstUseEver);
    ImGui::Begin(">> KULLANICI YONETIMI (CRM) <<", &show_user_management);
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
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(std::string("u_id_" + std::to_string(i)).c_str());
            ImGui::Selectable(userList[i].id.c_str());
            if (ImGui::BeginPopupContextItem()) { if (ImGui::Selectable("ID'yi Kopyala")) ImGui::SetClipboardText(userList[i].id.c_str()); ImGui::EndPopup(); }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(std::string("u_name_" + std::to_string(i)).c_str());
            ImGui::Selectable(userList[i].name.c_str());
            if (ImGui::BeginPopupContextItem()) { if (ImGui::Selectable("Ismi Kopyala")) ImGui::SetClipboardText(userList[i].name.c_str()); ImGui::EndPopup(); }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(std::string("u_email_" + std::to_string(i)).c_str());
            ImGui::Selectable(userList[i].email.c_str());
            if (ImGui::BeginPopupContextItem()) { if (ImGui::Selectable("E-Postayi Kopyala")) ImGui::SetClipboardText(userList[i].email.c_str()); ImGui::EndPopup(); }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(3);
            if (userList[i].status == "Online") ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Cevrimici");
            else ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Cevrimdisi");
            ImGui::TableSetColumnIndex(4);
            if (userList[i].sub_level == "Enterprise") ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Enterprise");
            else if (userList[i].sub_level == "Pro") ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "Pro");
            else ImGui::Text("%s", userList[i].sub_level.c_str());
            ImGui::TableSetColumnIndex(5); ImGui::Text("%s", userList[i].role.c_str());
            ImGui::TableSetColumnIndex(6);
            std::string btnLabel = "Islem##U" + std::to_string(i);
            if (ImGui::Button(btnLabel.c_str())) { ProcessConsoleCommand("statsUser " + userList[i].id); }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DrawDashboard() {
    if (!show_dashboard) return;
    ImGui::SetNextWindowPos(ImVec2(470, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(880, 255), ImGuiCond_FirstUseEver);
    ImGui::Begin(">> SISTEM MONITORU (CANLI VERI) <<", &show_dashboard);

    int total_users = 0, online_users = 0;
    {
        std::lock_guard<std::mutex> lock(userListMutex);
        total_users = userList.size();
        for (const auto& u : userList) { if (u.status == "Online") online_users++; }
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
    ImGui::Text("Toplam Sunucu Sayisi");
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d Adet", (int)serverList.size());
    ImGui::NextColumn();
    ImGui::Columns(1);
    ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "MySaaSApp.exe (Sunucu) Anlik Kaynak Tuketimi");
    ImGui::Separator(); ImGui::Spacing();
    char appCpuOverlay[64]; sprintf_s(appCpuOverlay, "Sunucu CPU: %.2f%%", app_cpu_usage);
    ImGui::PlotLines("##AppCPU", app_cpu_graph, 90, time_offset, appCpuOverlay, 0.0f, 100.0f, ImVec2(ImGui::GetContentRegionAvail().x, 40));
    char appRamOverlay[64]; sprintf_s(appRamOverlay, "Sunucu RAM: %.2f MB", app_ram_usage_mb);
    ImGui::PlotLines("##AppRAM", app_ram_graph, 90, time_offset, appRamOverlay, 0.0f, FLT_MAX, ImVec2(ImGui::GetContentRegionAvail().x, 40));
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

void DrawBanListModal() {
    if (!show_ban_list) return;
    ImGui::SetNextWindowPos(ImVec2(1366 / 2 - 350, 768 / 2 - 200), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(700, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Sistem Yasaklari (Ban Listesi)", &show_ban_list)) {
        if (ImGui::BeginTable("BanTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Kullanici ID"); ImGui::TableSetupColumn("E-Posta");
            ImGui::TableSetupColumn("Ad Soyad"); ImGui::TableSetupColumn("Sure");
            ImGui::TableSetupColumn("Sebep"); ImGui::TableSetupColumn("Tarih");
            ImGui::TableHeadersRow();
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void DrawUserStatsModal() {
    if (!show_user_stats) return;
    ImGui::SetNextWindowPos(ImVec2(1366 / 2 - 400, 768 / 2 - 250), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    std::string title = "Kullanici Istihbarati: " + active_stats_id;
    if (ImGui::Begin(title.c_str(), &show_user_stats)) {
        if (isFetchingStats) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Veriler sunucudan cekiliyor, lutfen bekleyin...");
        }
        else {
            std::lock_guard<std::mutex> lock(statsMutex);
            if (ImGui::BeginTabBar("UserStatsTabs")) {
                if (ImGui::BeginTabItem("Aktif Sunucular")) {
                    if (activeStatsServers.empty()) { ImGui::Text("Kullanicinin kurdugu veya katildigi bir sunucu bulunmuyor."); }
                    else {
                        if (ImGui::BeginTable("US_ServersTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                            ImGui::TableSetupColumn("Sunucu ID"); ImGui::TableSetupColumn("Sunucu Adi"); ImGui::TableSetupColumn("Durumu");
                            ImGui::TableHeadersRow();
                            for (const auto& s : activeStatsServers) {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", s.id.c_str());
                                ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", s.name.c_str());
                                ImGui::TableSetColumnIndex(2);
                                if (s.owner_id == active_stats_id) ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Kurucu (Sahip)");
                                else ImGui::Text("Normal Uye");
                            }
                            ImGui::EndTable();
                        }
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Arkadaslar & DM Kayitlari")) {
                    if (activeStatsFriends.empty()) { ImGui::Text("Kullanicinin ekli arkadasi bulunmuyor."); }
                    else {
                        if (ImGui::BeginTable("US_FriendsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                            ImGui::TableSetupColumn("Arkadas ID"); ImGui::TableSetupColumn("Ad Soyad"); ImGui::TableSetupColumn("E-Posta");
                            ImGui::TableHeadersRow();
                            for (const auto& f : activeStatsFriends) {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", f.id.c_str());
                                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", f.name.c_str());
                                ImGui::TableSetColumnIndex(2); ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", f.email.c_str());
                            }
                            ImGui::EndTable();
                        }
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Odeme Gecmisi")) {
                    if (activeStatsPayments.empty()) { ImGui::Text("Sistemde bu kullaniciya ait odeme kaydi bulunamadi."); }
                    else {
                        if (ImGui::BeginTable("US_PaymentsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                            ImGui::TableSetupColumn("Islem ID"); ImGui::TableSetupColumn("Tutar"); ImGui::TableSetupColumn("Durum");
                            ImGui::TableHeadersRow();
                            for (const auto& p : activeStatsPayments) {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", p.id.c_str());
                                ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%.2f", p.amount);
                                ImGui::TableSetColumnIndex(2);
                                if (p.status == "success") ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Basarili");
                                else ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "%s", p.status.c_str());
                            }
                            ImGui::EndTable();
                        }
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
    }
    ImGui::End();
}

void DrawServerStatsModal() {
    if (!show_server_stats) return;
    ImGui::SetNextWindowPos(ImVec2(1366 / 2 - 400, 768 / 2 - 300), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    std::string title = "Sunucu Gozetimi (Canli): " + active_stats_id;
    if (ImGui::Begin(title.c_str(), &show_server_stats)) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "DIKKAT: Yonetici ayricaligi ile ozel sunucu loglari goruntuleniyor.");
        ImGui::Separator();

        if (isFetchingServerStats) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Sunucu bilgileri cekiliyor, lutfen bekleyin...");
        }
        else {
            ImGui::Columns(2, "ServerColumns"); ImGui::SetColumnWidth(0, 300);

            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Sunucu Uyeleri");
            ImGui::BeginChild("ServerMembersRegion", ImVec2(0, 0), ImGuiChildFlags_Border);
            {
                std::lock_guard<std::mutex> lock(statsMutex);
                if (activeServerMembersList.empty()) {
                    ImGui::Text("Sunucuda uye bulunmuyor.");
                }
                else {
                    if (ImGui::BeginTable("SrvMembersTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("Isim"); ImGui::TableSetupColumn("Durum");
                        ImGui::TableHeadersRow();
                        for (const auto& m : activeServerMembersList) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", m.name.c_str());
                            ImGui::TableSetColumnIndex(1);
                            if (m.status == "Online") ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Online");
                            else ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Offline");
                        }
                        ImGui::EndTable();
                    }
                }
            }
            ImGui::EndChild();

            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Sunucu Islem Loglari (Gozetim)");
            ImGui::BeginChild("MessagesRegion", ImVec2(0, 0), ImGuiChildFlags_Border);
            {
                std::lock_guard<std::mutex> lock(statsMutex);
                if (activeServerLogsList.empty()) {
                    ImGui::Text("Bu sunucuda henuz bir islem kaydi yok.");
                }
                else {
                    if (ImGui::BeginTable("SrvLogsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
                        ImGui::TableSetupColumn("Tarih", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                        ImGui::TableSetupColumn("Aksiyon", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                        ImGui::TableSetupColumn("Detaylar");
                        ImGui::TableHeadersRow();
                        for (const auto& log : activeServerLogsList) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0); ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", log.time.c_str());
                            ImGui::TableSetColumnIndex(1);
                            if (log.action == "KANAL_SILINDI") ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "%s", log.action.c_str());
                            else ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%s", log.action.c_str());
                            ImGui::TableSetColumnIndex(2); ImGui::TextWrapped("%s", log.details.c_str());
                        }
                        ImGui::EndTable();
                    }
                }
            }
            ImGui::EndChild();
            ImGui::Columns(1);
        }
    }
    ImGui::End();
}

// ==============================================================
// GLFW HATA YAKALAYICI
// ==============================================================
static void glfw_error_callback(int error, const char* description) {
    std::cerr << "[GLFW HATA] (" << error << "): " << description << std::endl;
}

// ==============================================================
// ANA UYGULAMA (MAIN)
// ==============================================================
int main() {
    glfwSetErrorCallback(glfw_error_callback);

    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1366, 768, "MySaaS - Super Admin Arayuzu", NULL, NULL);
    if (!window) return -1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.15f, 0.3f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.1f, 0.3f, 0.6f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.98f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    consoleLog.push_back("[SISTEM] Super Admin oturumu otomatik olarak acildi (Bypass Aktif).");
    consoleLog.push_back("[SISTEM] Moduller yuklendi. Terminal hazir. Komutlari gormek icin 'help' yazin.");

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ARTIK DOĞRUDAN DASHBOARD (KONTROL PANELİ) ÇİZİLİYOR
        DrawMainMenuBar();

        double current_time = ImGui::GetTime();
        if (current_time - last_update_time > 0.5) {
            UpdateHardwareMetrics();
            app_cpu_graph[time_offset] = app_cpu_usage;
            app_ram_graph[time_offset] = app_ram_usage_mb;
            db_size_graph[time_offset] = db_size_mb;
            time_offset = (time_offset + 1) % 90;
            last_update_time = current_time;
        }

        DrawServerControlPanel();
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

    if (is_server_running) StopBackendServer();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}