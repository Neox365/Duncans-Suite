#include "EqLogic.h"

#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

// Eine Graphic-EQ-Zeile aus einer Datei (z. B. AutoEq "GraphicEQ.txt"): "GraphicEQ: 20 -1.5; 25 -1.2; ..." mit hoechstens 400 Punkten
bool IsSafeGraphicLine(const std::string& l)
{
    if (l.size() < 14 || l.size() > 8000 || l.compare(0, 11, "GraphicEQ: ") != 0)
        return false;
    size_t points = 0;
    for (size_t i = 11; i < l.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(l[i]);
        if (ch == ';')
            ++points;
        else if (!(std::isdigit(ch) || ch == ' ' || ch == '.' || ch == '-'))
            return false;
    }
    return points < 400;
}

bool IsSafeFilterLine(const std::string& l)
{
    if (l.compare(0, 11, "GraphicEQ: ") == 0)
        return IsSafeGraphicLine(l);
    if (l.empty() || l.size() > 160)
        return false;
    if (l.compare(0, 7, "Preamp:") != 0 && l.compare(0, 7, "Filter:") != 0)
        return false;
    for (unsigned char ch : l)
        if (!(std::isalnum(ch) || ch == ' ' || ch == '.' || ch == ',' || ch == '+' || ch == '-' || ch == '_' || ch == ':'))
            return false;
    return true;
}

std::wstring ToWide(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string ToUtf8(const std::wstring& w)
{
    if (w.empty())
        return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring LowerW(std::wstring s)
{
    if (!s.empty())
        CharLowerBuffW(&s[0], static_cast<DWORD>(s.size()));
    return s;
}

}  // namespace

namespace {
const wchar_t* const kBandNames[kBands] = { L"Tiefbass", L"Bass", L"Druck", L"Wärme", L"Körper", L"Mitten", L"Stimme", L"Klarheit", L"Zischen", L"Glanz" };
const wchar_t* const kBandHints[kBands] = {
    L"31 Hz, Tiefbass: das Grollen unter dem Bass (Explosionen, Kickdrum). Zu viel dröhnt.",
    L"63 Hz, Bass: der Wumms bei Beats und Bassgitarre. Zu viel wummert.",
    L"125 Hz, Druck: gibt dem Bass Fülle und Druck. Zu viel klingt dröhnend.",
    L"250 Hz, Wärme: macht den Klang warm. Zu viel klingt dumpf, zu wenig dünn.",
    L"500 Hz, Körper: Grundton vieler Instrumente. Zu viel klingt aufgebläht.",
    L"1 kHz, Mitten: Kern von Stimmen und Instrumenten. Zu viel klingt nasal.",
    L"2 kHz, Stimme: Präsenz von Stimmen und Gitarren. Zu viel klingt hart.",
    L"4 kHz, Klarheit: macht Sprache und Details deutlich. Zu viel klingt scharf.",
    L"8 kHz, Zischen: S-Laute und Becken. Zu viel klingt spitz.",
    L"16 kHz, Glanz: Luft und Brillanz ganz oben. Zu viel klingt rauschig.",
};
}  // namespace

const wchar_t* BandName(int band) { return kBandNames[std::clamp(band, 0, kBands - 1)]; }
const wchar_t* BandHint(int band) { return kBandHints[std::clamp(band, 0, kBands - 1)]; }

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

int TotalPreampDb(const Settings& s)
{
    return std::clamp(PreampDb(s) + std::clamp(s.preamp, -kMaxGain, kMaxGain), -30, 12);
}

const wchar_t* const kIrCopyName = L"duncans-ir.wav";

// Absoluter Pfad mit Laufwerk und nur harmlosen Zeichen (kein Zeilenumbruch, kein Semikolon usw.), damit nichts in die Konfiguration gelangt
static bool IsSafeConfigPath(const std::string& p)
{
    if (p.size() < 4 || p.size() > 240 || !std::isalpha(static_cast<unsigned char>(p[0])) || p[1] != ':' || (p[2] != '\\' && p[2] != '/'))
        return false;
    for (unsigned char c : p)
        if (!(std::isalnum(c) || c == ' ' || c == ':' || c == '\\' || c == '/' || c == '.' || c == '_' || c == '-' || c == '(' || c == ')' || c == '~'))
            return false;
    return p.find("..") == std::string::npos;
}

bool CheckImpulseFile(const std::wstring& path, std::wstring& err)
{
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        err = L"Die Datei wurde nicht gefunden.";
        return false;
    }
    const unsigned long long size = (static_cast<unsigned long long>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
    if (size > kMaxIrBytes)
    {
        err = L"Die Datei ist größer als 24 MB. Bitte eine kürzere Impulsantwort wählen.";
        return false;
    }
    char head[12] = {};
    DWORD got = 0;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        err = L"Die Datei konnte nicht gelesen werden.";
        return false;
    }
    const bool read = ReadFile(h, head, sizeof head, &got, nullptr) != FALSE;
    CloseHandle(h);
    if (!read || got < sizeof head || std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0 || size < 44)
    {
        err = L"Das ist keine WAV-Datei. Erwartet wird eine Impulsantwort als .wav.";
        return false;
    }
    return true;
}

