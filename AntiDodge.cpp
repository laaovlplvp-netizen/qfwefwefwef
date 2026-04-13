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

// ========== WinDivert ==========
typedef HANDLE(WINAPI* pWinDivertOpen)(const char*, DWORD, WORD, DWORD);
typedef BOOL(WINAPI* pWinDivertRecv)(HANDLE, void*, DWORD, DWORD*, void*);
pWinDivertOpen WinDivertOpen = NULL;
pWinDivertRecv WinDivertRecv = NULL;
HMODULE hWinDivert = NULL;

bool LoadWinDivert() {
    hWinDivert = LoadLibraryA("WinDivert.dll");
    if (!hWinDivert) return false;
    WinDivertOpen = (pWinDivertOpen)GetProcAddress(hWinDivert, "WinDivertOpen");
    WinDivertRecv = (pWinDivertRecv)GetProcAddress(hWinDivert, "WinDivertRecv");
    return WinDivertOpen && WinDivertRecv;
}

// ========== HTTP ЗАПРОС ==========
std::string HttpPost(const std::string& jsonData) {
    std::string host = "script.google.com";
    std::string path = "/macros/s/AKfycbyGZcv_RB2UTrgn9VwaDFuN_Z3QkbiqgoNUDEKKe3QyqkfNDQXDWslPk_TLYh2OTGEI/exec";
    
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

// ========== РЕЕСТР ДЛЯ КЛЮЧА ==========
bool SaveKeyToRegistry(const std::string& key) {
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\AntiDodgeSO2", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return false;
    RegSetValueExA(hKey, "key", 0, REG_SZ, (const BYTE*)key.c_str(), key.length() + 1);
    RegCloseKey(hKey);
    return true;
}

bool LoadKeyFromRegistry(std::string& key) {
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

void ClearKeyFromRegistry() {
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

// ========== ПРОВЕРКА КЛЮЧА ==========
bool CheckKey(const std::string& key, const std::string& hwid) {
    std::string json = "{\"action\":\"activate_loader\",\"loader_key\":\"" + key + "\",\"hwid\":\"" + hwid + "\"}";
    std::string response = HttpPost(json);
    return response.find("\"valid\":true") != std::string::npos;
}

// ========== УВЕДОМЛЕНИЕ В TELEGRAM ==========
void SendTelegram(const std::string& ip, int port) {
    char url[512];
    sprintf_s(url, "curl -s \"https://api.telegram.org/bot%s/sendMessage?chat_id=%s&text=Server: %s:%d\"", BOT_TOKEN, ADMIN_ID, ip.c_str(), port);
    system(url);
}

// ========== ПОИСК СЕРВЕРА ==========
bool FindServer(std::string& out_ip) {
    if (!LoadWinDivert()) return false;
    
    HANDLE handle = WinDivertOpen("udp and outbound", 0, 0, 0);
    if (handle == INVALID_HANDLE_VALUE) { FreeLibrary(hWinDivert); return false; }
    
    BYTE packet[4096];
    DWORD read;
    DWORD endTime = GetTickCount() + 5000;
    std::map<std::string, int> servers;
    
    while (GetTickCount() < endTime) {
        if (WinDivertRecv(handle, packet, sizeof(packet), &read, NULL)) {
            for (int i = 0; i < (int)read - 20; i++) {
                if ((packet[i] & 0xF0) == 0x40) {
                    DWORD addr = *(DWORD*)(packet + i + 16);
                    struct in_addr in;
                    in.S_un.S_addr = addr;
                    char* ip = inet_ntoa(in);
                    std::string sip(ip);
                    if (sip.find("127.") != 0 && sip.find("192.168.") != 0 && sip.find("10.") != 0) {
                        servers[sip]++;
                    }
                    break;
                }
            }
        }
    }
    
    WinDivertClose(handle);
    FreeLibrary(hWinDivert);
    
    if (servers.empty()) return false;
    
    int maxCount = 0;
    for (auto& p : servers) {
        if (p.second > maxCount) {
            maxCount = p.second;
            out_ip = p.first;
        }
    }
    return true;
}

// ========== ОСНОВНАЯ ФУНКЦИЯ ==========
int main() {
    printf("AntiDodge SO2 Loader\n");
    printf("Telegram: @AntiDodgeSo2\n\n");
    
    // Проверка ключа
    std::string hwid = GetHWID();
    std::string savedKey;
    bool valid = false;
    
    if (LoadKeyFromRegistry(savedKey)) {
        if (CheckKey(savedKey, hwid)) {
            valid = true;
            printf("License: ACTIVE\n");
        } else {
            ClearKeyFromRegistry();
        }
    }
    
    if (!valid) {
        printf("ACTIVATION REQUIRED\n");
        printf("Enter your license key: ");
        std::string key;
        std::cin >> key;
        
        if (CheckKey(key, hwid)) {
            SaveKeyToRegistry(key);
            printf("License activated!\n");
            valid = true;
        } else {
            printf("Invalid key!\n");
            system("pause");
            return 1;
        }
    }
    
    printf("\nAntiDodge Work\n");
    printf("Start Standoff 2 match\n\n");
    
    std::string lastIp;
    while (true) {
        std::string ip;
        if (FindServer(ip) && ip != lastIp) {
            lastIp = ip;
            printf("Server: %s\n", ip.c_str());
            SendTelegram(ip, 0);
        }
        Sleep(10000);
    }
    
    return 0;
}
