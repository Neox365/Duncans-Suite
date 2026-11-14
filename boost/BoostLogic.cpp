#include "BoostLogic.h"

#include <dwmapi.h>
#include <objbase.h>
#include <powrprof.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>

namespace boostmode {

namespace {
    std::wstring g_dataDir;

    const GUID kHighPerformance = { 0x8c5e7fda, 0xe8bf, 0x4a96, { 0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c } };
    const GUID kUltimatePerformance = { 0xe9a42b02, 0xd5df, 0x448d, { 0xaa, 0x00, 0x03, 0xf1, 0x47, 0x49, 0xeb, 0x61 } };

    struct Proc { DWORD pid; std::wstring exe; };

    std::vector<Proc> Snapshot() {
        std::vector<Proc> v;
        HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (h == INVALID_HANDLE_VALUE) return v;
        PROCESSENTRY32W e{}; e.dwSize = sizeof e;
        for (BOOL ok = Process32FirstW(h, &e); ok; ok = Process32NextW(h, &e)) v.push_back({ e.th32ProcessID, Lower(e.szExeFile) });
        CloseHandle(h);
        return v;
    }

    bool SameSession(DWORD pid) {
        DWORD mine = 0, theirs = 0;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &mine) || !ProcessIdToSessionId(pid, &theirs)) return false;
        return mine == theirs;
    }

    unsigned long long WorkingSet(DWORD pid) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return 0;
        PROCESS_MEMORY_COUNTERS pmc{}; pmc.cb = sizeof pmc;
        unsigned long long ws = GetProcessMemoryInfo(h, &pmc, sizeof pmc) ? pmc.WorkingSetSize : 0;
        CloseHandle(h);
        return ws;
    }

    std::wstring ImagePath(DWORD pid) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return L"";
        wchar_t buf[MAX_PATH * 2] = {}; DWORD n = static_cast<DWORD>(std::size(buf));
        std::wstring r;
        if (QueryFullProcessImageNameW(h, 0, buf, &n)) r.assign(buf, n);
        CloseHandle(h);
        return r;
    }

    bool IsAlive(DWORD pid) {
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (!h) return false;
        const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
        CloseHandle(h);
        return alive;
    }

    // ---- INI ----
    std::wstring IniPath(const wchar_t* name) { return DataDir() + L"\\" + name; }

    void EnsureUnicodeIni(const std::wstring& path) {     // UTF-16-Datei mit BOM: so speichert WritePrivateProfileString auch Umlaute verlustfrei
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        const WORD bom = 0xFEFF; DWORD w = 0;
        WriteFile(h, &bom, sizeof bom, &w, nullptr);
        CloseHandle(h);
    }

    std::wstring ReadIni(const std::wstring& file, const wchar_t* section, const wchar_t* key, const wchar_t* def = L"") {
        std::vector<wchar_t> buf(2048);
        for (;;) {
            const DWORD n = GetPrivateProfileStringW(section, key, def, buf.data(), static_cast<DWORD>(buf.size()), file.c_str());
            if (n < buf.size() - 1 || buf.size() > (1u << 20)) return std::wstring(buf.data(), n);
            buf.resize(buf.size() * 4);
        }
    }

    bool WriteIni(const std::wstring& file, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
        EnsureUnicodeIni(file);
        return WritePrivateProfileStringW(section, key, value.c_str(), file.c_str()) != 0;
    }

    void Log(const std::wstring& text) {
        const std::wstring path = IniPath(L"boost.log");
        HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        if (GetFileSize(h, nullptr) > (1u << 20)) { CloseHandle(h); DeleteFileW(path.c_str()); return; }
        SYSTEMTIME t; GetLocalTime(&t);
        wchar_t ts[64]; swprintf(ts, 64, L"%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        const std::wstring line = ts + text + L"\r\n";
        const int n = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
        std::string u(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), &u[0], n, nullptr, nullptr);
        DWORD w = 0; WriteFile(h, u.data(), static_cast<DWORD>(u.size()), &w, nullptr);
        CloseHandle(h);
    }

    BOOL CALLBACK CollectWindows(HWND hwnd, LPARAM lp) {
        auto* out = reinterpret_cast<std::map<DWORD, std::wstring>*>(lp);
        if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
        const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW)) return TRUE;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked) return TRUE;   // z. B. angehaltene Store-Apps
        wchar_t title[256] = {};
        if (GetWindowTextW(hwnd, title, 256) <= 0) return TRUE;
        DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
        if (pid && !out->count(pid)) (*out)[pid] = title;
        return TRUE;
    }

    struct CloseCtx { std::set<DWORD> pids; };
    BOOL CALLBACK PostClose(HWND hwnd, LPARAM lp) {
        auto* c = reinterpret_cast<CloseCtx*>(lp);
        DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
        if (c->pids.count(pid)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return TRUE;
    }
}