std::string MicDeviceKey(const std::wstring& name)
{
    std::string best, cur;
    auto flush = [&]() {
        while (!cur.empty() && cur.back() == ' ') cur.pop_back();
        size_t k = 0;
        while (k < cur.size() && cur[k] == ' ') ++k;
        cur = cur.substr(k);
        if (cur.size() > best.size()) best = cur;
        cur.clear();
    };
    for (wchar_t c : name)
    {
        const bool ok = (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || c == L' ' || c == L'.' || c == L'_' || c == L'-';
        if (ok) cur += static_cast<char>(c);
        else flush();
    }
    flush();
    return best.size() >= 3 && best.size() <= 64 ? best : std::string();
}

static std::string BuildMain(const Settings& s, const std::string& irFile);

std::string BuildConfig(const Settings& s, const std::string& irFile)
{
    std::string t = BuildMain(s, irFile);
    const std::string key = MicDeviceKey(s.micDevice);
    if (s.micMode >= 1 && s.micMode <= 3 && !key.empty())          // eigener Abschnitt nur fuer das Mikrofon; "Device: all" beendet ihn
    {
        t += "Device: " + key + "\r\n";
        if (s.micMode & 2) t += "Filter: ON HP Fc 90 Hz\r\n";
        if (s.micMode & 1)
        {
            t += "Filter: ON PK Fc 250 Hz Gain -2 dB Q 1.0\r\n";
            t += "Filter: ON PK Fc 3500 Hz Gain 3 dB Q 1.0\r\n";
        }
        t += "Device: all\r\n";
    }
    return t;
}

std::vector<std::wstring> CaptureDeviceNames()
{
    std::vector<std::wstring> out;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IMMDeviceEnumerator* en = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en))) && en)
    {
        IMMDeviceCollection* col = nullptr;
        UINT n = 0;
        if (SUCCEEDED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col)) && col && SUCCEEDED(col->GetCount(&n)))
            for (UINT i = 0; i < n && out.size() < 12; ++i)
            {
                IMMDevice* dev = nullptr;
                if (FAILED(col->Item(i, &dev)) || !dev) continue;
                IPropertyStore* store = nullptr;
                if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &store)) && store)
                {
                    PROPVARIANT v;
                    PropVariantInit(&v);
                    if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal)
                        out.push_back(v.pwszVal);
                    PropVariantClear(&v);
                    store->Release();
                }
                dev->Release();
            }
        if (col) col->Release();
        en->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return out;
}

