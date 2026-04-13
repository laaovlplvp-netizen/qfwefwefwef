#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string>
#include <map>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

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

// ========== Конфиг ==========
const char* GOOGLE_API_URL = "https://script.google.com/macros/s/AKfycbyGZcv_RB2UTrgn9VwaDFuN_Z3QkbiqgoNUDEKKe3QyqkfNDQXDWslPk_TLYh2OTGEI/exec";
const char* BOT_TOKEN = "8653498484:AAE1HIez8xwfKopD-FGH7mb7h8pwHgnJapU";
const char* ADMIN_ID = "112065332";

// ========== HTTP запрос ==========
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

// ========== Реестр ==========
bool SaveLicense(const std::string& key) {
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\AntiDodgeSO2", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return false;
    RegSetValueExA(hKey, "key", 0, REG_SZ, (const BYTE*)key.c_str(), key.length() + 1);
    RegCloseKey(hKey);
    return true;
}

bool LoadLicense(std::string& key) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\AntiDodgeSO2", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    char buf[256];
    DWORD size = sizeof(buf);
    if (RegQueryValueExA(hKey, "key", NULL, NULL, (LPBYTE)buf, &size) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return false;
    }
    key = buf;
    RegCloseKey(hKey);
    return true;
}

// ========== Проверка лицензии ==========
bool CheckLicense(const std::string& key) {
    std::string json = "{\"action\":\"activate_loader\",\"loader_key\":\"" + key + "\",\"hwid\":\"TEST\"}";
    std::string response = HttpPost(GOOGLE_API_URL, json);
    return response.find("\"valid\":true") != std::string::npos;
}

// ========== Уведомление в Telegram ==========
void SendNotification(const std::string& ip) {
    std::string url = "https://api.telegram.org/bot" + std::string(BOT_TOKEN) + "/sendMessage";
    std::string json = "{\"chat_id\":\"" + std::string(ADMIN_ID) + "\",\"text\":\"🎮 Server: " + ip + "\"}";
    HttpPost(url, json);
}

// ========== Поиск сервера через WinDivert ==========
bool FindServer(std::string& out_ip) {
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
                    if (ip.find("127.") != 0 && ip.find("192.168.") != 0 && ip.find("10.") != 0) {
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
    return true;
}

// ========== Главная функция ==========
int main() {
    printf("AntiDodge SO2 Loader\n");
    printf("Telegram: @AntiDodgeSo2\n\n");
    
    std::string key;
    if (!LoadLicense(key)) {
        printf("ACTIVATION REQUIRED\n");
        printf("Enter license key: ");
        std::cin >> key;
        if (CheckLicense(key)) {
            SaveLicense(key);
            printf("License activated!\n");
        } else {
            printf("Invalid key!\n");
            getchar(); getchar();
            return 1;
        }
    } else {
        if (!CheckLicense(key)) {
            printf("License expired!\n");
            getchar();
            return 1;
        }
        printf("License active\n");
    }
    
    printf("\nAntiDodge Work\n");
    printf("Start Standoff 2\n\n");
    
    std::string lastIp;
    while (true) {
        std::string ip;
        if (FindServer(ip) && ip != lastIp) {
            lastIp = ip;
            printf("Server: %s\n", ip.c_str());
            SendNotification(ip);
        }
        Sleep(10000);
    }
    
    return 0;
}
