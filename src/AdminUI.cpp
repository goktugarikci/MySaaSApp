#pragma warning(disable : 4996) // Zaman/Tarih fonksiyonları uyarılarını kapatır

// =============================================================
// 1. WINSOCK & WINDOWS API (EN ÜSTTE OLMALIDIR)
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
// 2. AĞ, JSON VE STANDART KÜTÜPHANELER
// =============================================================
#include <cpr/cpr.h>
#include <nlohmann/json.hpp> 

using json = nlohmann::json;

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
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
#include <ctime>
#include <algorithm>
#include <cctype>
#include <fstream> // HTML ve CSV dışa aktarım için

// =============================================================
// GLOBAL DEĞİŞKENLER VE DURUMLAR
// =============================================================
std::string jwtToken = "mock-jwt-token-aB3dE7xY9Z1kL0m"; // Sistem yöneticisi bypass anahtarı
const std::string API_BASE_URL = "http://localhost:8080";

int time_offset = 0;
double last_update_time = 0.0; double last_sync_time = 0.0;
float app_cpu_usage = 0.0f, app_ram_usage_mb = 0.0f, db_size_mb = 0.0f;
float app_cpu_graph[90] = { 0 }, app_ram_graph[90] = { 0 }, db_size_graph[90] = { 0 };
float current_cpu = 0.0f, current_ram_used_gb = 0.0f, current_ram_total_gb = 0.0f, current_ram_percent = 0.0f;
float current_disk_used_gb = 0.0f, current_disk_total_gb = 0.0f, current_disk_percent = 0.0f;

bool is_server_running = false;
std::string server_uptime_str = "00:00:00";
std::vector<std::string> http_traffic_log; std::mutex httpLogMutex;
HANDLE g_hChildStd_OUT_Rd = NULL;

static FILETIME prevSysIdle = { 0 }, prevSysKernel = { 0 }, prevSysUser = { 0 };
static FILETIME prevProcKernel = { 0 }, prevProcUser = { 0 }, prevProcSysKernel = { 0 }, prevProcSysUser = { 0 };
static bool firstProcRun = true;

// --- VERİ MODELLERİ ---
struct AdminUser { std::string id, name, email, role, status, sub_level; };
struct AdminServer { std::string id, name, owner_id; int member_count = 0; };
struct UserStatServer { std::string id, name, owner_id; };
struct UserStatFriend { std::string id, name, email; };
struct UserStatPayment { std::string id, status; float amount; };
struct ActiveServerMember { std::string id, name, status; };
struct ServerLogData { std::string time, action, details; };
struct BannedUser { std::string user_id, reason, date; };

std::vector<AdminUser> userList; std::vector<AdminServer> serverList;
std::vector<UserStatServer> activeStatsServers; std::vector<UserStatFriend> activeStatsFriends;
std::vector<UserStatPayment> activeStatsPayments; std::vector<ActiveServerMember> activeServerMembersList;
std::vector<ServerLogData> activeServerLogsList; std::vector<BannedUser> banList;

std::mutex userListMutex, serverListMutex, statsMutex, banListMutex, consoleLogMutex;
std::atomic<bool> isFetchingUsers(false), isFetchingServers(false), isFetchingStats(false), isFetchingServerStats(false), isFetchingBans(false);

char consoleInput[256] = ""; std::vector<std::string> consoleLog;

// --- MODÜLER ARAYÜZ DURUMLARI ---
bool show_server_control = true; bool show_dashboard = true;
bool show_user_management = true; bool show_server_management = true;
bool show_console_window = true; bool show_api_tester_window = false;
bool show_ban_list = false; bool show_user_stats = false; bool show_server_stats = false;
std::string active_stats_id = "";

// =============================================================
// MUHTEŞEM ENDPOINT KÜTÜPHANESİ (SİSTEMDEKİ 55+ ENDPOINT BURADA)
// =============================================================
int api_method_idx = 0;
const char* api_methods[] = { "GET", "POST", "PUT", "DELETE" };
char api_url_buffer[256] = "/api/users/me";
char api_body_buffer[4096] = "{\n  \n}";
char api_response_buffer[8192] = "Istek gondermek icin hazir.";
int api_last_status = 0; std::atomic<bool> is_api_loading(false);
std::mutex api_response_mutex; std::string thread_response_temp; bool new_response_ready = false;