static std::string BuildMain(const Settings& s, const std::string& irFile)
{
    std::string t = "# Duncan's Equalizer - wird automatisch erzeugt, bitte nicht von Hand aendern\r\n";
    if (!s.enabled)
        return t + "# Equalizer ist ausgeschaltet\r\n";
    bool anyBand = false;
    for (int b = 0; b < kBands; ++b)
        if (s.gains[b] != 0)
            anyBand = true;
    const int balance = std::clamp(s.balance, -kMaxBalance, kMaxBalance);
    const int delay = std::clamp(s.delay, -kMaxDelay, kMaxDelay);
    const bool useIr = !irFile.empty() && IsSafeConfigPath(irFile);
    if (!anyBand && !s.bass && !s.voice && s.extra.empty() && s.preamp == 0 && balance == 0 && delay == 0 && !s.mono && !useIr)
        return t + "# Alle Regler stehen auf 0\r\n";
    if (TotalPreampDb(s) != 0)
        t += "Preamp: " + Num(TotalPreampDb(s)) + " dB\r\n";
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
    size_t n = 0;
    for (const std::string& line : s.extra)      // nur Zeilen, die wie eingelesene Filterzeilen aussehen (Schutz vor "Include:", "VSTPlugin:" usw.)
    {
        if (n++ >= kMaxExtraLines || !IsSafeFilterLine(line))
            continue;
        t += line + "\r\n";
    }
    if (useIr)
        t += "Convolution: " + irFile + "\r\n";                              // Raum-/Kopfhoerer-Korrektur aus der Impulsantwort
    if (s.mono)
        t += "Copy: L=0.5*L+0.5*R R=0.5*L+0.5*R\r\n";                       // beide Seiten gleich
    if (balance != 0)                                                        // die lautere Seite bleibt, die andere wird leiser
        t += std::string("Channel: ") + (balance < 0 ? "R" : "L") + "\r\nPreamp: -" + Num(std::abs(balance)) + " dB\r\nChannel: all\r\n";
    if (delay != 0)                                                          // die Seite, die spaeter kommen soll, wird verzoegert
        t += std::string("Channel: ") + (delay < 0 ? "L" : "R") + "\r\nDelay: " + Num(std::abs(delay)) + " ms\r\nChannel: all\r\n";
    return t;
}

std::wstring SettingsPath()
{
    wchar_t base[MAX_PATH] = {};
    wchar_t testDir[MAX_PATH] = {};
    const bool testRun = GetEnvironmentVariableW(L"DUNCANS_EQ_TESTDIR", testDir, MAX_PATH) > 0;   // Testlauf: eigene Einstellungen, nie die echten
    if (testRun)
        lstrcpynW(base, (std::wstring(testDir) + L"\\Local").c_str(), MAX_PATH);
    else if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base)))
        return L"";
    const std::wstring dir = std::wstring(base) + L"\\DuncansEqualizer";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir + L"\\settings.txt";
}

bool SaveSettings(const Settings& s, const std::wstring& path)
{
    std::string t = std::string("enabled=") + (s.enabled ? "1" : "0") + "\r\nkeeprunning=" + (s.keepRunning ? "1" : "0") + "\r\nhotkeys=" + (s.hotkeys ? "1" : "0") + "\r\nspectrum=" + (s.spectrum ? "1" : "0") + "\r\nautogain=" + (s.autoGain ? "1" : "0") + "\r\nbass=" + (s.bass ? "1" : "0") +
                    "\r\nvoice=" + (s.voice ? "1" : "0") + "\r\nbalance=" + Num(std::clamp(s.balance, -kMaxBalance, kMaxBalance)) + "\r\nmono=" + (s.mono ? "1" : "0") + "\r\ndelay=" + Num(std::clamp(s.delay, -kMaxDelay, kMaxDelay)) + "\r\nmic=" + Num(s.micMode) + "\r\nmicdevice=" + ToUtf8(s.micDevice) + "\r\nir=" + ToUtf8(s.irPath) + "\r\ngains=";
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
        else if (key == "keeprunning") s.keepRunning = val != "0";
        else if (key == "hotkeys") s.hotkeys = val != "0";
        else if (key == "spectrum") s.spectrum = val != "0";
        else if (key == "autogain") s.autoGain = val == "1";
        else if (key == "bass") s.bass = val == "1";
        else if (key == "voice") s.voice = val == "1";
        else if (key == "balance") s.balance = std::clamp(std::atoi(val.c_str()), -kMaxBalance, kMaxBalance);
        else if (key == "mono") s.mono = val == "1";
        else if (key == "delay") s.delay = std::clamp(std::atoi(val.c_str()), -kMaxDelay, kMaxDelay);
        else if (key == "mic") s.micMode = std::clamp(std::atoi(val.c_str()), 0, 3);
        else if (key == "micdevice") s.micDevice = ToWide(val);
        else if (key == "ir") s.irPath = ToWide(val);
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

    // 0. Impulsantwort: Kopie mit festem ASCII-Namen in den config-Ordner (APO liest sie von dort; Pfade mit Umlauten gehen so auch)
    std::string irAbs;
    if (s.enabled && !s.irPath.empty())
    {
        std::wstring why;
        const std::wstring dest = cfgDir + L"\\" + kIrCopyName;
        if (CheckImpulseFile(s.irPath, why))
        {
            WIN32_FILE_ATTRIBUTE_DATA a{}, b{};
            const bool same = GetFileAttributesExW(s.irPath.c_str(), GetFileExInfoStandard, &a) && GetFileAttributesExW(dest.c_str(), GetFileExInfoStandard, &b) &&
                              a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow && CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) == 0;
            if (!same && !CopyFileW(s.irPath.c_str(), dest.c_str(), FALSE))
            {
                const DWORD e = GetLastError();
                return e == ERROR_ACCESS_DENIED ? ApplyResult::AccessDenied : ApplyResult::Failed;
            }
            bool ascii = true;
            for (wchar_t c : dest)
                if (c > 126 || c < 32)
                    ascii = false;
            if (ascii)
                irAbs = ToUtf8(dest);
        }
    }

    // 1. unsere Datei
    DWORD err = WriteFileBytes(cfgDir + L"\\" + kConfigFile, BuildConfig(s, irAbs));
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

