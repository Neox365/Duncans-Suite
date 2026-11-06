// Logik-Tests fuer Duncan's Equalizer (ohne Oberflaeche, ohne echtes Equalizer APO)
#include "../EqLogic.h"

#include <cstdio>
#include <string>
#include <vector>

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


    // Zusatzzeilen (AutoEq) und Profile
    {
        const std::string autoeq =
            "# AutoEq\r\nPreamp: -6.2 dB\r\nFilter 1: ON PK Fc 31 Hz Gain 2.1 dB Q 1.41\r\nFilter 2: ON LSC Fc 105 Hz Gain 5.5 dB Q 0.70\r\n"
            "Include: C:/geheim.txt\r\nVSTPlugin: Library evil.dll\r\nFilter 3: ON PK Fc 1000 Hz Gain 1 dB Q 1; Include: x\r\nunsinn\r\nFilter x: ON PK\r\n";
        int ignored = 0;
        std::vector<std::string> ex = ParseFilterFile(autoeq, ignored);
        CHECK(ex.size() == 3 && ignored == 5);
        CHECK(ex[0] == "Preamp: -6.2 dB");
        CHECK(ex[1] == "Filter: ON PK Fc 31 Hz Gain 2.1 dB Q 1.41");
        CHECK(ex[2] == "Filter: ON LSC Fc 105 Hz Gain 5.5 dB Q 0.70");
        std::string many;
        for (int i = 0; i < 100; ++i) many += "Filter " + std::to_string(i) + ": ON PK Fc 100 Hz Gain 1 dB Q 1\r\n";
        CHECK(ParseFilterFile(many, ignored).size() == kMaxExtraLines && ignored == 100 - (int)kMaxExtraLines);
        CHECK(ParseFilterFile("", ignored).empty() && ignored == 0);

        Settings x; x.enabled = true; x.extra = ex;
        std::string cfg = BuildConfig(x);
        CHECK(cfg.find("Preamp: -6.2 dB\r\nFilter: ON PK Fc 31 Hz Gain 2.1 dB Q 1.41\r\n") != std::string::npos);
        CHECK(cfg.find("GraphicEQ") == std::string::npos && cfg.find("Alle Regler") == std::string::npos);
        x.extra.push_back("Include: C:/boese.txt");           // von Hand eingeschleust: wird beim Schreiben verworfen
        x.extra.push_back("Filter: ON PK; VSTPlugin: a.dll");
        cfg = BuildConfig(x);
        CHECK(cfg.find("Include") == std::string::npos && cfg.find("VSTPlugin") == std::string::npos);
        Settings g6; g6.enabled = true; g6.gains[0] = -3;
        CHECK(BuildConfig(g6).find("Preamp") == std::string::npos);      // nur Absenkungen: keine Vorabsenkung noetig

        CHECK(CleanName(L"  Kopfhörer [Musik]=1  ") == L"Kopfhörer Musik1");
        CHECK(CleanName(L"  ").empty() && CleanName(L"a\tb") == L"ab");
        CHECK(CleanName(L"123456789012345678901234567890").size() == kMaxNameLen);

        std::vector<Profile> pl(3);
        pl[0].name = L"Standard";
        pl[1].name = L"Kopfhörer"; pl[1].device = L"Kopfhörer (Realtek(R) Audio)"; pl[1].s.bass = true; pl[1].s.voice = true;
        for (int b = 0; b < kBands; ++b) pl[1].s.gains[b] = b - 4;
        pl[1].s.extra = ex;
        pl[2].name = L"Boxen"; pl[2].device = L"Lautsprecher (HyperX)";
        wchar_t tmp2[MAX_PATH]; GetTempPathW(MAX_PATH, tmp2);
        std::wstring pp = std::wstring(tmp2) + L"eqprof" + std::to_wstring(GetCurrentProcessId()) + L".txt";
        CHECK(SaveProfiles(pl, 1, pp));
        std::vector<Profile> back; int act = -1;
        CHECK(LoadProfiles(back, act, pp));
        CHECK(back.size() == 3 && act == 1);
        CHECK(back[1].name == L"Kopfhörer" && back[1].device == L"Kopfhörer (Realtek(R) Audio)" && back[1].s.bass && back[1].s.voice);
        for (int b = 0; b < kBands; ++b) CHECK(back[1].s.gains[b] == b - 4);
        CHECK(back[1].s.extra == ex && back[0].s.extra.empty() && back[2].device == L"Lautsprecher (HyperX)");
        CHECK(FindProfile(back, L"KOPFHÖRER") == 1 && FindProfile(back, L"gibtsnicht") == -1);
        CHECK(ProfileForDevice(back, L"lautsprecher (hyperx)") == 2 && ProfileForDevice(back, L"") == -1 && ProfileForDevice(back, L"Andere") == -1);
        // beschaedigt: doppelter Name, Namenlose, aktives Profil fehlt, unsichere Zeile
        Put(pp, "active=Gibtsnicht\r\n[profile]\r\nname=A\r\ngains=1,2,3\r\nextra=Include: x\r\n[profile]\r\nname=a\r\n[profile]\r\nbass=1\r\n");
        CHECK(LoadProfiles(back, act, pp) && back.size() == 1 && act == 0 && back[0].s.extra.empty() && back[0].s.gains[2] == 3 && back[0].s.gains[3] == 0);
        Put(pp, "nur muell\r\n");
        CHECK(!LoadProfiles(back, act, pp) && back.empty());
        CHECK(!LoadProfiles(back, act, pp + L".fehlt"));
        DeleteFileW(pp.c_str());
        CHECK(!ProfilesPath().empty());
        std::wstring dev = DefaultOutputName();       // je nach Rechner leer oder ein Name; darf nicht abstuerzen
        CHECK(dev.size() < 400);
    }


    // Eigener Preamp-Regler
    {
        Settings p; p.enabled = true; p.gains[2] = 6;
        CHECK(PreampDb(p) == -6 && TotalPreampDb(p) == -6);
        p.preamp = 3;  CHECK(TotalPreampDb(p) == -3 && BuildConfig(p).find("Preamp: -3 dB") != std::string::npos);
        p.preamp = -4; CHECK(TotalPreampDb(p) == -10);
        p.preamp = 99; CHECK(TotalPreampDb(p) == 6);                 // Regler wird auf +12 begrenzt (-6 + 12)
        p.preamp = -99; CHECK(TotalPreampDb(p) == -18);              // -6 - 12
        Settings q; q.enabled = true; q.preamp = 5;                  // nur der Preamp: schreibt Preamp, sonst nichts
        std::string cq = BuildConfig(q);
        CHECK(cq.find("Preamp: 5 dB") != std::string::npos && cq.find("GraphicEQ") == std::string::npos && cq.find("Alle Regler") == std::string::npos);
        Settings z; z.enabled = true; CHECK(BuildConfig(z).find("Alle Regler") != std::string::npos);   // alles 0: weiter nichts
        Settings bs; bs.enabled = true; bs.bass = true;              // Bass-Schub senkt automatisch ab
        CHECK(PreampDb(bs) == -4 && BuildConfig(bs).find("Preamp: -4 dB") != std::string::npos);
        bs.preamp = -2; CHECK(BuildConfig(bs).find("Preamp: -6 dB") != std::string::npos);
        std::vector<Profile> pl(1); pl[0].name = L"A"; pl[0].s.preamp = -7;
        wchar_t tmp3[MAX_PATH]; GetTempPathW(MAX_PATH, tmp3);
        std::wstring pp3 = std::wstring(tmp3) + L"eqpre" + std::to_wstring(GetCurrentProcessId()) + L".txt";
        std::vector<Profile> back3; int act3 = 0;
        CHECK(SaveProfiles(pl, 0, pp3) && LoadProfiles(back3, act3, pp3) && back3.size() == 1 && back3[0].s.preamp == -7);
        Put(pp3, "[profile]\r\nname=B\r\npreamp=500\r\n");
        CHECK(LoadProfiles(back3, act3, pp3) && back3[0].s.preamp == kMaxGain);
        Put(pp3, "[profile]\r\nname=C\r\n");
        CHECK(LoadProfiles(back3, act3, pp3) && back3[0].s.preamp == 0);   // aeltere Dateien ohne Preamp
        DeleteFileW(pp3.c_str());
    }


    // Infobereich-Einstellung und Autostart
    {
        Settings k; CHECK(k.keepRunning);                         // Standard: im Infobereich weiterlaufen
        k.keepRunning = false;
        wchar_t tmp4[MAX_PATH]; GetTempPathW(MAX_PATH, tmp4);
        std::wstring sp = std::wstring(tmp4) + L"eqkeep" + std::to_wstring(GetCurrentProcessId()) + L".txt";
        Settings back; CHECK(SaveSettings(k, sp) && LoadSettings(back, sp) && !back.keepRunning);
        Put(sp, "enabled=1\r\n");                                  // aeltere Datei ohne den Schluessel
        CHECK(LoadSettings(back, sp) && back.keepRunning && back.enabled);
        DeleteFileW(sp.c_str());
        const wchar_t* name = L"DuncansEqualizerTest";
        const std::wstring vexe = L"C:/Programme/Test Ordner/DuncansEqualizer.exe";
        SetAutostart(false, vexe, name);
        CHECK(!GetAutostart(name));
        CHECK(SetAutostart(true, vexe, name) && GetAutostart(name));
        HKEY key = nullptr; wchar_t val[512] = {}; DWORD sz = sizeof(val);
        CHECK(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS);
        CHECK(RegQueryValueExW(key, name, nullptr, nullptr, reinterpret_cast<BYTE*>(val), &sz) == ERROR_SUCCESS);
        RegCloseKey(key);
        CHECK(std::wstring(val) == L"\"C:/Programme/Test Ordner/DuncansEqualizer.exe\" --background");
        CHECK(SetAutostart(false, vexe, name) && !GetAutostart(name));
        CHECK(SetAutostart(false, vexe, name));                    // schon aus: kein Fehler
    }

    // Aufraeumen
    for (const wchar_t* f : { L"/apo/config/config.txt", L"/apo/config/duncans-equalizer.txt", L"/s.txt", L"/kaputt.txt" }) DeleteFileW((dir + f).c_str());
    RemoveDirectoryW((apo + L"/config").c_str()); RemoveDirectoryW(apo.c_str()); RemoveDirectoryW(dir.c_str());

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