std::wstring Lower(const std::wstring& s) {
    std::wstring r = s;
    if (!r.empty()) CharLowerBuffW(&r[0], static_cast<DWORD>(r.size()));
    return r;
}

bool IsProtectedExe(const std::wstring& exeLower) {
    static const wchar_t* const kList[] = {
        L"system", L"registry", L"smss.exe", L"csrss.exe", L"wininit.exe", L"winlogon.exe", L"services.exe", L"lsass.exe", L"svchost.exe",
        L"dwm.exe", L"explorer.exe", L"sihost.exe", L"taskhostw.exe", L"fontdrvhost.exe", L"ctfmon.exe", L"textinputhost.exe",
        L"searchhost.exe", L"searchapp.exe", L"startmenuexperiencehost.exe", L"shellexperiencehost.exe", L"applicationframehost.exe",
        L"runtimebroker.exe", L"securityhealthsystray.exe", L"securityhealthservice.exe", L"msmpeng.exe", L"nissrv.exe", L"audiodg.exe",
        L"conhost.exe", L"lockapp.exe", L"logonui.exe", L"spoolsv.exe", L"wudfhost.exe", L"dllhost.exe", L"smartscreen.exe",
        L"duncansboost.exe", L"duncanssuite.exe",
    };
    for (const wchar_t* p : kList) if (exeLower == p) return true;
    return false;
}

std::vector<AppInfo> ListApps() {
    std::map<DWORD, std::wstring> wins;
    EnumWindows(CollectWindows, reinterpret_cast<LPARAM>(&wins));
    const std::vector<Proc> procs = Snapshot();
    const DWORD self = GetCurrentProcessId();
    std::map<DWORD, std::wstring> exeOf;
    for (const Proc& p : procs) exeOf[p.pid] = p.exe;

    std::map<std::wstring, AppInfo> apps;
    for (const auto& w : wins) {
        if (w.first == self) continue;
        auto it = exeOf.find(w.first);
        if (it == exeOf.end() || IsProtectedExe(it->second) || !SameSession(w.first)) continue;
        AppInfo& a = apps[it->second];
        if (a.exe.empty()) { a.exe = it->second; a.title = w.second; }
    }
    for (const Proc& p : procs) {                       // RAM und Prozesszahl aller Prozesse eines Programms
        auto it = apps.find(p.exe);
        if (it == apps.end() || p.pid == self || !SameSession(p.pid)) continue;
        it->second.ramBytes += WorkingSet(p.pid);
        ++it->second.procCount;
    }
    std::vector<AppInfo> out;
    for (auto& a : apps) out.push_back(std::move(a.second));
    std::sort(out.begin(), out.end(), [](const AppInfo& x, const AppInfo& y) { return x.ramBytes > y.ramBytes; });
    return out;
}

bool AnyRunning(const std::vector<std::wstring>& exes) {
    if (exes.empty()) return false;
    for (const Proc& p : Snapshot())
        if (std::find(exes.begin(), exes.end(), p.exe) != exes.end()) return true;
    return false;
}