std::wstring CleanName(std::wstring name)
{
    std::wstring out;
    for (wchar_t c : name)
    {
        if (c < 32 || c == L'=' || c == L'[' || c == L']' || c == L'\t')
            continue;
        out += c;
    }
    while (!out.empty() && out.front() == L' ') out.erase(out.begin());
    while (!out.empty() && out.back() == L' ') out.pop_back();
    if (out.size() > kMaxNameLen)
        out.resize(kMaxNameLen);
    while (!out.empty() && out.back() == L' ') out.pop_back();
    return out;
}

int FindProfile(const std::vector<Profile>& list, const std::wstring& name)
{
    const std::wstring key = LowerW(name);
    for (size_t i = 0; i < list.size(); ++i)
        if (LowerW(list[i].name) == key)
            return static_cast<int>(i);
    return -1;
}

int ProfileForDevice(const std::vector<Profile>& list, const std::wstring& device)
{
    if (device.empty())
        return -1;
    const std::wstring key = LowerW(device);
    for (size_t i = 0; i < list.size(); ++i)
        if (!list[i].device.empty() && LowerW(list[i].device) == key)
            return static_cast<int>(i);
    return -1;
}

std::wstring ProfilesPath()
{
    const std::wstring p = SettingsPath();
    const size_t s = p.find_last_of(L'\\');
    return s == std::wstring::npos ? L"" : p.substr(0, s) + L"\\profiles.txt";
}

static std::string AppsLine(const Profile& p)
{
    std::string out;
    size_t n = 0;
    for (const std::wstring& a : p.apps)
    {
        std::string u = ToUtf8(a);
        if (u.empty() || u.find(',') != std::string::npos || u.find('\r') != std::string::npos || u.find('\n') != std::string::npos || n >= kMaxProfileApps)
            continue;
        out += (n ? "," : "") + u;
        ++n;
    }
    return out;
}

int ProfileForApps(const std::vector<Profile>& list, const std::vector<std::wstring>& activeKeys, int preferred)
{
    auto matches = [&](const Profile& p) {
        for (const std::wstring& a : p.apps)
            for (const std::wstring& k : activeKeys)
                if (!a.empty() && LowerW(a) == LowerW(k))
                    return true;
        return false;
    };
    if (preferred >= 0 && preferred < static_cast<int>(list.size()) && matches(list[static_cast<size_t>(preferred)]))
        return preferred;
    for (size_t i = 0; i < list.size(); ++i)
        if (matches(list[i]))
            return static_cast<int>(i);
    return -1;
}

static std::string ProfileBlock(const Profile& p, bool withDevice)
{
    std::string t = "[profile]\r\nname=" + ToUtf8(p.name) + "\r\ndevice=" + (withDevice ? ToUtf8(p.device) : std::string()) + "\r\napps=" + (withDevice ? AppsLine(p) : std::string()) + "\r\nbass=" + (p.s.bass ? "1" : "0") +
                    "\r\nvoice=" + (p.s.voice ? "1" : "0") + "\r\nbalance=" + Num(std::clamp(p.s.balance, -kMaxBalance, kMaxBalance)) + "\r\nmono=" + (p.s.mono ? "1" : "0") + "\r\ndelay=" + Num(std::clamp(p.s.delay, -kMaxDelay, kMaxDelay)) +
                    (withDevice && !p.s.irPath.empty() ? "\r\nir=" + ToUtf8(p.s.irPath) : std::string()) + "\r\npreamp=" + Num(std::clamp(p.s.preamp, -kMaxGain, kMaxGain)) + "\r\ngains=";
    for (int b = 0; b < kBands; ++b)
        t += (b ? "," : "") + Num(ClampGain(p.s.gains[b]));
    t += "\r\n";
    size_t n = 0;
    for (const std::string& l : p.s.extra)
        if (n < kMaxExtraLines && IsSafeFilterLine(l))
        {
            t += "extra=" + l + "\r\n";
            ++n;
        }
    return t;
}