struct EndpointDef { const char* method; const char* url; const char* desc; const char* body; };
EndpointDef predefined_endpoints[] = {
    // --- 1. KİMLİK & AUTH (AuthRoutes) ---
    {"POST", "/api/auth/login", "Giris Yap (Login) -> Token Doner", "{\n  \"email\": \"admin@mysaas.com\",\n  \"password\": \"admin123\"\n}"},
    {"POST", "/api/auth/register", "Yeni Kayit Ol", "{\n  \"name\": \"Yeni Uye\",\n  \"email\": \"test@test.com\",\n  \"password\": \"123456\"\n}"},
    {"POST", "/api/auth/forgot-password", "Sifremi Unuttum Kodu Gonder", "{\n  \"email\": \"admin@mysaas.com\"\n}"},
    {"POST", "/api/auth/reset-password", "Sifreyi Sifirla", "{\n  \"token\": \"TOKEN_KODU\",\n  \"new_password\": \"yeni123\"\n}"},

    // --- 2. KULLANICI & PROFİL (UserRoutes) ---
    {"GET", "/api/users/me", "Kendi Profilimi Getir", ""},
    {"PUT", "/api/users/me", "Profilimi Guncelle", "{\n  \"name\": \"Super Admin\",\n  \"status\": \"Online\"\n}"},
    {"DELETE", "/api/users/me", "Hesabimi Tamamen Sil", ""},
    {"PUT", "/api/users/me/avatar", "Avatar URL'sini Guncelle", "{\n  \"avatar_url\": \"https://sitem.com/resim.png\"\n}"},
    {"GET", "/api/users/search?q=Admin", "Kullanici Ara (Min 3 Karakter)", ""},
    {"POST", "/api/user/ping", "Aktiflik Bildir (Ping - Son Gorulme)", ""},
    {"POST", "/api/user/status", "Manuel Durum Degistir", "{\n  \"status\": \"Rahatsiz Etmeyin\"\n}"},

    // --- 3. ARKADAŞLIK & ENGEL (UserRoutes) ---
    {"GET", "/api/friends", "Arkadas Listesini Getir", ""},
    {"GET", "/api/friends/requests", "Gelen Arkadaslik Istekleri", ""},
    {"POST", "/api/friends/request", "Arkadaslik Istegi Gonder", "{\n  \"target_id\": \"HEDEF_ID\"\n}"},
    {"PUT", "/api/friends/requests/REQ_ID", "Istegi Kabul / Red Et", "{\n  \"status\": \"accepted\"\n}"},
    {"DELETE", "/api/friends/FRIEND_ID", "Arkadasliktan Cikar", ""},
    {"GET", "/api/friends/blocks", "Engellenenleri Listele", ""},
    {"POST", "/api/friends/blocks", "Kullaniciyi Engelle", "{\n  \"target_id\": \"HEDEF_ID\"\n}"},
    {"DELETE", "/api/friends/blocks/TARGET_ID", "Kullanici Engelini Kaldir", ""},

    // --- 4. BİLDİRİM & ÖZEL MESAJ - DM (UserRoutes) ---
    {"GET", "/api/users/me/server-invites", "Bekleyen Sunucu Davetleri", ""},
    {"GET", "/api/notifications", "Genel Bildirimleri Getir", ""},
    {"PUT", "/api/notifications/NOTIF_ID/read", "Bildirimi Okundu Isaretle", ""},
    {"POST", "/api/users/dm", "Ozel Mesaj (DM) Kanali Baslat", "{\n  \"target_id\": \"HEDEF_ID\"\n}"},
    {"DELETE", "/api/users/dm/CHANNEL_ID", "DM Gecmisini ve Kanali Sil", ""},

    // --- 5. SUNUCU & YÖNETİM (ServerRoutes) ---
    {"GET", "/api/servers", "Katildigim Sunuculari Listele", ""},
    {"POST", "/api/servers", "Yeni Sunucu (Workspace) Olustur", "{\n  \"name\": \"SaaS Gelistirme Ekibi\"\n}"},
    {"PUT", "/api/servers/SERVER_ID", "Sunucu Adini Degistir (Kurucu)", "{\n  \"name\": \"Yeni Ad\"\n}"},
    {"DELETE", "/api/servers/SERVER_ID", "Sunucuyu Tamamen Sil (Kurucu)", ""},
    {"POST", "/api/servers/SERVER_ID/invites", "Sunucu Davet Linki Uret", ""},
    {"POST", "/api/servers/join/INVITE_CODE", "Davet Koduyla Sunucuya Katil", ""},
    {"DELETE", "/api/servers/SERVER_ID/leave", "Sunucudan Ayril", ""},
    {"DELETE", "/api/servers/SERVER_ID/members/USER_ID", "Uyeyi Sunucudan At (Kick)", ""},

    // --- 6. KANAL YÖNETİMİ (ServerRoutes) ---
    {"GET", "/api/servers/SERVER_ID/channels", "Sunucu Kanallarini Getir", ""},
    {"POST", "/api/servers/SERVER_ID/channels", "Yeni Kanal Ekle", "{\n  \"name\": \"genel-sohbet\",\n  \"type\": 1,\n  \"is_private\": false\n}"},
    {"PUT", "/api/channels/CHANNEL_ID", "Kanal Adini Degistir", "{\n  \"name\": \"duyurular\"\n}"},
    {"DELETE", "/api/channels/CHANNEL_ID", "Kanali Sil", ""},

    // --- 7. MESAJLAŞMA & THREAD & TEPKİLER (MessageRoutes) ---
    {"GET", "/api/channels/CHANNEL_ID/messages", "Kanal Mesajlarini Cek", ""},
    {"POST", "/api/channels/CHANNEL_ID/messages", "Kanala Mesaj Gonder (Medyali)", "{\n  \"content\": \"Merhaba!\",\n  \"attachment_url\": \"\"\n}"},
    {"PUT", "/api/messages/MSG_ID", "Mesaji Duzenle", "{\n  \"content\": \"Duzenlenmis mesaj\"\n}"},
    {"DELETE", "/api/messages/MSG_ID", "Kendi Mesajimi Sil", ""},
    {"POST", "/api/messages/MSG_ID/reactions", "Mesaja Emoji/Tepki Ekle", "{\n  \"reaction\": \"👍\"\n}"},
    {"DELETE", "/api/messages/MSG_ID/reactions/EMOJI", "Eklenen Emojiyi Geri Al", ""},
    {"GET", "/api/messages/MSG_ID/thread", "Mesajin Alt Yanitlarini (Thread) Getir", ""},
    {"POST", "/api/messages/MSG_ID/thread", "Mesaja Alt Yanit (Thread) Gonder", "{\n  \"content\": \"Bu mesaja katiliyorum.\"\n}"},

    // --- 8. KANBAN (TRELLO) PANOSU (KanbanRoutes) ---
    {"GET", "/api/boards/CHANNEL_ID", "Kanban Panosunu ve Listeleri Getir", ""},
    {"POST", "/api/boards/CHANNEL_ID/lists", "Yeni Sütun (Liste) Ekle", "{\n  \"title\": \"Yapilacaklar\"\n}"},
    {"PUT", "/api/lists/LIST_ID", "Sutun Adini / Pozisyonunu Degistir", "{\n  \"title\": \"Guncel Ad\",\n  \"position\": 1\n}"},
    {"DELETE", "/api/lists/LIST_ID", "Sutunu ve Icindeki Kartlari Sil", ""},
    {"POST", "/api/lists/LIST_ID/cards", "Sutuna Yeni Gorev Karti Ekle", "{\n  \"title\": \"Arayuz Fix\",\n  \"description\": \"Detay\",\n  \"priority\": 2\n}"},
    {"PUT", "/api/cards/CARD_ID", "Gorevi / Karti Duzenle", "{\n  \"title\": \"Guncel Baslik\",\n  \"description\": \"Yeni detay\",\n  \"priority\": 3\n}"},
    {"DELETE", "/api/cards/CARD_ID", "Gorev Kartini Sil", ""},
    {"PUT", "/api/cards/CARD_ID/move", "Gorevi Baska Sutuna Tasi (Drag&Drop)", "{\n  \"new_list_id\": \"HEDEF_SUTUN_ID\",\n  \"new_position\": 0\n}"},
    {"PUT", "/api/cards/CARD_ID/status", "Gorevi Tamamlandi Isaretle", "{\n  \"is_completed\": true\n}"},
    {"PUT", "/api/cards/CARD_ID/assign", "Goreve Sorumlu Kisi Ata (Assignee)", "{\n  \"assignee_id\": \"USER_ID\"\n}"},
    {"POST", "/api/cards/CARD_ID/comments", "Karta Yorum Ekle", "{\n  \"content\": \"Bu islem acildir!\"\n}"},
    {"DELETE", "/api/comments/COMMENT_ID", "Kart Yorumunu Sil", ""},

    // --- 9. WEBSOCKET (GERÇEK ZAMANLI) YÖNETİM ---
    {"GET", "ws://localhost:8080/ws/chat", "Genel Chat & Kanban Bildirim Soketi", ""},
    {"GET", "ws://localhost:8080/ws/video-call", "Goruntulu/Sesli Arama Sinyal Soketi", ""},

    // --- 10. YÖNETİM, ŞİKAYET & ÖDEME (AdminRoutes vs) ---
    {"POST", "/api/upload", "Dosya Yukle (Resim/PDF - Multipart Data)", ""},
    {"GET", "/api/admin/reports", "Aktif Kullanici Sikayetlerini Listele", ""},
    {"POST", "/api/reports", "Baskasini Sikayet Et", "{\n  \"content_id\": \"HEDEF_ID\",\n  \"type\": \"USER\",\n  \"reason\": \"Kufur/Hakaret\"\n}"},
    {"GET", "/api/admin/banlist", "Banlanmis Kullanici Listesi", ""},
    {"POST", "/api/payments/checkout", "Yeni Abonelik / Odeme Baslat", "{\n  \"amount\": 99.99,\n  \"currency\": \"TRY\"\n}"}
};