CloseResult CloseApps(const std::vector<std::wstring>& exes, int graceMs, bool force) {
    CloseResult res;
    const DWORD self = GetCurrentProcessId();
    std::map<std::wstring, std::vector<DWORD>> byExe;          // Zielprogramme -> Prozesse
    std::set<std::wstring> pathSet;
    for (const Proc& p : Snapshot()) {
        if (p.pid == self || IsProtectedExe(p.exe)) continue;
        if (std::find(exes.begin(), exes.end(), p.exe) == exes.end()) continue;
        if (!SameSession(p.pid)) continue;
        byExe[p.exe].push_back(p.pid);
        const std::wstring path = ImagePath(p.pid);
        if (!path.empty() && pathSet.insert(Lower(path)).second) res.paths.push_back(path);
    }
    if (byExe.empty()) return res;

    CloseCtx ctx;
    for (const auto& e : byExe) for (DWORD pid : e.second) ctx.pids.insert(pid);
    EnumWindows(PostClose, reinterpret_cast<LPARAM>(&ctx));    // alle Fenster freundlich schliessen (auch versteckte Tray-Fenster)

    const DWORD start = GetTickCount();
    auto anyAlive = [&]() { for (DWORD pid : ctx.pids) if (IsAlive(pid)) return true; return false; };
    while (anyAlive() && static_cast<int>(GetTickCount() - start) < graceMs) Sleep(200);

    std::set<DWORD> terminated;
    if (force) {
        for (DWORD pid : ctx.pids) {
            if (!IsAlive(pid)) continue;
            if (HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid)) {
                if (TerminateProcess(h, 1)) terminated.insert(pid);
                WaitForSingleObject(h, 2000);
                CloseHandle(h);
            }
        }
    }
    for (const auto& e : byExe) {
        bool alive = false, wasForced = false;
        for (DWORD pid : e.second) {
            if (IsAlive(pid)) alive = true;
            if (terminated.count(pid)) wasForced = true;
        }
        if (alive) { ++res.failed; continue; }
        ++res.closed;
        if (wasForced) ++res.forced;
        res.names.push_back(e.first);
    }
    return res;
}

bool GetActiveScheme(GUID& g) {
    GUID* p = nullptr;
    if (PowerGetActiveScheme(nullptr, &p) != ERROR_SUCCESS || !p) return false;
    g = *p;
    LocalFree(p);
    return true;
}

bool SetActiveScheme(const GUID& g) { return PowerSetActiveScheme(nullptr, &g) == ERROR_SUCCESS; }

bool FindPerformanceScheme(GUID& out) {
    bool hasHigh = false, hasUltimate = false;
    for (ULONG i = 0;; ++i) {
        GUID g{}; DWORD size = sizeof g;
        if (PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, i, reinterpret_cast<UCHAR*>(&g), &size) != ERROR_SUCCESS) break;
        if (IsEqualGUID(g, kUltimatePerformance)) hasUltimate = true;
        if (IsEqualGUID(g, kHighPerformance)) hasHigh = true;
    }
    if (hasUltimate) { out = kUltimatePerformance; return true; }
    if (hasHigh) { out = kHighPerformance; return true; }
    return false;
}

namespace {
    const wchar_t* const kToastKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\PushNotifications";
}

ToastState GetToast() {
    ToastState s;
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kToastKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return s;
    DWORD type = 0, v = 0, size = sizeof v;
    if (RegQueryValueExW(k, L"ToastEnabled", nullptr, &type, reinterpret_cast<BYTE*>(&v), &size) == ERROR_SUCCESS && type == REG_DWORD) { s.exists = true; s.value = v; }
    RegCloseKey(k);
    return s;
}

bool SetToast(const ToastState& s) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kToastKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r = s.exists ? RegSetValueExW(k, L"ToastEnabled", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&s.value), sizeof s.value)
                      : RegDeleteValueW(k, L"ToastEnabled");
    RegCloseKey(k);
    return r == ERROR_SUCCESS || (!s.exists && r == ERROR_FILE_NOT_FOUND);
}

