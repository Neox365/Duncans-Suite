#include "SuiteSafety.h"

#include <shlobj.h>

#include <cstdio>
#include <mutex>
#include <vector>

namespace suite_safety {

namespace {
    std::mutex g_logMutex;

    std::wstring ProgramData() {
        wchar_t base[MAX_PATH] = {};
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, SHGFP_TYPE_CURRENT, base))) return L"";
        return base;
    }

    std::string ToUtf8(const std::wstring& w) {
        if (w.empty()) return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
        return s;
    }

#pragma pack(push, 1)
    struct RestorePointInfo { DWORD eventType; DWORD pointType; INT64 seq; WCHAR description[256]; };
    struct StateMgrStatus { DWORD status; INT64 seq; };
#pragma pack(pop)
    using SetFn = BOOL(WINAPI*)(RestorePointInfo*, StateMgrStatus*);
}

std::wstring DataDir() {
    const std::wstring pd = ProgramData();
    if (pd.empty()) return L"";
    const std::wstring dir = pd + L"\\DuncansSuite";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring BackupsDir() {
    const std::wstring d = DataDir();
    if (d.empty()) return L"";
    const std::wstring dir = d + L"\\Backups";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring LogPath() {
    const std::wstring d = DataDir();
    return d.empty() ? L"" : d + L"\\duncans_suite.log";
}

void Log(const wchar_t* module, const std::wstring& text) {
    const std::wstring path = LogPath();
    if (path.empty()) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad) && fad.nFileSizeLow > (1u << 20))
        MoveFileExW(path.c_str(), (path + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);   // einfache Rotation
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t head[96];
    swprintf(head, 96, L"%04d-%02d-%02d %02d:%02d:%02d [%ls] ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, module);
    const std::string line = ToUtf8(std::wstring(head) + text) + "\r\n";
    DWORD written = 0;
    WriteFile(f, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(f);
}

bool CreateRestorePoint(DWORD pointType, std::wstring& note) {
    HMODULE lib = LoadLibraryExW(L"srclient.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);   // nur aus System32 laden
    if (!lib) { note = L"Systemwiederherstellung nicht verfügbar (srclient.dll fehlt)."; Log(L"Sicherheit", L"Wiederherstellungspunkt: " + note); return false; }
    auto fn = reinterpret_cast<SetFn>(reinterpret_cast<void*>(GetProcAddress(lib, "SRSetRestorePointW")));
    if (!fn) { FreeLibrary(lib); note = L"SRSetRestorePointW wurde nicht gefunden."; Log(L"Sicherheit", L"Wiederherstellungspunkt: " + note); return false; }

    RestorePointInfo info{};
    info.eventType = 100;       // BEGIN_SYSTEM_CHANGE
    info.pointType = pointType;
    lstrcpynW(info.description, L"Duncan's Suite Pre-Optimization", 256);
    StateMgrStatus st{};
    const BOOL ok = fn(&info, &st) && st.status == 0;
    if (ok) {
        RestorePointInfo end{};
        end.eventType = 101;    // END_SYSTEM_CHANGE
        end.seq = st.seq;
        StateMgrStatus endSt{};
        fn(&end, &endSt);
    }
    FreeLibrary(lib);
    if (!ok) {
        note = st.status == ERROR_SERVICE_DISABLED
            ? L"Der Systemschutz ist auf diesem PC deaktiviert."
            : L"Der Wiederherstellungspunkt konnte nicht erstellt werden (Status " + std::to_wstring(st.status) + L").";
        Log(L"Sicherheit", L"Wiederherstellungspunkt \"Duncan's Suite Pre-Optimization\": FEHLER – " + note);
        return false;
    }
    Log(L"Sicherheit", L"Wiederherstellungspunkt \"Duncan's Suite Pre-Optimization\" angelegt (Nr. " + std::to_wstring(st.seq) + L")");
    return true;
}

bool IsElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION el{};
    DWORD n = 0;
    const bool ok = GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &n) != 0;
    CloseHandle(tok);
    return ok && el.TokenIsElevated != 0;
}

}  // namespace suite_safety