// =============================================================
// YARDIMCI FONKSİYONLAR
// =============================================================
void AddConsoleLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(consoleLogMutex);
    consoleLog.push_back(msg);
    if (consoleLog.size() > 500) consoleLog.erase(consoleLog.begin());
}

DWORD GetBackendProcessId() {
    PROCESSENTRY32 pe32 = { 0 }; pe32.dwSize = sizeof(PROCESSENTRY32);
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
    DWORD dwRead; CHAR chBuf[1024]; std::string bufferStr = "";
    while (true) {
        bool bSuccess = ReadFile(g_hChildStd_OUT_Rd, chBuf, sizeof(chBuf) - 1, &dwRead, NULL);
        if (!bSuccess || dwRead == 0) break;
        chBuf[dwRead] = '\0'; bufferStr += chBuf; size_t pos;
        while ((pos = bufferStr.find('\n')) != std::string::npos) {
            std::string line = bufferStr.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::lock_guard<std::mutex> lock(httpLogMutex);
            http_traffic_log.push_back(line);
            if (http_traffic_log.size() > 1000) http_traffic_log.erase(http_traffic_log.begin());
            bufferStr.erase(0, pos + 1);
        }
    }
    CloseHandle(g_hChildStd_OUT_Rd); g_hChildStd_OUT_Rd = NULL;
}

void StartBackendServer() {
    SECURITY_ATTRIBUTES saAttr; saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); saAttr.bInheritHandle = TRUE; saAttr.lpSecurityDescriptor = NULL;
    HANDLE hChildStd_OUT_Wr = NULL;
    if (!CreatePipe(&g_hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &saAttr, 0)) { AddConsoleLog("[HATA] Iletisim kanali olusturulamadi."); return; }
    SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si = { sizeof(STARTUPINFOA) }; si.cb = sizeof(STARTUPINFOA); si.hStdError = hChildStd_OUT_Wr; si.hStdOutput = hChildStd_OUT_Wr; si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    if (CreateProcessA("MySaaSApp.exe", NULL, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(hChildStd_OUT_Wr); is_server_running = true;
        std::thread(ReadPipeThread).detach();
        AddConsoleLog("[SISTEM] Sunucu basariyla baslatildi. Port: 8080");
    }
    else { AddConsoleLog("[HATA] Sunucu baslatilamadi! 'MySaaSApp.exe' bulunamadi."); }
}

void StopBackendServer() {
    DWORD pid = GetBackendProcessId();
    if (pid != 0) {
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProc) { TerminateProcess(hProc, 0); CloseHandle(hProc); AddConsoleLog("[SISTEM] Sunucu zorla kapatildi."); is_server_running = false; }
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
    current_ram_total_gb = memInfo.ullTotalPhys / (1024.0f * 1024.0f * 1024.0f); current_ram_used_gb = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024.0f * 1024.0f * 1024.0f); current_ram_percent = (current_ram_used_gb / current_ram_total_gb);

    ULARGE_INTEGER fba, tnb, tnfb;
    if (GetDiskFreeSpaceExA(NULL, &fba, &tnb, &tnfb)) { current_disk_total_gb = tnb.QuadPart / (1024.0f * 1024.0f * 1024.0f); current_disk_used_gb = current_disk_total_gb - (tnfb.QuadPart / (1024.0f * 1024.0f * 1024.0f)); current_disk_percent = (current_disk_used_gb / current_disk_total_gb); }

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
                ULARGE_INTEGER uct, unow; uct.LowPart = cT.dwLowDateTime; uct.HighPart = cT.dwHighDateTime; unow.LowPart = ftNow.dwLowDateTime; unow.HighPart = ftNow.dwHighDateTime;
                ULONGLONG diffSec = (unow.QuadPart - uct.QuadPart) / 10000000;
                int h = diffSec / 3600; int m = (diffSec % 3600) / 60; int s = diffSec % 60; char buf[64]; sprintf_s(buf, "%02d:%02d:%02d", h, m, s); server_uptime_str = buf;
                if (!firstProcRun && pid == prevPid) {
                    ULONGLONG sysKDiff = (((ULARGE_INTEGER*)&sysKernel)->QuadPart - ((ULARGE_INTEGER*)&prevProcSysKernel)->QuadPart); ULONGLONG sysUDiff = (((ULARGE_INTEGER*)&sysUser)->QuadPart - ((ULARGE_INTEGER*)&prevProcSysUser)->QuadPart); ULONGLONG procKDiff = (((ULARGE_INTEGER*)&kT)->QuadPart - ((ULARGE_INTEGER*)&prevProcKernel)->QuadPart); ULONGLONG procUDiff = (((ULARGE_INTEGER*)&uT)->QuadPart - ((ULARGE_INTEGER*)&prevProcUser)->QuadPart); ULONGLONG totalSys = sysKDiff + sysUDiff; ULONGLONG totalProc = procKDiff + procUDiff;
                    if (totalSys > 0) app_cpu_usage = (float)((totalProc * 100.0) / totalSys); else app_cpu_usage = 0.0f;
                    if (std::isnan(app_cpu_usage) || std::isinf(app_cpu_usage)) app_cpu_usage = 0.0f;
                }
                prevProcKernel = kT; prevProcUser = uT; prevProcSysKernel = sysKernel; prevProcSysUser = sysUser; firstProcRun = false;
            } CloseHandle(hProc);
        } prevPid = pid;
    }
    else { is_server_running = false; server_uptime_str = "00:00:00"; app_cpu_usage = 0.0f; app_ram_usage_mb = 0.0f; prevPid = 0; firstProcRun = true; }
    try { if (std::filesystem::exists("mysaasapp.db")) db_size_mb = std::filesystem::file_size("mysaasapp.db") / (1024.0f * 1024.0f); else db_size_mb = 0.0f; }
    catch (...) { db_size_mb = 0.0f; }
}