namespace {
    struct GameTweakSpec { const wchar_t* key; const wchar_t* name; DWORD wanted; };
    const GameTweakSpec kGameTweaks3[kGameTweaks] = {
        { L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", 1 },                     // Windows-Spielmodus an
        { L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled", 0 },  // Hintergrundaufnahme aus
        { L"System\\GameConfigStore", L"GameDVR_Enabled", 0 },                              // Game-Bar-Aufzeichnung aus
    };
}

DWORD GameTweakWanted(int i) { return (i >= 0 && i < kGameTweaks) ? kGameTweaks3[i].wanted : 0; }

RegDword GetGameTweak(int i) {
    RegDword r;
    if (i < 0 || i >= kGameTweaks) return r;
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kGameTweaks3[i].key, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return r;
    DWORD type = 0, v = 0, size = sizeof v;
    if (RegQueryValueExW(k, kGameTweaks3[i].name, nullptr, &type, reinterpret_cast<BYTE*>(&v), &size) == ERROR_SUCCESS && type == REG_DWORD) { r.exists = true; r.value = v; }
    RegCloseKey(k);
    return r;
}

bool SetGameTweak(int i, const RegDword& v) {
    if (i < 0 || i >= kGameTweaks) return false;
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kGameTweaks3[i].key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r = v.exists ? RegSetValueExW(k, kGameTweaks3[i].name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v.value), sizeof v.value)
                      : RegDeleteValueW(k, kGameTweaks3[i].name);
    RegCloseKey(k);
    return r == ERROR_SUCCESS || (!v.exists && r == ERROR_FILE_NOT_FOUND);
}

// ---- Persistenz ----
std::wstring DataDir() {
    if (!g_dataDir.empty()) return g_dataDir;
    wchar_t local[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local))) return L".";
    g_dataDir = std::wstring(local) + L"\\DuncansBoost";
    CreateDirectoryW(g_dataDir.c_str(), nullptr);
    return g_dataDir;
}

void SetDataDirForTest(const std::wstring& dir) { g_dataDir = dir; CreateDirectoryW(dir.c_str(), nullptr); }

std::wstring Join(const std::vector<std::wstring>& v) {
    std::wstring r;
    for (size_t i = 0; i < v.size(); ++i) { if (i) r += L'|'; r += v[i]; }
    return r;
}

std::vector<std::wstring> Split(const std::wstring& s) {
    std::vector<std::wstring> v;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t p = s.find(L'|', start);
        const std::wstring part = s.substr(start, p == std::wstring::npos ? std::wstring::npos : p - start);
        if (!part.empty()) v.push_back(part);
        if (p == std::wstring::npos) break;
        start = p + 1;
    }
    return v;
}

std::wstring GuidToString(const GUID& g) {
    wchar_t buf[64] = {};
    StringFromGUID2(g, buf, 64);
    return buf;
}

bool GuidFromString(const std::wstring& s, GUID& g) { return !s.empty() && CLSIDFromString(s.c_str(), &g) == S_OK; }

bool SettingsFileExists() { return GetFileAttributesW(IniPath(L"settings.ini").c_str()) != INVALID_FILE_ATTRIBUTES; }

std::vector<std::wstring> SuggestedHelpers() {
    return { L"onedrive.exe", L"dropbox.exe", L"googledrivefs.exe", L"teams.exe", L"ms-teams.exe", L"skype.exe", L"slack.exe" };
}

int ClampClipSeconds(int s)
{
    int best = kClipLengths[1];
    int bestDist = 1 << 30;
    for (int v : kClipLengths) {
        const int d = s > v ? s - v : v - s;
        if (d < bestDist) { bestDist = d; best = v; }
    }
    return best;
}

int NextClipSeconds(int s)
{
    s = ClampClipSeconds(s);
    for (int i = 0; i < 3; ++i)
        if (kClipLengths[i] == s) return kClipLengths[(i + 1) % 3];
    return kClipLengths[1];
}

bool ClipShouldRecord(const std::vector<std::wstring>& gameExes, const std::wstring& foregroundExe)
{
    if (gameExes.empty()) return true;
    return !foregroundExe.empty() && std::find(gameExes.begin(), gameExes.end(), foregroundExe) != gameExes.end();
}

