#include "GameScan.h"

#include <algorithm>
#include <cwctype>
#include <set>

namespace boostmode {

namespace {

std::wstring Wide(const std::string& s)
{
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::wstring Lower(std::wstring s)
{
    for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

// liest einen "..."-Wert ab Position p (nach dem oeffnenden Anfuehrungszeichen), loest \\ und \" auf
std::string ReadQuoted(const std::string& t, size_t p, size_t* endOut = nullptr)
{
    std::string v;
    while (p < t.size() && t[p] != '"') {
        if (t[p] == '\\' && p + 1 < t.size()) {
            const char n = t[p + 1];
            if (n == '\\' || n == '"' || n == '/') { v += n; p += 2; continue; }
            if (n == 'n' || n == 't') { v += ' '; p += 2; continue; }
        }
        v += t[p++];
    }
    if (endOut) *endOut = p;
    return v;
}

// Wert zu einem Schluessel suchen: "key" <Leerraum/Doppelpunkt> "wert" (VDF und JSON)
bool FindValue(const std::string& t, const std::string& key, std::string& out)
{
    const std::string needle = "\"" + key + "\"";
    size_t pos = 0;
    while ((pos = t.find(needle, pos)) != std::string::npos) {
        size_t p = pos + needle.size();
        while (p < t.size() && (t[p] == ' ' || t[p] == '\t' || t[p] == ':' || t[p] == '\r' || t[p] == '\n')) ++p;
        if (p < t.size() && t[p] == '"') { out = ReadQuoted(t, p + 1); return true; }
        pos += needle.size();
    }
    return false;
}

bool ReadFileText(const std::wstring& path, std::string& out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    bool ok = false;
    if (sz.QuadPart > 0 && sz.QuadPart < (4 << 20)) {
        out.assign(static_cast<size_t>(sz.QuadPart), '\0');
        DWORD got = 0;
        ok = ReadFile(h, &out[0], static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size();
    }
    CloseHandle(h);
    return ok;
}

bool DirExists(const std::wstring& p)
{
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// EXE-Dateien in einem Ordner bis zur angegebenen Tiefe
void CollectExes(const std::wstring& dir, int depth, std::vector<std::pair<std::wstring, unsigned long long>>& out)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth > 0 && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) CollectExes(dir + L"\\" + name, depth - 1, out);
        } else if (name.size() > 4 && Lower(name.substr(name.size() - 4)) == L".exe") {
            out.push_back({ Lower(name), (static_cast<unsigned long long>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow });
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

std::wstring BaseName(const std::wstring& p)
{
    const size_t k = p.find_last_of(L"\\/");
    return k == std::wstring::npos ? p : p.substr(k + 1);
}

}  // namespace

std::vector<std::wstring> ParseSteamLibraries(const std::string& vdf)
{
    std::vector<std::wstring> out;
    const std::string needle = "\"path\"";
    size_t pos = 0;
    while ((pos = vdf.find(needle, pos)) != std::string::npos) {
        size_t p = pos + needle.size();
        pos = p;
        while (p < vdf.size() && (vdf[p] == ' ' || vdf[p] == '\t')) ++p;
        if (p >= vdf.size() || vdf[p] != '"') continue;
        const std::wstring v = Wide(ReadQuoted(vdf, p + 1));
        if (!v.empty() && std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
    }
    return out;
}

bool ParseSteamManifest(const std::string& acf, std::wstring& name, std::wstring& installDir)
{
    std::string n, d;
    if (!FindValue(acf, "name", n) || !FindValue(acf, "installdir", d) || n.empty() || d.empty()) return false;
    if (d.find("..") != std::string::npos || d.find(':') != std::string::npos || d[0] == '\\' || d[0] == '/') return false;    // nur Unterordner
    name = Wide(n);
    installDir = Wide(d);
    return true;
}

bool ParseEpicManifest(const std::string& json, std::wstring& name, std::wstring& installLocation, std::wstring& exe)
{
    std::string n, loc, ex;
    if (!FindValue(json, "DisplayName", n) || !FindValue(json, "InstallLocation", loc) || n.empty() || loc.empty()) return false;
    FindValue(json, "LaunchExecutable", ex);
    name = Wide(n);
    installLocation = Wide(loc);
    exe = Lower(BaseName(Wide(ex)));
    if (!exe.empty() && (exe.size() < 5 || exe.substr(exe.size() - 4) != L".exe")) exe.clear();
    return true;
}

bool IsHelperExe(const std::wstring& f)
{
    static const wchar_t* const kBad[] = { L"unins", L"crash", L"redist", L"setup", L"install", L"vcredist", L"vc_redist", L"dxsetup", L"dotnet", L"directx",
                                           L"easyanticheat", L"beservice", L"battleye", L"anticheat", L"cefprocess", L"cefsharp", L"ue4prereq", L"updater",
                                           L"bootstrap", L"report", L"helper", L"webhelper", L"benchmark", L"eosoverlay", L"notification_helper" };
    for (const wchar_t* b : kBad)
        if (f.find(b) != std::wstring::npos) return true;
    return false;
}

bool IsToolName(const std::wstring& gameName)
{
    const std::wstring n = Lower(gameName);
    static const wchar_t* const kTools[] = { L"redistributable", L"steamworks", L"proton", L"linux runtime", L"steam linux", L"steamvr", L"sdk", L"dedicated server" };
    for (const wchar_t* t : kTools)
        if (n.find(t) != std::wstring::npos) return true;
    return false;
}

std::wstring PickMainExe(const std::vector<std::pair<std::wstring, unsigned long long>>& candidates)
{
    std::wstring best, bestAny;
    unsigned long long bestSize = 0, bestAnySize = 0;
    for (const auto& c : candidates) {
        if (c.second >= bestAnySize) { bestAnySize = c.second; bestAny = c.first; }
        if (!IsHelperExe(c.first) && c.second >= bestSize) { bestSize = c.second; best = c.first; }
    }
    return best.empty() ? bestAny : best;
}

std::vector<InstalledGame> ScanInstalledGames()
{
    std::vector<InstalledGame> out;
    std::set<std::wstring> seen;
    auto add = [&](const std::wstring& name, const std::wstring& exe, const wchar_t* store) {
        if (name.empty() || exe.empty() || IsToolName(name) || !seen.insert(exe).second) return;
        out.push_back({ name, exe, store });
    };

    // Steam
    std::wstring steam;
    for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        const wchar_t* sub = root == HKEY_CURRENT_USER ? L"Software\\Valve\\Steam" : L"SOFTWARE\\WOW6432Node\\Valve\\Steam";
        const wchar_t* val = root == HKEY_CURRENT_USER ? L"SteamPath" : L"InstallPath";
        HKEY k = nullptr;
        if (RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
            wchar_t buf[MAX_PATH] = {};
            DWORD sz = sizeof(buf) - sizeof(wchar_t), type = 0;
            if (RegQueryValueExW(k, val, nullptr, &type, reinterpret_cast<BYTE*>(buf), &sz) == ERROR_SUCCESS && type == REG_SZ) steam = buf;
            RegCloseKey(k);
        }
        if (!steam.empty()) break;
    }
    if (!steam.empty()) {
        for (wchar_t& c : steam) if (c == L'/') c = L'\\';
        std::vector<std::wstring> libs = { steam };
        std::string vdf;
        if (ReadFileText(steam + L"\\steamapps\\libraryfolders.vdf", vdf))
            for (const std::wstring& l : ParseSteamLibraries(vdf))
                if (std::find(libs.begin(), libs.end(), l) == libs.end()) libs.push_back(l);
        for (const std::wstring& lib : libs) {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((lib + L"\\steamapps\\appmanifest_*.acf").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) continue;
            do {
                std::string acf;
                std::wstring name, dir;
                if (!ReadFileText(lib + L"\\steamapps\\" + fd.cFileName, acf) || !ParseSteamManifest(acf, name, dir) || IsToolName(name)) continue;
                const std::wstring full = lib + L"\\steamapps\\common\\" + dir;
                if (!DirExists(full)) continue;
                std::vector<std::pair<std::wstring, unsigned long long>> exes;
                CollectExes(full, 2, exes);
                add(name, PickMainExe(exes), L"Steam");
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }

    // Epic Games
    wchar_t pd[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"ProgramData", pd, MAX_PATH) > 0) {
        const std::wstring dir = std::wstring(pd) + L"\\Epic\\EpicGamesLauncher\\Data\\Manifests";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*.item").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::string json;
                std::wstring name, loc, exe;
                if (!ReadFileText(dir + L"\\" + fd.cFileName, json) || !ParseEpicManifest(json, name, loc, exe)) continue;
                if (exe.empty() || IsHelperExe(exe)) {
                    std::vector<std::pair<std::wstring, unsigned long long>> exes;
                    CollectExes(loc, 2, exes);
                    exe = PickMainExe(exes);
                }
                add(name, exe, L"Epic");
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }

    std::sort(out.begin(), out.end(), [](const InstalledGame& a, const InstalledGame& b) { return Lower(a.name) < Lower(b.name); });
    return out;
}

}  // namespace boostmode