void FetchUsersAsync() { if (isFetchingUsers) return; isFetchingUsers = true; std::thread([]() { cpr::Response r = cpr::Get(cpr::Url{ API_BASE_URL + "/api/admin/users" }, cpr::Header{ {"Authorization", jwtToken} }); if (r.status_code == 200) { try { auto j = json::parse(r.text, nullptr, false); if (!j.is_discarded() && j.is_array()) { std::lock_guard<std::mutex> lock(userListMutex); userList.clear(); for (const auto& item : j) { AdminUser u; u.id = item.value("id", "N/A"); u.name = item.value("name", "Unknown"); u.email = item.value("email", "N/A"); u.status = item.value("status", "Offline"); u.role = item.value("is_system_admin", 0) == 1 ? "System Admin" : "User"; int sub = item.value("subscription_level", 0); u.sub_level = (sub == 2) ? "Enterprise" : (sub == 1) ? "Pro" : "Normal"; userList.push_back(u); } } } catch (...) {} } isFetchingUsers = false; }).detach(); }
void FetchServersAsync() { if (isFetchingServers) return; isFetchingServers = true; std::thread([]() { cpr::Response r = cpr::Get(cpr::Url{ API_BASE_URL + "/api/admin/servers" }, cpr::Header{ {"Authorization", jwtToken} }); if (r.status_code == 200) { try { auto j = json::parse(r.text, nullptr, false); if (!j.is_discarded() && j.is_array()) { std::lock_guard<std::mutex> lock(serverListMutex); serverList.clear(); for (const auto& item : j) { AdminServer s; s.id = item.value("id", "N/A"); s.name = item.value("name", "Unknown"); s.owner_id = item.value("owner_id", "N/A"); s.member_count = item.value("member_count", 0); serverList.push_back(s); } } } catch (...) {} } isFetchingServers = false; }).detach(); }
void FetchBanListAsync() { if (isFetchingBans) return; isFetchingBans = true; std::thread([]() { cpr::Response r = cpr::Get(cpr::Url{ API_BASE_URL + "/api/admin/banlist" }, cpr::Header{ {"Authorization", jwtToken} }); if (r.status_code == 200) { try { auto j = json::parse(r.text, nullptr, false); if (!j.is_discarded() && j.is_array()) { std::lock_guard<std::mutex> lock(banListMutex); banList.clear(); for (const auto& b : j) { banList.push_back({ b.value("user_id", ""), b.value("reason", ""), b.value("date", "") }); } } } catch (...) {} } isFetchingBans = false; }).detach(); }
void FetchUserStatsAsync(std::string userId) { if (isFetchingStats) return; isFetchingStats = true; { std::lock_guard<std::mutex> lock(statsMutex); activeStatsServers.clear(); activeStatsFriends.clear(); activeStatsPayments.clear(); } std::thread([userId]() { cpr::Response r = cpr::Get(cpr::Url{ API_BASE_URL + "/api/admin/users/" + userId + "/servers" }, cpr::Header{ {"Authorization", jwtToken} }); if (r.status_code == 200) { try { auto j = json::parse(r.text, nullptr, false); if (!j.is_discarded() && j.contains("servers") && j["servers"].is_array()) { std::lock_guard<std::mutex> lock(statsMutex); for (const auto& s : j["servers"]) { activeStatsServers.push_back({ s.value("server_id", ""), s.value("server_name", ""), s.value("owner_id", "") }); } } } catch (...) {} } isFetchingStats = false; }).detach(); }
void FetchServerStatsAsync(std::string serverId) { if (isFetchingServerStats) return; isFetchingServerStats = true; { std::lock_guard<std::mutex> lock(statsMutex); activeServerMembersList.clear(); activeServerLogsList.clear(); } std::thread([serverId]() { cpr::Response r = cpr::Get(cpr::Url{ API_BASE_URL + "/api/admin/servers/" + serverId + "/detailed_members" }, cpr::Header{ {"Authorization", jwtToken} }); if (r.status_code == 200) { try { auto j = json::parse(r.text, nullptr, false); if (!j.is_discarded() && j.contains("members") && j["members"].is_array()) { std::lock_guard<std::mutex> lock(statsMutex); for (const auto& m : j["members"]) { activeServerMembersList.push_back({ m.value("user_id", ""), m.value("name", ""), m.value("status", "Offline") }); } } } catch (...) {} } isFetchingServerStats = false; }).detach(); }

void SendApiRequest() {
    if (is_api_loading) return; is_api_loading = true; strcpy_s(api_response_buffer, "Islem sunucuya iletiliyor..."); api_last_status = 0;
    std::string method = api_methods[api_method_idx]; std::string endpoint = api_url_buffer; std::string body = api_body_buffer;
    if (endpoint.rfind("/", 0) != 0) endpoint = "/" + endpoint; std::string url = API_BASE_URL + endpoint;
    AddConsoleLog("[API ISTEGI] " + method + " " + endpoint);
    std::thread([method, url, body, endpoint]() {
        cpr::Response r; cpr::Header headers = { {"Authorization", jwtToken}, {"Content-Type", "application/json"} };
        if (method == "GET") r = cpr::Get(cpr::Url{ url }, headers); else if (method == "POST") r = cpr::Post(cpr::Url{ url }, headers, cpr::Body{ body }); else if (method == "PUT") r = cpr::Put(cpr::Url{ url }, headers, cpr::Body{ body }); else if (method == "DELETE") r = cpr::Delete(cpr::Url{ url }, headers);
        std::lock_guard<std::mutex> lock(api_response_mutex); api_last_status = r.status_code;
        try { auto j = nlohmann::json::parse(r.text); thread_response_temp = j.dump(4); }
        catch (...) { thread_response_temp = r.text.empty() ? "(Bos yanit)" : r.text; }
        if (r.status_code == 0) thread_response_temp = "Baglanti Hatasi: Sunucu kapali olabilir."; AddConsoleLog("[API YANIT] Status: " + std::to_string(api_last_status) + " -> " + endpoint); new_response_ready = true; is_api_loading = false;
        }).detach();
}

std::vector<std::string> ParseCommand(const std::string& cmd) { std::istringstream iss(cmd); std::vector<std::string> tokens; std::string token; while (iss >> token) tokens.push_back(token); return tokens; }