Settings LoadSettings() {
    Settings s;
    const std::wstring f = IniPath(L"settings.ini");
    s.closeApps = Split(ReadIni(f, L"Settings", L"CloseApps"));
    s.gameApps = Split(ReadIni(f, L"Settings", L"GameApps"));
    s.optPower = ReadIni(f, L"Settings", L"Power", L"1") == L"1";
    s.optToast = ReadIni(f, L"Settings", L"Toast", L"0") == L"1";
    s.optAuto = ReadIni(f, L"Settings", L"Auto", L"0") == L"1";
    s.optRestart = ReadIni(f, L"Settings", L"Restart", L"1") == L"1";
    s.optDuck = ReadIni(f, L"Settings", L"Duck", L"0") == L"1";
    s.optMuteGame = ReadIni(f, L"Settings", L"MuteGame", L"0") == L"1";
    s.optCross = ReadIni(f, L"Settings", L"Cross", L"0") == L"1";
    s.crossStyle = static_cast<int>(wcstol(ReadIni(f, L"Settings", L"CrossStyle", L"0").c_str(), nullptr, 10));
    s.crossColor = static_cast<int>(wcstol(ReadIni(f, L"Settings", L"CrossColor", L"0").c_str(), nullptr, 10));
    s.crossSize = static_cast<int>(wcstol(ReadIni(f, L"Settings", L"CrossSize", L"1").c_str(), nullptr, 10));
    s.optClip = ReadIni(f, L"Settings", L"Clip", L"0") == L"1";
    s.clipSeconds = ClampClipSeconds(static_cast<int>(wcstol(ReadIni(f, L"Settings", L"ClipSeconds", L"30").c_str(), nullptr, 10)));
    s.optOverlay = ReadIni(f, L"Settings", L"Overlay", L"0") == L"1";
    s.overlayCorner = static_cast<int>(wcstol(ReadIni(f, L"Settings", L"OverlayCorner", L"1").c_str(), nullptr, 10));
    if (s.overlayCorner < 0 || s.overlayCorner > 3) s.overlayCorner = 1;
    s.overlayStats = ReadIni(f, L"Settings", L"OverlayStats", L"1") != L"0";
    return s;
}

bool SaveSettings(const Settings& s) {
    const std::wstring f = IniPath(L"settings.ini");
    bool ok = WriteIni(f, L"Settings", L"CloseApps", Join(s.closeApps));
    ok &= WriteIni(f, L"Settings", L"GameApps", Join(s.gameApps));
    ok &= WriteIni(f, L"Settings", L"Power", s.optPower ? L"1" : L"0");
    ok &= WriteIni(f, L"Settings", L"Toast", s.optToast ? L"1" : L"0");
    ok &= WriteIni(f, L"Settings", L"Auto", s.optAuto ? L"1" : L"0");
    ok &= WriteIni(f, L"Settings", L"Restart", s.optRestart ? L"1" : L"0");
    ok &= WriteIni(f, L"Settings", L"Duck", s.optDuck ? L"1" : L"0");
    ok &= WriteIni(f, L"Settings", L"MuteGame", s.optMuteGame ? L"1" : L"0");
    ok &= WriteIni(f, L"Settings", L"Cross", s.optCross ? L"1" : L"0");
    ok &= WriteIni(f, L"Settings", L"CrossStyle", std::to_wstring(s.crossStyle));
    ok &= WriteIni(f, L"Settings", L"CrossColor", std::to_wstring(s.crossColor));
    ok &= WriteIni(f, L"Settings", L"CrossSize", std::to_wstring(s.crossSize));
    ok &= WriteIni(f, L"Settings", L"Clip", s.optClip ? L"1" : L"0");
    ok &= WriteIni(f, L"Settings", L"ClipSeconds", std::to_wstring(ClampClipSeconds(s.clipSeconds)));
    ok &= WriteIni(f, L"Settings", L"Overlay", s.optOverlay ? L"1" : L"0");
    ok &= WriteIni(f, L"Settings", L"OverlayCorner", std::to_wstring(s.overlayCorner < 0 || s.overlayCorner > 3 ? 1 : s.overlayCorner));
    ok &= WriteIni(f, L"Settings", L"OverlayStats", s.overlayStats ? L"1" : L"0");
    return ok;
}

