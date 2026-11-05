// Logik-Tests fuer Duncan's Equalizer (ohne Oberflaeche, ohne echtes Equalizer APO)
#include "../EqLogic.h"

#include <cstdio>
#include <string>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

static std::string Slurp(const std::wstring& p)
{
    std::string out;
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "<fehlt>";
    char b[4096]; DWORD n;
    while (ReadFile(h, b, sizeof b, &n, nullptr) && n) out.append(b, n);
    CloseHandle(h);
    return out;
}
static void Put(const std::wstring& p, const std::string& d)
{
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD n; WriteFile(h, d.data(), (DWORD)d.size(), &n, nullptr); CloseHandle(h);
}
static int Count(const std::string& s, const std::string& sub)
{
    int c = 0; for (size_t p = s.find(sub); p != std::string::npos; p = s.find(sub, p + 1)) ++c; return c;
}

int main()
{
    using namespace eq;
    // Voreinstellungen
    CHECK(PresetCount() == 6);
    for (int i = 0; i < PresetCount(); ++i)
        for (int j = i + 1; j < PresetCount(); ++j) {
            bool same = true;
            for (int b = 0; b < kBands; ++b) if (PresetAt(i).gains[b] != PresetAt(j).gains[b]) same = false;
            CHECK(!same);
        }
    for (int i = 0; i < PresetCount(); ++i)
        for (int b = 0; b < kBands; ++b) CHECK(PresetAt(i).gains[b] >= -kMaxGain && PresetAt(i).gains[b] <= kMaxGain);
    Settings s;
    CHECK(MatchPreset(s) == 0);                       // alles 0 = "Flach"
    for (int b = 0; b < kBands; ++b) s.gains[b] = PresetAt(3).gains[b];
    CHECK(MatchPreset(s) == 3);
    s.gains[4] += 1;
    CHECK(MatchPreset(s) == -1);
    CHECK(ClampGain(99) == kMaxGain && ClampGain(-99) == -kMaxGain && ClampGain(3) == 3);

    // Reglerbeschriftung
    for (int b = 0; b < kBands; ++b) { CHECK(BandName(b)[0] != 0); CHECK(std::wstring(BandHint(b)).size() > 30); }
    CHECK(std::wstring(BandName(3)) == L"Wärme" && std::wstring(BandHint(0)).find(L"31 Hz") == 0 && std::wstring(BandHint(9)).find(L"16 kHz") == 0);
    CHECK(BandName(-5) == BandName(0) && BandHint(99) == BandHint(9));

    // Vorverstaerkung
    Settings p;
    CHECK(PreampDb(p) == 0);
    p.gains[2] = 6; CHECK(PreampDb(p) == -6);
    p.gains[2] = -5; CHECK(PreampDb(p) == 0);          // nur Absenkungen: keine Vorabsenkung noetig
    p.gains[5] = 12; p.bass = true; p.voice = true; CHECK(PreampDb(p) == -18);   // gedeckelt

    // Konfigurationstext
    Settings off; off.enabled = false; off.gains[0] = 6;
    std::string t = BuildConfig(off);
    CHECK(t.find("Preamp") == std::string::npos && t.find("GraphicEQ") == std::string::npos);
    Settings flat; flat.enabled = true;
    t = BuildConfig(flat);
    CHECK(t.find("GraphicEQ") == std::string::npos && t.find("Filter") == std::string::npos);
    Settings on; on.enabled = true; on.gains[0] = 6; on.gains[9] = -3;
    t = BuildConfig(on);
    CHECK(t.find("Preamp: -6 dB\r\n") != std::string::npos);
    CHECK(t.find("GraphicEQ: 31 6; 63 0; 125 0; 250 0; 500 0; 1000 0; 2000 0; 4000 0; 8000 0; 16000 -3\r\n") != std::string::npos);
    CHECK(t.find("Filter") == std::string::npos);
    on.bass = true; on.voice = true;
    t = BuildConfig(on);
    CHECK(t.find("Filter: ON LSC Fc 110 Hz Gain 4 dB") != std::string::npos);
    CHECK(t.find("Filter: ON PK Fc 3000 Hz Gain 3 dB Q 1.0") != std::string::npos);
    CHECK(t.find("Preamp: -13 dB") != std::string::npos);
    Settings onlyBass; onlyBass.enabled = true; onlyBass.bass = true;
    t = BuildConfig(onlyBass);
    CHECK(t.find("GraphicEQ") == std::string::npos && t.find("LSC") != std::string::npos && t.find("Preamp: -4 dB") != std::string::npos);
    Settings wild; wild.enabled = true; wild.gains[1] = 99;
    CHECK(BuildConfig(wild).find("63 12") != std::string::npos);   // Werte werden begrenzt

    // Speichern/Laden
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"eqtest" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    Settings a; a.enabled = true; a.bass = true; for (int b = 0; b < kBands; ++b) a.gains[b] = b - 5;
    CHECK(SaveSettings(a, dir + L"/s.txt"));
    Settings r;
    CHECK(LoadSettings(r, dir + L"/s.txt"));
    CHECK(r.enabled && r.bass && !r.voice);
    for (int b = 0; b < kBands; ++b) CHECK(r.gains[b] == b - 5);
    CHECK(!LoadSettings(r, dir + L"/fehlt.txt") && !r.enabled);
    Put(dir + L"/kaputt.txt", "gains=abc,99,-99\r\nenabled=1\r\nmuell\r\n");
    CHECK(LoadSettings(r, dir + L"/kaputt.txt") && r.enabled && r.gains[0] == 0 && r.gains[1] == kMaxGain && r.gains[2] == -kMaxGain);
    CHECK(!SettingsPath().empty());

    // Eintragen in einen (nachgebildeten) Equalizer-APO-Ordner
    CHECK(ApplyToDir(L"", a) == ApplyResult::NotInstalled);
    CHECK(ApplyToDir(dir + L"/nichtda", a) == ApplyResult::NotInstalled);
    std::wstring apo = dir + L"/apo";
    CreateDirectoryW(apo.c_str(), nullptr);
    CreateDirectoryW((apo + L"/config").c_str(), nullptr);
    Put(apo + L"/config/config.txt", "# Eigene Zeile\r\nPreamp: -1 dB");     // ohne Zeilenende am Ende
    CHECK(ApplyToDir(apo, a) == ApplyResult::Applied);
    std::string main = Slurp(apo + L"/config/config.txt");
    CHECK(main.find("# Eigene Zeile\r\nPreamp: -1 dB\r\nInclude: duncans-equalizer.txt\r\n") == 0);
    CHECK(Slurp(apo + L"/config/duncans-equalizer.txt") == BuildConfig(a));
    CHECK(ApplyToDir(apo, a) == ApplyResult::Applied);                            // zweites Mal: keine doppelte Zeile
    CHECK(Count(Slurp(apo + L"/config/config.txt"), "Include: duncans-equalizer.txt") == 1);
    a.enabled = false;
    CHECK(ApplyToDir(apo, a) == ApplyResult::Applied);
    CHECK(Slurp(apo + L"/config/duncans-equalizer.txt").find("ausgeschaltet") != std::string::npos);
    // fehlende config.txt wird angelegt
    DeleteFileW((apo + L"/config/config.txt").c_str());
    CHECK(ApplyToDir(apo, a) == ApplyResult::Applied && Slurp(apo + L"/config/config.txt") == "Include: duncans-equalizer.txt\r\n");
    // UTF-16-Hauptdatei wird nicht angefasst
    Put(apo + L"/config/config.txt", std::string("\xFF\xFE", 2) + "a");
    CHECK(ApplyToDir(apo, a) == ApplyResult::Failed);
    CHECK(Slurp(apo + L"/config/config.txt") == std::string("\xFF\xFE", 2) + "a");
    // schreibgeschuetzte Datei -> Zugriff verweigert
    Put(apo + L"/config/config.txt", "x\r\n");
    SetFileAttributesW((apo + L"/config/duncans-equalizer.txt").c_str(), FILE_ATTRIBUTE_READONLY);
    CHECK(ApplyToDir(apo, a) == ApplyResult::AccessDenied);
    SetFileAttributesW((apo + L"/config/duncans-equalizer.txt").c_str(), FILE_ATTRIBUTE_NORMAL);

    // Aufraeumen
    for (const wchar_t* f : { L"/apo/config/config.txt", L"/apo/config/duncans-equalizer.txt", L"/s.txt", L"/kaputt.txt" }) DeleteFileW((dir + f).c_str());
    RemoveDirectoryW((apo + L"/config").c_str()); RemoveDirectoryW(apo.c_str()); RemoveDirectoryW(dir.c_str());

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