void ProcessConsoleCommand(const std::string& cmd) {
    if (cmd.empty()) return; AddConsoleLog("root@mysaas:~# " + cmd); auto args = ParseCommand(cmd); std::string action = args[0]; std::transform(action.begin(), action.end(), action.begin(), ::tolower);
    if (action == "clear") { std::lock_guard<std::mutex> lock(consoleLogMutex); consoleLog.clear(); }
    else if (action == "help") { AddConsoleLog("========== SISTEM KOMUTLARI =========="); AddConsoleLog(" SUNUCU  : start | stop | restart | uptime"); AddConsoleLog(" BACKUP  : backup [isim] (Orn: backup veri1)"); AddConsoleLog(" PANEL   : clear | refresh"); AddConsoleLog(" YONETIM : ban <ID> | unban <ID> | banlist"); AddConsoleLog(" GOZETIM : statsuser <ID> | statsserver <ID>"); }
    else if (action == "refresh" || action == "sync") { AddConsoleLog("[BILGI] Veriler yenileniyor..."); FetchUsersAsync(); FetchServersAsync(); FetchBanListAsync(); }
    else if (action == "uptime") { if (is_server_running) AddConsoleLog("[BILGI] Uptime: " + server_uptime_str); else AddConsoleLog("[UYARI] Sunucu KAPALI."); }
    else if (action == "start") { if (!is_server_running) StartBackendServer(); else AddConsoleLog("[BILGI] Sunucu zaten calisiyor."); }
    else if (action == "stop") { if (is_server_running) StopBackendServer(); else AddConsoleLog("[BILGI] Sunucu zaten kapali."); }
    else if (action == "restart") { std::thread([]() { if (is_server_running) StopBackendServer(); std::this_thread::sleep_for(std::chrono::seconds(2)); StartBackendServer(); }).detach(); }
    else if (action == "backup") { try { if (!std::filesystem::exists("backups")) std::filesystem::create_directory("backups"); std::string dest; if (args.size() > 1) { dest = "backups/" + args[1] + ".db"; } else { auto t = std::time(nullptr); auto tm = *std::localtime(&t); const char* days[] = { "Paz", "Pzt", "Sal", "Car", "Per", "Cum", "Cmt" }; char buf[256]; sprintf_s(buf, "guncelleme_oncesi_%02d-%02d-%04d_%02d-%02d_%s.db", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min, days[tm.tm_wday]); dest = "backups/" + std::string(buf); } if (std::filesystem::exists("mysaasapp.db")) { std::filesystem::copy_file("mysaasapp.db", dest, std::filesystem::copy_options::overwrite_existing); AddConsoleLog("[BASARILI] Veritabani yedeklendi -> " + dest); } else { AddConsoleLog("[HATA] 'mysaasapp.db' bulunamadi."); } } catch (const std::exception& e) { AddConsoleLog(std::string("[HATA] Yedekleme basarisiz: ") + e.what()); } }
    else if (action == "ban" && args.size() > 1) { std::string targetId = args[1]; AddConsoleLog("[BILGI] Yasaklaniyor: " + targetId); std::thread([targetId]() { cpr::Response r = cpr::Post(cpr::Url{ API_BASE_URL + "/api/admin/ban" }, cpr::Header{ {"Authorization", jwtToken}, {"Content-Type", "application/json"} }, cpr::Body{ "{\"user_id\":\"" + targetId + "\"}" }); if (r.status_code == 200) { AddConsoleLog("[BASARILI] Yasaklandi."); FetchBanListAsync(); FetchUsersAsync(); } else AddConsoleLog("[HATA] Yasaklama basarisiz (" + std::to_string(r.status_code) + ")"); }).detach(); }
    else if (action == "unban" && args.size() > 1) { std::string targetId = args[1]; AddConsoleLog("[BILGI] Yasak kaldiriliyor: " + targetId); std::thread([targetId]() { cpr::Response r = cpr::Post(cpr::Url{ API_BASE_URL + "/api/admin/unban" }, cpr::Header{ {"Authorization", jwtToken}, {"Content-Type", "application/json"} }, cpr::Body{ "{\"user_id\":\"" + targetId + "\"}" }); if (r.status_code == 200) { AddConsoleLog("[BASARILI] Yasak kaldirildi."); FetchBanListAsync(); FetchUsersAsync(); } else AddConsoleLog("[HATA] Islem basarisiz (" + std::to_string(r.status_code) + ")"); }).detach(); }
    else if (action == "banlist") { show_ban_list = true; FetchBanListAsync(); }
    else if (action == "statsuser" && args.size() > 1) { active_stats_id = args[1]; show_user_stats = true; FetchUserStatsAsync(active_stats_id); }
    else if (action == "statsserver" && args.size() > 1) { active_stats_id = args[1]; show_server_stats = true; FetchServerStatsAsync(active_stats_id); }
    else { AddConsoleLog("[HATA] Gecersiz komut. Eger ID gerekiyorsa yanina yazmayi unutmayin."); }
}