bool SaveProfiles(const std::vector<Profile>& list, int active, const std::wstring& path)
{
    std::string t;
    if (active >= 0 && active < static_cast<int>(list.size()))
        t += "active=" + ToUtf8(list[static_cast<size_t>(active)].name) + "\r\n";
    for (const Profile& p : list)
        t += ProfileBlock(p, true);
    return WriteFileBytes(path, t) == 0;
}

std::string ProfileToText(const Profile& p)
{
    return "# Duncan's Equalizer - Profil (Textdatei, kann in Duncan's Equalizer unter \"Teilen\" geladen werden)\r\n" + ProfileBlock(p, false);
}

std::wstring UniqueProfileName(const std::vector<Profile>& list, const std::wstring& wanted)
{
    const std::wstring base = CleanName(wanted);
    if (base.empty())
        return base;
    if (FindProfile(list, base) < 0)
        return base;
    for (int n = 2; n < 100; ++n)
    {
        const std::wstring suffix = L" (" + std::to_wstring(n) + L")";
        std::wstring cand = (base.size() + suffix.size() > kMaxNameLen ? base.substr(0, kMaxNameLen - suffix.size()) : base) + suffix;
        if (FindProfile(list, cand) < 0)
            return cand;
    }
    return std::wstring();
}

bool ProfileFromText(const std::string& text, Profile& out)
{
    if (text.size() > 64 * 1024)
        return false;
    std::vector<Profile> list;
    int active = 0;
    if (!ParseProfiles(text, list, active) || list.empty())
        return false;
    out = list[0];
    out.device.clear();
    out.apps.clear();
    out.s.irPath.clear();           // Dateipfade gehoeren zu diesem PC und werden nie weitergegeben
    return true;
}

bool LoadProfiles(std::vector<Profile>& list, int& active, const std::wstring& path)
{
    list.clear();
    active = 0;
    std::string data;
    if (!ReadFileBytes(path, data))
        return false;
    return ParseProfiles(data, list, active);
}

