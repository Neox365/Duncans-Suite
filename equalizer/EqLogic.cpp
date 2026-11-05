#include "EqLogic.h"

#include <shlobj.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace eq {

const int kFreqs[kBands] = { 31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
const wchar_t* const kConfigFile = L"duncans-equalizer.txt";

namespace {

const Preset kPresets[] = {
    { L"Flach",   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { L"Bass",    { 6, 5, 4, 2, 0, 0, 0, 0, 0, 0 } },
    { L"Musik",   { 4, 3, 1, 0, -1, -1, 0, 2, 3, 4 } },
    { L"Spiele",  { 2, 1, 0, -1, 0, 2, 4, 4, 2, 0 } },
    { L"Film",    { 4, 3, 2, 0, 0, 0, 1, 2, 3, 2 } },
    { L"Stimmen", { -4, -3, -1, 0, 2, 4, 4, 2, 0, -1 } },
};

std::string Num(int v) { return std::to_string(v); }

bool ReadFileBytes(const std::wstring& path, std::string& out)
{
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    char buf[8192];
    DWORD n = 0;
    while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n)
        out.append(buf, n);
    CloseHandle(h);
    return true;
}

// 0 = ok, sonst GetLastError()
DWORD WriteFileBytes(const std::wstring& path, const std::string& data)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError();
    DWORD written = 0;
    const BOOL ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    const DWORD err = ok && written == data.size() ? 0 : (GetLastError() ? GetLastError() : ERROR_WRITE_FAULT);
    CloseHandle(h);
    return err;
}

bool DirExists(const std::wstring& p)
{
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

}  // namespace

int PresetCount() { return static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0])); }
const Preset& PresetAt(int index) { return kPresets[std::clamp(index, 0, PresetCount() - 1)]; }
int ClampGain(int db) { return std::clamp(db, -kMaxGain, kMaxGain); }

int MatchPreset(const Settings& s)
{
    for (int i = 0; i < PresetCount(); ++i)
    {
        bool same = true;
        for (int b = 0; b < kBands; ++b)
            if (kPresets[i].gains[b] != s.gains[b])
                same = false;
        if (same)
            return i;
    }
    return -1;
}

int PreampDb(const Settings& s)
{
    int peak = 0;
    for (int b = 0; b < kBands; ++b)
        peak = std::max(peak, s.gains[b]);
    if (s.bass)
        peak += 4;
    if (s.voice)
        peak += 3;
    return -std::min(peak, 18);
}

std::string BuildConfig(const Settings& s)
{
    std::string t = "# Duncan's Equalizer - wird automatisch erzeugt, bitte nicht von Hand aendern\r\n";
    if (!s.enabled)
        return t + "# Equalizer ist ausgeschaltet\r\n";
    bool anyBand = false;
    for (int b = 0; b < kBands; ++b)
        if (s.gains[b] != 0)
            anyBand = true;
    if (!anyBand && !s.bass && !s.voice)
        return t + "# Alle Regler stehen auf 0\r\n";
    t += "Preamp: " + Num(PreampDb(s)) + " dB\r\n";
    if (anyBand)
    {
        t += "GraphicEQ: ";
        for (int b = 0; b < kBands; ++b)
        {
            if (b)
                t += "; ";
            t += Num(kFreqs[b]) + " " + Num(ClampGain(s.gains[b]));
        }
        t += "\r\n";
    }
    if (s.bass)
        t += "Filter: ON LSC Fc 110 Hz Gain 4 dB\r\n";
    if (s.voice)
    {
        t += "Filter: ON PK Fc 250 Hz Gain -2 dB Q 1.0\r\n";
        t += "Filter: ON PK Fc 3000 Hz Gain 3 dB Q 1.0\r\n";
    }
    return t;
}

std::wstring SettingsPath()
{
    wchar_t base[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base)))
        return L"";
    const std::wstring dir = std::wstring(base) + L"\\DuncansEqualizer";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.txt";
}