// =============================================================
// UI PANELLERİ ÇİZİM MANTIĞI
// =============================================================
void DrawMainMenuBar() { if (ImGui::BeginMainMenuBar()) { if (ImGui::BeginMenu("Paneller")) { ImGui::MenuItem("Sunucu Kontrol Merkezi", NULL, &show_server_control); ImGui::MenuItem("Sistem Monitoru (Dashboard)", NULL, &show_dashboard); ImGui::MenuItem("Kullanici Yonetimi (CRM)", NULL, &show_user_management); ImGui::MenuItem("Sunucu Yonetimi (Workspace)", NULL, &show_server_management); ImGui::Separator(); ImGui::MenuItem("Sistem Konsolu", NULL, &show_console_window); ImGui::MenuItem("API Test Araci (Postman)", NULL, &show_api_tester_window); ImGui::Separator(); if (ImGui::MenuItem("Yasakli Kullanicilar (Banlist)")) { show_ban_list = true; FetchBanListAsync(); } ImGui::EndMenu(); } ImGui::EndMainMenuBar(); } }
void DrawServerControlPanel() { if (!show_server_control) return; ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver); ImGui::SetNextWindowSize(ImVec2(450, 300), ImGuiCond_FirstUseEver); ImGui::Begin(">> SUNUCU KONTROL MERKEZI <<", &show_server_control); ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Motor: MySaaSApp.exe"); ImGui::Separator(); ImGui::Text("Durum: "); ImGui::SameLine(); if (is_server_running) { ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "AKTIF"); ImGui::Text("Uptime: %s", server_uptime_str.c_str()); } else { ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "KAPALI"); ImGui::Text("Uptime: 00:00:00"); } ImGui::Spacing(); if (ImGui::Button("Sunucuyu Baslat", ImVec2(180, 30))) { if (!is_server_running) StartBackendServer(); } ImGui::SameLine(); if (ImGui::Button("Sunucuyu Durdur", ImVec2(180, 30))) { if (is_server_running) StopBackendServer(); } ImGui::Spacing(); ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "HTTP Trafik Loglari"); ImGui::Separator(); ImGui::BeginChild("HTTPLogRegion", ImVec2(0, 0), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar); { std::lock_guard<std::mutex> lock(httpLogMutex); for (const auto& log : http_traffic_log) { if (log.find("401") != std::string::npos || log.find("404") != std::string::npos || log.find("500") != std::string::npos || log.find("[HATA]") != std::string::npos) ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "%s", log.c_str()); else if (log.find("200") != std::string::npos || log.find("201") != std::string::npos) ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", log.c_str()); else ImGui::TextUnformatted(log.c_str()); } } if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f); ImGui::EndChild(); ImGui::End(); }
void DrawDashboard() { if (!show_dashboard) return; ImGui::SetNextWindowPos(ImVec2(10, 340), ImGuiCond_FirstUseEver); ImGui::SetNextWindowSize(ImVec2(450, 410), ImGuiCond_FirstUseEver); ImGui::Begin(">> SISTEM MONITORU <<", &show_dashboard); int t_users = 0, o_users = 0; { std::lock_guard<std::mutex> lock(userListMutex); t_users = userList.size(); for (const auto& u : userList) { if (u.status == "Online") o_users++; } } ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Ag Istatistikleri"); ImGui::Separator(); ImGui::Columns(3, "net_stats", false); ImGui::Text("Kayitli"); ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%d Kisi", t_users); ImGui::NextColumn(); ImGui::Text("Aktif"); ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%d Kisi", o_users); ImGui::NextColumn(); ImGui::Text("Sunucu"); ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d Adet", (int)serverList.size()); ImGui::Columns(1); ImGui::Spacing(); ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Kaynak Tuketimi"); ImGui::Separator(); char cpuOverlay[64]; sprintf_s(cpuOverlay, "CPU: %.2f%%", app_cpu_usage); ImGui::PlotLines("##AppCPU", app_cpu_graph, 90, time_offset, cpuOverlay, 0.0f, 100.0f, ImVec2(-1, 40)); char ramOverlay[64]; sprintf_s(ramOverlay, "RAM: %.2f MB", app_ram_usage_mb); ImGui::PlotLines("##AppRAM", app_ram_graph, 90, time_offset, ramOverlay, 0.0f, FLT_MAX, ImVec2(-1, 40)); ImGui::Spacing(); ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Fiziksel Sunucu"); ImGui::Separator(); char sysRam[64]; sprintf_s(sysRam, "RAM: %.1f / %.1f GB", current_ram_used_gb, current_ram_total_gb); ImGui::ProgressBar(current_ram_percent, ImVec2(-1, 0), sysRam); ImGui::Spacing(); char diskOverlay[64]; sprintf_s(diskOverlay, "DB Boyutu: %.2f MB", db_size_mb); ImGui::ProgressBar(0.0f, ImVec2(-1, 0), diskOverlay); ImGui::End(); }
void DrawUserManagement() { if (!show_user_management) return; ImGui::SetNextWindowPos(ImVec2(470, 30), ImGuiCond_FirstUseEver); ImGui::SetNextWindowSize(ImVec2(880, 200), ImGuiCond_FirstUseEver); ImGui::Begin(">> KULLANICI YONETIMI (CRM) <<", &show_user_management); if (ImGui::Button("Yenile (Refresh)")) FetchUsersAsync(); if (isFetchingUsers) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 1, 0, 1), " Guncelleniyor..."); } ImGui::Spacing(); if (ImGui::BeginTable("UsersTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) { ImGui::TableSetupColumn("ID"); ImGui::TableSetupColumn("Ad Soyad"); ImGui::TableSetupColumn("E-Posta"); ImGui::TableSetupColumn("Durum"); ImGui::TableSetupColumn("Abonelik"); ImGui::TableSetupColumn("Yetki"); ImGui::TableSetupColumn("Islem"); ImGui::TableHeadersRow(); std::lock_guard<std::mutex> lock(userListMutex); for (int i = 0; i < userList.size(); i++) { ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::PushID(std::string("u_" + std::to_string(i)).c_str()); ImGui::Selectable(userList[i].id.c_str()); if (ImGui::BeginPopupContextItem()) { if (ImGui::Selectable("ID Kopyala")) ImGui::SetClipboardText(userList[i].id.c_str()); ImGui::EndPopup(); } ImGui::PopID(); ImGui::TableSetColumnIndex(1); ImGui::Text("%s", userList[i].name.c_str()); ImGui::TableSetColumnIndex(2); ImGui::Text("%s", userList[i].email.c_str()); ImGui::TableSetColumnIndex(3); if (userList[i].status == "Online") ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Online"); else if (userList[i].status == "Banned") ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Banned"); else ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Offline"); ImGui::TableSetColumnIndex(4); ImGui::Text("%s", userList[i].sub_level.c_str()); ImGui::TableSetColumnIndex(5); ImGui::Text("%s", userList[i].role.c_str()); ImGui::TableSetColumnIndex(6); std::string btn = "Detay##U" + std::to_string(i); if (ImGui::Button(btn.c_str())) ProcessConsoleCommand("statsuser " + userList[i].id); ImGui::SameLine(); std::string btnBan = "Ban##U" + std::to_string(i); if (ImGui::Button(btnBan.c_str())) ProcessConsoleCommand("ban " + userList[i].id); } ImGui::EndTable(); } ImGui::End(); }
void DrawServerManagement() { if (!show_server_management) return; ImGui::SetNextWindowPos(ImVec2(470, 240), ImGuiCond_FirstUseEver); ImGui::SetNextWindowSize(ImVec2(880, 150), ImGuiCond_FirstUseEver); ImGui::Begin(">> SUNUCU (WORKSPACE) YONETIMI <<", &show_server_management); if (ImGui::Button("Yenile (Refresh)")) FetchServersAsync(); if (isFetchingServers) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 1, 0, 1), " Guncelleniyor..."); } ImGui::Spacing(); if (ImGui::BeginTable("ServersTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) { ImGui::TableSetupColumn("ID"); ImGui::TableSetupColumn("Sunucu Adi"); ImGui::TableSetupColumn("Kurucu ID"); ImGui::TableSetupColumn("Uye"); ImGui::TableSetupColumn("Islem"); ImGui::TableHeadersRow(); std::lock_guard<std::mutex> lock(serverListMutex); for (int i = 0; i < serverList.size(); i++) { ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::PushID(std::string("s_" + std::to_string(i)).c_str()); ImGui::Selectable(serverList[i].id.c_str()); if (ImGui::BeginPopupContextItem()) { if (ImGui::Selectable("ID Kopyala")) ImGui::SetClipboardText(serverList[i].id.c_str()); ImGui::EndPopup(); } ImGui::PopID(); ImGui::TableSetColumnIndex(1); ImGui::Text("%s", serverList[i].name.c_str()); ImGui::TableSetColumnIndex(2); ImGui::Text("%s", serverList[i].owner_id.c_str()); ImGui::TableSetColumnIndex(3); ImGui::Text("%d", serverList[i].member_count); ImGui::TableSetColumnIndex(4); std::string btn = "Gozetim##S" + std::to_string(i); if (ImGui::Button(btn.c_str())) ProcessConsoleCommand("statsserver " + serverList[i].id); } ImGui::EndTable(); } ImGui::End(); }
void DrawSystemConsoleWindow() { if (!show_console_window) return; ImGui::SetNextWindowPos(ImVec2(470, 400), ImGuiCond_FirstUseEver); ImGui::SetNextWindowSize(ImVec2(880, 200), ImGuiCond_FirstUseEver); if (ImGui::Begin(">> SISTEM KONSOLU <<", &show_console_window)) { ImGui::BeginChild("ConsoleRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar); { std::lock_guard<std::mutex> lock(consoleLogMutex); for (const auto& log : consoleLog) { if (log.find("[HATA]") != std::string::npos) ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "%s", log.c_str()); else if (log.find("[API ISTEGI]") != std::string::npos) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "%s", log.c_str()); else if (log.find("[API YANIT]") != std::string::npos) ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", log.c_str()); else if (log.find("[BASARILI]") != std::string::npos || log.find("[DOSYA]") != std::string::npos) ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", log.c_str()); else ImGui::TextUnformatted(log.c_str()); } } if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f); ImGui::EndChild(); ImGui::PushItemWidth(-60); if (ImGui::InputText("##Komut", consoleInput, IM_ARRAYSIZE(consoleInput), ImGuiInputTextFlags_EnterReturnsTrue)) { ProcessConsoleCommand(std::string(consoleInput)); strcpy_s(consoleInput, ""); ImGui::SetKeyboardFocusHere(-1); } ImGui::PopItemWidth(); ImGui::SameLine(); if (ImGui::Button("Gonder")) { ProcessConsoleCommand(std::string(consoleInput)); strcpy_s(consoleInput, ""); } } ImGui::End(); }

// =============================================================
// MUHTEŞEM HTML DOKÜMANTASYON MOTORU VE POSTMAN
// =============================================================
void DrawApiTesterWindow() {
    if (!show_api_tester_window) return;
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver); ImGui::SetNextWindowSize(ImVec2(1100, 650), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(">> GELISTIRICI API TEST ARACI (POSTMAN) <<", &show_api_tester_window)) {
        { std::lock_guard<std::mutex> lock(api_response_mutex); if (new_response_ready) { strncpy_s(api_response_buffer, sizeof(api_response_buffer), thread_response_temp.c_str(), _TRUNCATE); new_response_ready = false; } }

        ImGui::Columns(2, "PostmanColumns"); ImGui::SetColumnWidth(0, 400);

        // --- SOL PANEL ---
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Tam Endpoint Kutuphanesi (%d Adet)", IM_ARRAYSIZE(predefined_endpoints)); ImGui::SameLine();

        // HTML DIŞA AKTARMA BUTONU
        if (ImGui::Button("HTML Dokuman Cikart", ImVec2(150, 20))) {
            std::ofstream file("api_documentation.html");
            if (file.is_open()) {
                std::string html_header = R"(
<!DOCTYPE html><html lang="tr"><head><meta charset="UTF-8"><title>MySaaS API Dokümantasyonu</title>
<style>
    body { font-family: 'Segoe UI', sans-serif; background-color: #0d1117; color: #c9d1d9; margin: 0; padding: 20px; }
    h1 { color: #58a6ff; text-align: center; border-bottom: 1px solid #30363d; padding-bottom: 10px; }
    .auth-box { background-color: #161b22; border: 1px solid #30363d; border-radius: 8px; padding: 20px; margin-bottom: 20px; }
    .auth-box h3 { margin-top: 0; color: #3fb950; }
    table { width: 100%; border-collapse: collapse; background-color: #161b22; border-radius: 8px; overflow: hidden; }
    th, td { border: 1px solid #30363d; padding: 12px 15px; text-align: left; vertical-align: top;}
    th { background-color: #21262d; color: #8b949e; font-weight: bold;}
    .method { padding: 5px 10px; border-radius: 4px; font-weight: bold; font-size: 12px; display: inline-block; width: 60px; text-align: center;}
    .m-GET { background-color: #238636; color: #ffffff; } .m-POST { background-color: #d29922; color: #ffffff; }
    .m-PUT { background-color: #1f6feb; color: #ffffff; } .m-DELETE { background-color: #da3633; color: #ffffff; }
    .url { font-family: monospace; font-size: 14px; color: #a5d6ff; font-weight: bold;}
    pre { margin: 0; color: #e6edf3; font-size: 13px; white-space: pre-wrap; background: #0d1117; padding: 10px; border-radius: 6px; border: 1px solid #30363d;}
</style></head>
<body>
    <h1>🚀 MySaaS REST API & WebSocket Dokümantasyonu</h1>
    <div class="auth-box"><h3>🔐 Güvenlik (Bearer Token)</h3><p>Tüm isteklere token eklenmelidir:</p>
    <pre>Headers: { "Content-Type": "application/json", "Authorization": "Bearer &lt;TOKEN_BURADA&gt;" }</pre></div>
    <table><thead><tr><th style="width: 80px;">Metot</th><th style="width: 300px;">Endpoint (URL)</th><th style="width: 250px;">Açıklama</th><th>JSON Gövdesi (Body)</th></tr></thead><tbody>)";
                file << html_header;
                for (int i = 0; i < IM_ARRAYSIZE(predefined_endpoints); i++) {
                    std::string m = predefined_endpoints[i].method; std::string mClass = "m-" + m;
                    std::string bodyText = predefined_endpoints[i].body; if (bodyText.empty()) bodyText = "<span style='color:#8b949e'><i>JSON Body Gerekmez</i></span>";
                    file << "<tr><td><span class='method " << mClass << "'>" << m << "</span></td><td class='url'>" << predefined_endpoints[i].url << "</td><td>" << predefined_endpoints[i].desc << "</td><td><pre>" << bodyText << "</pre></td></tr>\n";
                }
                file << "</tbody></table></body></html>"; file.close(); AddConsoleLog("[BASARILI] Mukemmel API Dokumani 'api_documentation.html' olarak olusturuldu!");
            }
        }
        ImGui::Separator();

        ImGui::BeginChild("EndpointListRegion", ImVec2(0, 0), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar);
        for (int i = 0; i < IM_ARRAYSIZE(predefined_endpoints); i++) {
            std::string m = predefined_endpoints[i].method;
            if (m == "GET") ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.2f, 1.0f)); else if (m == "POST") ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.0f, 1.0f)); else if (m == "PUT") ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.6f, 1.0f, 1.0f)); else ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            std::string label = "[" + m + "] " + predefined_endpoints[i].desc;
            if (ImGui::Selectable(label.c_str())) {
                if (m == "GET") api_method_idx = 0; else if (m == "POST") api_method_idx = 1; else if (m == "PUT") api_method_idx = 2; else api_method_idx = 3;
                strcpy_s(api_url_buffer, predefined_endpoints[i].url); strcpy_s(api_body_buffer, predefined_endpoints[i].body); strcpy_s(api_response_buffer, "Istek gonderilmeyi bekliyor..."); api_last_status = 0;
            } ImGui::PopStyleColor(); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", predefined_endpoints[i].url);
        } ImGui::EndChild(); ImGui::NextColumn();

        // --- SAĞ PANEL ---
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "HTTP Istegi Olustur"); ImGui::Separator();
        ImGui::PushItemWidth(80); ImGui::Combo("##Method", &api_method_idx, api_methods, IM_ARRAYSIZE(api_methods)); ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(-1); ImGui::InputText("##URL", api_url_buffer, IM_ARRAYSIZE(api_url_buffer)); ImGui::PopItemWidth(); ImGui::Spacing();
        ImGui::Text("JSON Request Body:"); ImGui::InputTextMultiline("##Body", api_body_buffer, IM_ARRAYSIZE(api_body_buffer), ImVec2(-1, 150), ImGuiInputTextFlags_AllowTabInput); ImGui::Spacing();
        if (is_api_loading) { ImGui::Button("Gonderiliyor...", ImVec2(150, 40)); }
        else { if (ImGui::Button("Istegi Gonder (SEND)", ImVec2(150, 40))) { if (is_server_running) { SendApiRequest(); } else { strcpy_s(api_response_buffer, "HATA: Sunucu kapali! Lutfen sunucuyu baslatin."); } } } ImGui::SameLine(); ImGui::Text("Durum Kodu: "); ImGui::SameLine();
        if (api_last_status == 200 || api_last_status == 201) ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%d OK", api_last_status); else if (api_last_status > 0) ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "%d ERROR", api_last_status); else ImGui::Text("Bekleniyor...");
        ImGui::Spacing(); ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Sunucu Yaniti (Response Body)"); ImGui::Separator();
        ImGui::InputTextMultiline("##Response", api_response_buffer, IM_ARRAYSIZE(api_response_buffer), ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
        ImGui::Columns(1);
    } ImGui::End();
}

void DrawBanListModal() { if (!show_ban_list) return; ImGui::SetNextWindowPos(ImVec2(1366 / 2 - 350, 768 / 2 - 200), ImGuiCond_FirstUseEver); ImGui::SetNextWindowSize(ImVec2(700, 400), ImGuiCond_FirstUseEver); if (ImGui::Begin("Yasakli Kullanicilar (Ban Listesi)", &show_ban_list)) { if (ImGui::Button("Listeyi Yenile")) FetchBanListAsync(); ImGui::Spacing(); if (ImGui::BeginTable("BanTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) { ImGui::TableSetupColumn("Kullanici ID"); ImGui::TableSetupColumn("Sebep"); ImGui::TableSetupColumn("Tarih"); ImGui::TableSetupColumn("Islem"); ImGui::TableHeadersRow(); std::lock_guard<std::mutex> lock(banListMutex); for (int i = 0; i < banList.size(); i++) { ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("%s", banList[i].user_id.c_str()); ImGui::TableSetColumnIndex(1); ImGui::Text("%s", banList[i].reason.c_str()); ImGui::TableSetColumnIndex(2); ImGui::Text("%s", banList[i].date.c_str()); ImGui::TableSetColumnIndex(3); std::string btn = "Kaldir##B" + std::to_string(i); if (ImGui::Button(btn.c_str())) ProcessConsoleCommand("unban " + banList[i].user_id); } ImGui::EndTable(); } } ImGui::End(); }
void DrawUserStatsModal() { if (!show_user_stats) return; ImGui::SetNextWindowPos(ImVec2(1366 / 2 - 400, 768 / 2 - 250), ImGuiCond_FirstUseEver); ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver); std::string title = "Kullanici Istihbarati: " + active_stats_id; if (ImGui::Begin(title.c_str(), &show_user_stats)) { if (isFetchingStats) ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Veriler cekiliyor..."); else { std::lock_guard<std::mutex> lock(statsMutex); if (ImGui::BeginTabBar("UTabs")) { if (ImGui::BeginTabItem("Sunucular")) { if (activeStatsServers.empty()) ImGui::Text("Sunucu yok."); else { if (ImGui::BeginTable("UST", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) { ImGui::TableSetupColumn("ID"); ImGui::TableSetupColumn("Ad"); ImGui::TableSetupColumn("Durum"); ImGui::TableHeadersRow(); for (const auto& s : activeStatsServers) { ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("%s", s.id.c_str()); ImGui::TableSetColumnIndex(1); ImGui::Text("%s", s.name.c_str()); ImGui::TableSetColumnIndex(2); if (s.owner_id == active_stats_id) ImGui::TextColored(ImVec4(0.2f, 1, 0.2f, 1), "Kurucu"); else ImGui::Text("Uye"); } ImGui::EndTable(); } } ImGui::EndTabItem(); } if (ImGui::BeginTabItem("DM/Arkadaslar")) { if (activeStatsFriends.empty()) ImGui::Text("Arkadas yok."); else { if (ImGui::BeginTable("UFT", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) { ImGui::TableSetupColumn("ID"); ImGui::TableSetupColumn("Ad"); ImGui::TableSetupColumn("E-Posta"); ImGui::TableHeadersRow(); for (const auto& f : activeStatsFriends) { ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("%s", f.id.c_str()); ImGui::TableSetColumnIndex(1); ImGui::Text("%s", f.name.c_str()); ImGui::TableSetColumnIndex(2); ImGui::Text("%s", f.email.c_str()); } ImGui::EndTable(); } } ImGui::EndTabItem(); } ImGui::EndTabBar(); } } } ImGui::End(); }
void DrawServerStatsModal() { if (!show_server_stats) return; ImGui::SetNextWindowPos(ImVec2(1366 / 2 - 400, 768 / 2 - 300), ImGuiCond_FirstUseEver); ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver); std::string title = "Sunucu Gozetimi: " + active_stats_id; if (ImGui::Begin(title.c_str(), &show_server_stats)) { if (isFetchingServerStats) ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Cekiliyor..."); else { ImGui::Columns(2, "SCols"); ImGui::SetColumnWidth(0, 300); ImGui::TextColored(ImVec4(0.4f, 0.8f, 1, 1), "Uyeler"); ImGui::BeginChild("SMR", ImVec2(0, 0), true); { std::lock_guard<std::mutex> lock(statsMutex); if (activeServerMembersList.empty()) ImGui::Text("Uye yok."); else { if (ImGui::BeginTable("SMT", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) { ImGui::TableSetupColumn("Ad"); ImGui::TableSetupColumn("Durum"); ImGui::TableHeadersRow(); for (const auto& m : activeServerMembersList) { ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("%s", m.name.c_str()); ImGui::TableSetColumnIndex(1); if (m.status == "Online") ImGui::TextColored(ImVec4(0.2f, 1, 0.2f, 1), "Online"); else ImGui::Text("Offline"); } ImGui::EndTable(); } } } ImGui::EndChild(); ImGui::NextColumn(); ImGui::TextColored(ImVec4(0.4f, 0.8f, 1, 1), "Loglar"); ImGui::BeginChild("SLR", ImVec2(0, 0), true); { std::lock_guard<std::mutex> lock(statsMutex); if (activeServerLogsList.empty()) ImGui::Text("Kayıt yok."); else { if (ImGui::BeginTable("SLT", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) { ImGui::TableSetupColumn("Tarih", ImGuiTableColumnFlags_WidthFixed, 130); ImGui::TableSetupColumn("Aksiyon", ImGuiTableColumnFlags_WidthFixed, 100); ImGui::TableSetupColumn("Detay"); ImGui::TableHeadersRow(); for (const auto& log : activeServerLogsList) { ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("%s", log.time.c_str()); ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "%s", log.action.c_str()); ImGui::TableSetColumnIndex(2); ImGui::TextWrapped("%s", log.details.c_str()); } ImGui::EndTable(); } } } ImGui::EndChild(); ImGui::Columns(1); } } ImGui::End(); }

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(1366, 768, "MySaaS - Super Admin Arayuzu", NULL, NULL); if (!window) return -1; glfwMakeContextCurrent(window); glfwSwapInterval(1);
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGuiStyle& style = ImGui::GetStyle(); ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true); ImGui_ImplOpenGL3_Init("#version 130");
    AddConsoleLog("[SISTEM] Arayuz acildi. Komutlar icin 'help' yaziniz.");

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents(); ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        DrawMainMenuBar();
        double current_time = ImGui::GetTime();
        if (current_time - last_update_time > 0.5) { UpdateHardwareMetrics(); app_cpu_graph[time_offset] = app_cpu_usage; app_ram_graph[time_offset] = app_ram_usage_mb; db_size_graph[time_offset] = db_size_mb; time_offset = (time_offset + 1) % 90; last_update_time = current_time; }
        if (is_server_running && (current_time - last_sync_time > 3.0)) { FetchUsersAsync(); FetchServersAsync(); FetchBanListAsync(); last_sync_time = current_time; }
        else if (!is_server_running) { last_sync_time = 0; }

        DrawServerControlPanel(); DrawDashboard(); DrawUserManagement(); DrawServerManagement();
        DrawSystemConsoleWindow(); DrawApiTesterWindow();
        DrawBanListModal(); DrawUserStatsModal(); DrawServerStatsModal();

        ImGui::Render(); int dw, dh; glfwGetFramebufferSize(window, &dw, &dh); glViewport(0, 0, dw, dh); glClearColor(0.08f, 0.08f, 0.08f, 1.0f); glClear(GL_COLOR_BUFFER_BIT); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); glfwSwapBuffers(window);
    }
    if (is_server_running) StopBackendServer();
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate(); return 0;
}