State LoadState() {
    State s;
    const std::wstring f = IniPath(L"state.ini");
    s.active = ReadIni(f, L"State", L"Active", L"0") == L"1";
    s.auto_ = ReadIni(f, L"State", L"Auto", L"0") == L"1";
    s.powerChanged = ReadIni(f, L"State", L"PowerChanged", L"0") == L"1" && GuidFromString(ReadIni(f, L"State", L"PrevScheme"), s.prevScheme);
    s.toastChanged = ReadIni(f, L"State", L"ToastChanged", L"0") == L"1";
    s.prevToast.exists = ReadIni(f, L"State", L"PrevToastExists", L"0") == L"1";
    s.prevToast.value = static_cast<DWORD>(wcstoul(ReadIni(f, L"State", L"PrevToast", L"1").c_str(), nullptr, 10));
    s.gameMask = static_cast<unsigned>(wcstoul(ReadIni(f, L"State", L"GameMask", L"0").c_str(), nullptr, 10));
    for (int i = 0; i < kGameTweaks; ++i) {
        s.prevGame[i].exists = ReadIni(f, L"State", (L"PrevGameExists" + std::to_wstring(i)).c_str(), L"0") == L"1";
        s.prevGame[i].value = static_cast<DWORD>(wcstoul(ReadIni(f, L"State", (L"PrevGame" + std::to_wstring(i)).c_str(), L"0").c_str(), nullptr, 10));
    }
    s.closedPaths = Split(ReadIni(f, L"State", L"ClosedPaths"));
    return s;
}

bool SaveState(const State& s) {
    const std::wstring f = IniPath(L"state.ini");
    bool ok = WriteIni(f, L"State", L"Active", s.active ? L"1" : L"0");
    ok &= WriteIni(f, L"State", L"Auto", s.auto_ ? L"1" : L"0");
    ok &= WriteIni(f, L"State", L"PowerChanged", s.powerChanged ? L"1" : L"0");
    ok &= WriteIni(f, L"State", L"PrevScheme", s.powerChanged ? GuidToString(s.prevScheme) : L"");
    ok &= WriteIni(f, L"State", L"ToastChanged", s.toastChanged ? L"1" : L"0");
    ok &= WriteIni(f, L"State", L"PrevToastExists", s.prevToast.exists ? L"1" : L"0");
    ok &= WriteIni(f, L"State", L"PrevToast", std::to_wstring(s.prevToast.value));
    ok &= WriteIni(f, L"State", L"GameMask", std::to_wstring(s.gameMask));
    for (int i = 0; i < kGameTweaks; ++i) {
        ok &= WriteIni(f, L"State", (L"PrevGameExists" + std::to_wstring(i)).c_str(), s.prevGame[i].exists ? L"1" : L"0");
        ok &= WriteIni(f, L"State", (L"PrevGame" + std::to_wstring(i)).c_str(), std::to_wstring(s.prevGame[i].value));
    }
    ok &= WriteIni(f, L"State", L"ClosedPaths", Join(s.closedPaths));
    return ok;
}