bool SaveSettings(const Settings& s, const std::wstring& path)
{
    std::string t = std::string("enabled=") + (s.enabled ? "1" : "0") + "\r\nbass=" + (s.bass ? "1" : "0") +
                    "\r\nvoice=" + (s.voice ? "1" : "0") + "\r\ngains=";
    for (int b = 0; b < kBands; ++b)
        t += (b ? "," : "") + Num(ClampGain(s.gains[b]));
    t += "\r\n";
    return WriteFileBytes(path, t) == 0;
}

bool LoadSettings(Settings& s, const std::wstring& path)
{
    s = Settings{};
    std::string data;
    if (!ReadFileBytes(path, data))
        return false;
    size_t pos = 0;
    while (pos < data.size())
    {
        size_t e = data.find('\n', pos);
        if (e == std::string::npos)
            e = data.size();
        const std::string line = Trim(data.substr(pos, e - pos));
        pos = e + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        if (key == "enabled") s.enabled = val == "1";
        else if (key == "bass") s.bass = val == "1";
        else if (key == "voice") s.voice = val == "1";
        else if (key == "gains")
        {
            size_t p = 0;
            for (int b = 0; b < kBands && p <= val.size(); ++b)
            {
                size_t c = val.find(',', p);
                if (c == std::string::npos)
                    c = val.size();
                s.gains[b] = ClampGain(std::atoi(val.substr(p, c - p).c_str()));
                p = c + 1;
            }
        }
    }
    return true;
}

std::wstring FindInstallDir()
{
    wchar_t buf[MAX_PATH] = {};
    DWORD size = sizeof(buf);
    HKEY key = nullptr;
    std::wstring found;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\EqualizerAPO", 0, KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS)
    {
        DWORD type = 0;
        if (RegQueryValueExW(key, L"InstallPath", nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS && type == REG_SZ)
            found = buf;
        RegCloseKey(key);
    }
    if (found.empty())
    {
        wchar_t pf[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, pf)))
            found = std::wstring(pf) + L"\\EqualizerAPO";
    }
    while (!found.empty() && (found.back() == L'\\' || found.back() == L'/'))
        found.pop_back();
    return !found.empty() && DirExists(found + L"\\config") ? found : std::wstring();
}

ApplyResult ApplyToDir(const std::wstring& installDir, const Settings& s)
{
    if (installDir.empty() || !DirExists(installDir + L"\\config"))
        return ApplyResult::NotInstalled;
    const std::wstring cfgDir = installDir + L"\\config";

    // 1. unsere Datei
    DWORD err = WriteFileBytes(cfgDir + L"\\" + kConfigFile, BuildConfig(s));
    if (err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION)
        return err == ERROR_ACCESS_DENIED ? ApplyResult::AccessDenied : ApplyResult::Failed;
    if (err)
        return ApplyResult::Failed;

    // 2. Include-Zeile in config.txt (nur anhaengen, vorhandenen Inhalt nie veraendern)
    const std::wstring mainPath = cfgDir + L"\\config.txt";
    std::string main;
    ReadFileBytes(mainPath, main);
    if (main.size() >= 2 && static_cast<unsigned char>(main[0]) == 0xFF && static_cast<unsigned char>(main[1]) == 0xFE)
        return ApplyResult::Failed;     // UTF-16: nicht anfassen
    const std::string includeLine = "Include: duncans-equalizer.txt";
    size_t pos = 0;
    bool has = false;
    while (pos < main.size())
    {
        size_t e = main.find('\n', pos);
        if (e == std::string::npos)
            e = main.size();
        if (Trim(main.substr(pos, e - pos)) == includeLine)
            has = true;
        pos = e + 1;
    }
    if (!has)
    {
        if (!main.empty() && main.back() != '\n')
            main += "\r\n";
        main += includeLine + "\r\n";
        err = WriteFileBytes(mainPath, main);
        if (err == ERROR_ACCESS_DENIED)
            return ApplyResult::AccessDenied;
        if (err)
            return ApplyResult::Failed;
    }
    return ApplyResult::Applied;
}

}  // namespace eq