bool ParseProfiles(const std::string& data, std::vector<Profile>& list, int& active)
{
    list.clear();
    active = 0;
    std::wstring activeName;
    size_t pos = 0;
    while (pos < data.size())
    {
        size_t e = data.find('\n', pos);
        if (e == std::string::npos)
            e = data.size();
        const std::string line = Trim(data.substr(pos, e - pos));
        pos = e + 1;
        if (line == "[profile]")
        {
            if (list.size() < static_cast<size_t>(kMaxProfiles))
                list.emplace_back();
            else
                break;
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        if (list.empty())
        {
            if (key == "active")
                activeName = ToWide(val);
            continue;
        }
        Profile& p = list.back();
        if (key == "name") p.name = CleanName(ToWide(val));
        else if (key == "device") p.device = ToWide(val);
        else if (key == "apps")
        {
            p.apps.clear();
            size_t q = 0;
            while (q <= val.size() && p.apps.size() < kMaxProfileApps)
            {
                size_t c = val.find(',', q);
                if (c == std::string::npos)
                    c = val.size();
                const std::wstring a = LowerW(ToWide(Trim(val.substr(q, c - q))));
                if (!a.empty() && a.size() <= 64)
                    p.apps.push_back(a);
                q = c + 1;
            }
        }
        else if (key == "bass") p.s.bass = val == "1";
        else if (key == "voice") p.s.voice = val == "1";
        else if (key == "balance") p.s.balance = std::clamp(std::atoi(val.c_str()), -kMaxBalance, kMaxBalance);
        else if (key == "mono") p.s.mono = val == "1";
        else if (key == "delay") p.s.delay = std::clamp(std::atoi(val.c_str()), -kMaxDelay, kMaxDelay);
        else if (key == "ir") p.s.irPath = ToWide(val);
        else if (key == "preamp") p.s.preamp = std::clamp(std::atoi(val.c_str()), -kMaxGain, kMaxGain);
        else if (key == "extra") { if (p.s.extra.size() < kMaxExtraLines && IsSafeFilterLine(val)) p.s.extra.push_back(val); }
        else if (key == "gains")
        {
            size_t q = 0;
            for (int b = 0; b < kBands && q <= val.size(); ++b)
            {
                size_t c = val.find(',', q);
                if (c == std::string::npos)
                    c = val.size();
                p.s.gains[b] = ClampGain(std::atoi(val.substr(q, c - q).c_str()));
                q = c + 1;
            }
        }
    }
    // Profile ohne Namen oder mit doppeltem Namen entfernen
    std::vector<Profile> clean;
    for (const Profile& p : list)
        if (!p.name.empty() && FindProfile(clean, p.name) < 0)
            clean.push_back(p);
    list.swap(clean);
    if (list.empty())
        return false;
    const int idx = FindProfile(list, activeName);
    active = idx >= 0 ? idx : 0;
    return true;
}

std::vector<std::string> ParseFilterFile(const std::string& text, int& ignored)
{
    std::vector<std::string> out;
    ignored = 0;
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos)
            e = text.size();
        std::string line = Trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (line.empty() || line[0] == '#')
            continue;
        std::string norm;
        if (line.compare(0, 7, "Preamp:") == 0)
            norm = line;
        else if (line.compare(0, 11, "GraphicEQ: ") == 0)
        {
            bool haveGraphic = false;
            for (const std::string& o : out)
                if (o.compare(0, 11, "GraphicEQ: ") == 0)
                    haveGraphic = true;
            if (!haveGraphic)
                norm = line;
        }
        else if (line.compare(0, 6, "Filter") == 0)
        {
            const size_t colon = line.find(':');
            bool ok = colon != std::string::npos;
            for (size_t i = 6; ok && i < colon; ++i)
                if (!(line[i] == ' ' || (line[i] >= '0' && line[i] <= '9')))
                    ok = false;
            if (ok)
                norm = "Filter:" + line.substr(colon + 1);
        }
        if (!norm.empty() && IsSafeFilterLine(norm) && out.size() < kMaxExtraLines)
            out.push_back(norm);
        else
            ++ignored;
    }
    return out;
}

Settings BypassSettings(const Settings& s)
{
    // Original hoeren, aber ungefaehr gleich laut: nur die Vorverstaerkung bleibt (automatische, eigene und die aus Dateien)
    double total = TotalPreampDb(s);
    for (const std::string& l : s.extra)
        if (l.compare(0, 7, "Preamp:") == 0)
        {
            double v = 0;
            if (std::sscanf(l.c_str() + 7, "%lf", &v) == 1)
                total += v;
        }
    total = std::clamp(total, -30.0, 12.0);
    Settings b;
    b.enabled = true;
    if (std::fabs(total) >= 0.05)
        b.extra.push_back("Preamp: " + Num(static_cast<int>(std::lround(total))) + " dB");
    return b;
}

std::wstring DefaultOutputName()
{
    std::wstring name;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IMMDeviceEnumerator* en = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en))) && en)
    {
        IMMDevice* dev = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev)) && dev)
        {
            IPropertyStore* store = nullptr;
            if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &store)) && store)
            {
                PROPVARIANT v;
                PropVariantInit(&v);
                if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal)
                    name = v.pwszVal;
                PropVariantClear(&v);
                store->Release();
            }
            dev->Release();
        }
        en->Release();
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return name;
}

bool GetAutostart(const wchar_t* valueName)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return false;
    const bool exists = RegQueryValueExW(k, valueName, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(k);
    return exists;
}

bool SetAutostart(bool on, const std::wstring& exePath, const wchar_t* valueName)
{
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;
    LONG r;
    if (on)
    {
        const std::wstring cmd = L"\"" + exePath + L"\" --background";
        r = RegSetValueExW(k, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        r = RegDeleteValueW(k, valueName);
        if (r == ERROR_FILE_NOT_FOUND)
            r = ERROR_SUCCESS;
    }
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

}  // namespace eq