bool Activate(const Settings& cfg, bool autoTriggered, std::wstring& note) {
    State old = LoadState();
    if (old.active) { note = L"Der Zock-Modus ist bereits aktiv."; return true; }

    State s;
    s.active = true;
    s.auto_ = autoTriggered;
    GUID target{};
    bool havePerf = false;
    if (cfg.optPower && GetActiveScheme(s.prevScheme)) havePerf = FindPerformanceScheme(target);
    s.prevToast = GetToast();
    for (int i = 0; i < kGameTweaks; ++i) s.prevGame[i] = GetGameTweak(i);   // vorher sichern, auch wenn sie nicht gesetzt werden
    SaveState(s);                                       // Marker zuerst: bricht das Programm ab, laesst sich alles wiederherstellen
    Log(std::wstring(L"Zock-Modus an") + (autoTriggered ? L" (Spiel erkannt)" : L""));

    std::wstring parts;
    auto add = [&](const std::wstring& t) { if (!parts.empty()) parts += L", "; parts += t; };

    if (cfg.optPower) {
        if (havePerf) {
            GUID cur{}; GetActiveScheme(cur);
            if (IsEqualGUID(cur, target)) add(L"Höchstleistung war schon aktiv");
            else if (SetActiveScheme(target)) { s.powerChanged = true; add(L"Höchstleistung aktiv"); }
            else add(L"Energiesparplan nicht umgestellt");
        } else add(L"Höchstleistung nicht verfügbar");
        SaveState(s);
        // Spielmodus an, Hintergrundaufnahme aus: gehoert zur Spiel-Optimierung (wie Hoechstleistung immer dabei)
        int changed = 0;
        for (int i = 0; i < kGameTweaks; ++i) {
            if (s.prevGame[i].exists && s.prevGame[i].value == GameTweakWanted(i)) continue;   // steht schon richtig
            if (SetGameTweak(i, { true, GameTweakWanted(i) })) { s.gameMask |= 1u << i; ++changed; }
        }
        if (changed) add(L"Spielmodus an, Hintergrundaufnahme aus");
        SaveState(s);
    }
    if (cfg.optToast) {
        if (s.prevToast.exists && s.prevToast.value == 0) add(L"Benachrichtigungen waren schon aus");
        else if (SetToast({ true, 0 })) { s.toastChanged = true; add(L"Benachrichtigungen aus"); }
        SaveState(s);
    }
    if (!cfg.closeApps.empty()) {
        const CloseResult r = CloseApps(cfg.closeApps, 8000, true);
        s.closedPaths = r.paths;
        add(std::to_wstring(r.closed) + (r.closed == 1 ? L" Programm beendet" : L" Programme beendet"));
        if (r.failed) add(std::to_wstring(r.failed) + L" nicht beendet");
        for (const std::wstring& n : r.names) Log(L"beendet: " + n);
        SaveState(s);
    }
    note = parts.empty() ? L"Zock-Modus aktiv." : parts + L".";
    return true;
}

bool Deactivate(const Settings& cfg, std::wstring& note) {
    State s = LoadState();
    if (!s.active) { note = L"Der Zock-Modus ist nicht aktiv."; return true; }
    std::wstring parts;
    auto add = [&](const std::wstring& t) { if (!parts.empty()) parts += L", "; parts += t; };
    bool ok = true;

    if (s.powerChanged) {
        if (SetActiveScheme(s.prevScheme)) add(L"Energiesparplan wiederhergestellt"); else { ok = false; add(L"Energiesparplan konnte nicht wiederhergestellt werden"); }
    }
    {
        bool gameOk = true;
        for (int i = 0; i < kGameTweaks; ++i)
            if (s.gameMask & (1u << i)) gameOk &= SetGameTweak(i, s.prevGame[i]);
        if (s.gameMask) {
            if (gameOk) add(L"Spiel-Einstellungen wiederhergestellt"); else { ok = false; add(L"Spiel-Einstellungen konnten nicht wiederhergestellt werden"); }
        }
    }
    if (s.toastChanged) {
        if (SetToast(s.prevToast)) add(L"Benachrichtigungen wiederhergestellt"); else { ok = false; add(L"Benachrichtigungen konnten nicht wiederhergestellt werden"); }
    }
    int started = 0;
    if (cfg.optRestart) {
        for (const std::wstring& p : s.closedPaths) {
            if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWMINNOACTIVE)) > 32) ++started;
        }
        if (started) add(std::to_wstring(started) + (started == 1 ? L" Programm gestartet" : L" Programme gestartet"));
    }
    State clean;                                        // Zustand zuruecksetzen (nur wenn alles wiederhergestellt ist, sonst bleibt der Marker)
    if (ok) SaveState(clean);
    Log(L"Zock-Modus aus: " + parts);
    note = parts.empty() ? L"Zock-Modus beendet." : parts + L".";
    return ok;
}

}  // namespace boostmode
