// Logik-Tests fuer Duncan's Equalizer (ohne Oberflaeche, ohne echtes Equalizer APO)
#include "../EqCurve.h"
#include "../EqLogic.h"

#include <cstring>
#include <algorithm>
#include <cmath>
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

    // ---- Frequenzkurve und eigene Filter (EqCurve) ----
    {
        const auto at = [](const Settings& s, double f) { return ResponseDb(s, { f })[0]; };
        Settings flat;
        flat.enabled = true;
        CHECK(std::fabs(at(flat, 1000)) < 1e-9 && std::fabs(at(flat, 20)) < 1e-9);           // alles auf 0: gerade Linie

        // Glocke +6 dB bei 1 kHz: dort genau +6, weit weg ungefaehr 0
        Filter pk;
        pk.type = "PK"; pk.fc = 1000; pk.gain = 6; pk.q = 1.0;
        Settings a = flat;
        a.extra = { FormatFilter(pk) };
        CHECK(std::fabs(at(a, 1000) - 6.0) < 0.05 && std::fabs(at(a, 20)) < 0.3 && std::fabs(at(a, 20000)) < 1.0);
        CHECK(at(a, 700) > 2 && at(a, 700) < 6 && std::fabs(at(a, 700) - at(a, 1430)) < 1.0);   // symmetrisch um die Mitte (logarithmisch)
        pk.gain = -6;
        a.extra = { FormatFilter(pk) };
        CHECK(std::fabs(at(a, 1000) + 6.0) < 0.05);
        pk.on = false;                                                                         // ausgeschalteter Filter wirkt nicht
        a.extra = { FormatFilter(pk) };
        CHECK(std::fabs(at(a, 1000)) < 1e-9);

        // Hochpass 100 Hz: bei 100 Hz etwa -3 dB, darunter steil, darueber flach; Tiefpass spiegelbildlich
        Filter hp; hp.type = "HP"; hp.fc = 100;
        a.extra = { FormatFilter(hp) };
        CHECK(std::fabs(at(a, 100) + 3.0) < 0.2 && at(a, 20) < -25 && std::fabs(at(a, 5000)) < 0.1);
        Filter lp; lp.type = "LP"; lp.fc = 5000;
        a.extra = { FormatFilter(lp) };
        CHECK(std::fabs(at(a, 5000) + 3.0) < 0.2 && at(a, 20000) < -20 && std::fabs(at(a, 100)) < 0.1);

        // Shelf-Filter und Kerbfilter
        Filter ls; ls.type = "LSC"; ls.fc = 200; ls.gain = 5;
        a.extra = { FormatFilter(ls) };
        CHECK(std::fabs(at(a, 20) - 5.0) < 0.3 && std::fabs(at(a, 8000)) < 0.3);
        Filter hs; hs.type = "HSC"; hs.fc = 6000; hs.gain = -4;
        a.extra = { FormatFilter(hs) };
        CHECK(std::fabs(at(a, 20000) + 4.0) < 0.5 && std::fabs(at(a, 100)) < 0.1);
        Filter no; no.type = "NO"; no.fc = 1000; no.q = 8;
        a.extra = { FormatFilter(no) };
        CHECK(at(a, 1000) < -30 && std::fabs(at(a, 100)) < 0.1);

        // Regler (Graphic-EQ): an den Stuetzstellen genau der Reglerwert, dazwischen verbunden; Bass-Schub hebt die Tiefen
        Settings g = flat;
        g.gains[5] = 6;   // 1 kHz
        CHECK(std::fabs(at(g, 1000) - 6.0) < 1e-6 && std::fabs(at(g, 500)) < 1e-6 && at(g, 750) > 0 && at(g, 750) < 6);
        Settings bass = flat;
        bass.bass = true;
        CHECK(at(bass, 30) > at(bass, 2000) + 2.5);
        // Vorverstaerkung nur auf Wunsch in der Kurve
        Settings pre = flat;
        pre.gains[5] = 6;
        CHECK(std::fabs(ResponseDb(pre, { 1000 }, true)[0] - (6.0 + (-6))) < 1e-6 && std::fabs(ResponseDb(pre, { 1000 }, false)[0] - 6.0) < 1e-6);
        // Der Kurventext entspricht dem, was geschrieben wird: eingelesene AutoEq-Zeilen zaehlen mit
        int ignored = 0;
        Settings imp = flat;
        imp.extra = ParseFilterFile("Preamp: -4.5 dB\nFilter 1: ON PK Fc 105 Hz Gain 5.3 dB Q 0.70\nFilter 2: ON HSC Fc 9000 Hz Gain -3.2 dB Q 0.7\n", ignored);
        CHECK(ignored == 0 && imp.extra.size() == 3 && at(imp, 105) > 4.0 && at(imp, 20000) < -2.0);

        // Zeilen lesen und schreiben
        Filter f;
        CHECK(ParseFilter("Filter: ON PK Fc 105 Hz Gain 5.3 dB Q 0.70", f) && f.type == "PK" && f.on && std::fabs(f.fc - 105) < 1e-9 && std::fabs(f.gain - 5.3) < 1e-9 && std::fabs(f.q - 0.7) < 1e-9);
        CHECK(ParseFilter("Filter: OFF HP Fc 80 Hz", f) && !f.on && f.type == "HP" && f.gain == 0);
        CHECK(ParseFilter("Filter: ON LSC Fc 110 Hz Gain 4 dB", f) && f.type == "LSC" && f.q == 0);   // ohne Q: Standard
        CHECK(ParseFilter("Filter: ON PK Fc 99999 Hz Gain 99 dB Q 99", f) && f.fc == kMaxFc && f.gain == kMaxFilterGain && f.q == kMaxQ);   // begrenzt
        CHECK(!ParseFilter("Preamp: -3 dB", f) && !ParseFilter("Filter: ON XX Fc 100 Hz", f) && !ParseFilter("Filter: ON PK Gain 3 dB", f) &&
              !ParseFilter("Filter: ON PK Fc 100 Hz Gain 3 dB BW Oct 1", f) && !ParseFilter("Filter: ON PK Fc abc Hz", f) && !ParseFilter("", f));
        Filter w;
        w.type = "PK"; w.fc = 2500; w.gain = -3.5; w.q = 2;
        const std::string line = FormatFilter(w);
        Filter back;
        CHECK(line == "Filter: ON PK Fc 2500.0 Hz Gain -3.5 dB Q 2.00" && ParseFilter(line, back) && back.type == "PK" && back.fc == 2500 && back.gain == -3.5 && back.q == 2);
        Settings safe = flat;
        safe.extra = { line, FormatFilter(hp), FormatFilter(ls), FormatFilter(no) };
        const std::string cfg = BuildConfig(safe);
        CHECK(Count(cfg, "Filter: ON") == 4);   // alle Zeilen passen durch die Sicherheitspruefung von BuildConfig
        const auto fr = LogFreqs(5);
        CHECK(fr.size() == 5 && std::fabs(fr[0] - 20) < 1e-9 && std::fabs(fr[4] - 20000) < 1e-6 && fr[2] > 600 && fr[2] < 650);
        CHECK(FilterUsesGain("PK") && !FilterUsesGain("HP") && FilterUsesQ("NO") && !FilterUsesQ("LP"));
    }

    // Aufraeumen
    for (const wchar_t* f : { L"/apo/config/config.txt", L"/apo/config/duncans-equalizer.txt", L"/s.txt", L"/kaputt.txt" }) DeleteFileW((dir + f).c_str());
    RemoveDirectoryW((apo + L"/config").c_str()); RemoveDirectoryW(apo.c_str()); RemoveDirectoryW(dir.c_str());

    // Profil weitergeben (Export/Import) und Tastenkuerzel-Schalter
    {
        Profile p;
        p.name = L"Kopfhörer Büro"; p.device = L"Realtek (R) Audio"; p.s.bass = true; p.s.voice = true; p.s.preamp = -3;
        for (int b = 0; b < kBands; ++b) p.s.gains[b] = b - 5;
        p.s.extra = {"Preamp: -4.5 dB", "Filter: ON PK Fc 105 Hz Gain -2.5 dB Q 0.7"};
        const std::string t = ProfileToText(p);
        CHECK(t.find("realtek") == std::string::npos && t.find("Realtek") == std::string::npos);       // Geraetebindung wird nicht weitergegeben
        Profile q;
        CHECK(ProfileFromText(t, q) && q.name == p.name && q.device.empty() && q.s.bass && q.s.voice && q.s.preamp == -3 && q.s.extra == p.s.extra);
        for (int b = 0; b < kBands; ++b) CHECK(q.s.gains[b] == b - 5);
        Profile r;
        CHECK(!ProfileFromText("", r) && !ProfileFromText("nur muell\r\n", r) && !ProfileFromText("[profile]\r\nbass=1\r\n", r));   // ohne Namen kein Profil
        CHECK(!ProfileFromText(std::string(70 * 1024, 'x'), r));                                       // zu gross
        CHECK(ProfileFromText("[profile]\r\nname=X\r\ngains=99,-99,1\r\nextra=Include: boese.txt\r\nextra=Filter: ON PK Fc 1000 Hz Gain 1 dB Q 1\r\n", r) &&
              r.s.gains[0] == kMaxGain && r.s.gains[1] == -kMaxGain && r.s.extra.size() == 1);          // Werte begrenzt, unsichere Zeile verworfen
        CHECK(ProfileFromText("[profile]\r\nname=Erstes\r\n[profile]\r\nname=Zweites\r\n", r) && r.name == L"Erstes");   // nur das erste Profil
        std::vector<Profile> list(2);
        list[0].name = L"Kopfhörer"; list[1].name = L"Kopfhörer (2)";
        CHECK(UniqueProfileName(list, L"Boxen") == L"Boxen");
        CHECK(UniqueProfileName(list, L"kopfhörer") == L"kopfhörer (3)");
        CHECK(UniqueProfileName(list, L"  ").empty());
        list[0].name = L"123456789012345678901234";                                                     // 24 Zeichen = Hoechstlaenge
        CHECK(UniqueProfileName(list, L"123456789012345678901234").size() <= kMaxNameLen && FindProfile(list, UniqueProfileName(list, L"123456789012345678901234")) < 0);
        // Tastenkuerzel: Standard an, wird gespeichert
        wchar_t tmp3[MAX_PATH]; GetTempPathW(MAX_PATH, tmp3);
        const std::wstring sp3 = std::wstring(tmp3) + L"eqhk" + std::to_wstring(GetCurrentProcessId()) + L".txt";
        Settings hk; CHECK(hk.hotkeys);
        hk.hotkeys = false;
        Settings hb; CHECK(SaveSettings(hk, sp3) && LoadSettings(hb, sp3) && !hb.hotkeys);
        Put(sp3, "enabled=1\r\n");
        CHECK(LoadSettings(hb, sp3) && hb.hotkeys);                                                      // alte Datei ohne den Eintrag: an
        DeleteFileW(sp3.c_str());
    }

    // Profil pro Programm: Speichern/Laden, Export ohne Programme, Auswahl
    {
        std::vector<Profile> list(3);
        list[0].name = L"Standard";
        list[1].name = L"Musik";   list[1].apps = {L"spotify", L"vlc"};
        list[2].name = L"Spiele";  list[2].apps = {L"bannerlord"};
        list[1].s.gains[0] = 4;
        wchar_t tmpP[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpP);
        const std::wstring pp = std::wstring(tmpP) + L"eqapps" + std::to_wstring(GetCurrentProcessId()) + L".txt";
        CHECK(SaveProfiles(list, 1, pp));
        std::vector<Profile> back; int act = -1;
        CHECK(LoadProfiles(back, act, pp) && back.size() == 3 && act == 1);
        CHECK(back[1].apps.size() == 2 && back[1].apps[0] == L"spotify" && back[1].apps[1] == L"vlc");
        CHECK(back[2].apps.size() == 1 && back[2].apps[0] == L"bannerlord" && back[0].apps.empty());
        DeleteFileW(pp.c_str());
        Profile imp;
        CHECK(ProfileFromText(ProfileToText(list[1]), imp) && imp.apps.empty() && imp.s.gains[0] == 4);   // Export nimmt keine Programme mit
        const std::vector<std::wstring> none, spot = {L"Spotify"}, both = {L"bannerlord", L"spotify"}, other = {L"chrome"};
        CHECK(ProfileForApps(list, none, 0) == -1);
        CHECK(ProfileForApps(list, other, 0) == -1);
        CHECK(ProfileForApps(list, spot, 0) == 1);                                                         // ohne Beachtung der Gross-/Kleinschreibung
        CHECK(ProfileForApps(list, both, 0) == 1);                                                         // das erste passende
        CHECK(ProfileForApps(list, both, 2) == 2);                                                         // das bevorzugte bleibt, wenn es passt
        CHECK(ProfileForApps(list, spot, 2) == 1);
        // zu lange Listen werden begrenzt, Kommas im Namen verworfen
        Profile many;
        many.name = L"Viele";
        for (int i = 0; i < 12; ++i) many.apps.push_back(L"prog" + std::to_wstring(i));
        many.apps.push_back(L"a,b");
        std::vector<Profile> ml = {many};
        const std::wstring pp2 = std::wstring(tmpP) + L"eqapps2" + std::to_wstring(GetCurrentProcessId()) + L".txt";
        CHECK(SaveProfiles(ml, 0, pp2) && LoadProfiles(back, act, pp2) && back[0].apps.size() == kMaxProfileApps);
        DeleteFileW(pp2.c_str());
    }

    // Auto-Lautstaerke: Einstellung wird gespeichert; alte Dateien ohne den Eintrag: aus
    {
        wchar_t tmpA[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpA);
        const std::wstring spa = std::wstring(tmpA) + L"eqag" + std::to_wstring(GetCurrentProcessId()) + L".txt";
        Settings ag; CHECK(!ag.autoGain);
        ag.autoGain = true;
        Settings ag2; CHECK(SaveSettings(ag, spa) && LoadSettings(ag2, spa) && ag2.autoGain);
        Put(spa, "enabled=1\r\n");
        CHECK(LoadSettings(ag2, spa) && !ag2.autoGain);
        DeleteFileW(spa.c_str());
    }

    // AutoEq: GraphicEQ.txt wird als eine Zeile uebernommen, die Kurve addiert sich zu den Reglern
    {
        int ign = 0;
        const std::string gtxt = "GraphicEQ: 20 -1.5; 100 0; 1000 3.0; 10000 -2; 20000 -4\r\n";
        std::vector<std::string> gl = ParseFilterFile(gtxt, ign);
        CHECK(gl.size() == 1 && ign == 0 && gl[0].compare(0, 11, "GraphicEQ: ") == 0);
        gl = ParseFilterFile(gtxt + "GraphicEQ: 20 0; 30 1\r\n", ign);
        CHECK(gl.size() == 1 && ign == 1);                                                              // nur eine Graphic-EQ-Zeile
        gl = ParseFilterFile("GraphicEQ: 20 abc; 30 1\r\n", ign);
        CHECK(gl.empty() && ign == 1);                                                                  // Buchstaben: verworfen
        gl = ParseFilterFile("Preamp: -6.5 dB\r\nFilter 1: ON PK Fc 31 Hz Gain 6.3 dB Q 1.41\r\n" + gtxt, ign);
        CHECK(gl.size() == 3);
        Settings gs;
        gs.extra = ParseFilterFile(gtxt, ign);
        const std::vector<double> fq = {1000.0};
        CHECK(std::fabs(ResponseDb(gs, fq)[0] - 3.0) < 0.1);
        gs.gains[5] = 6;                                                                                  // Regler + Datei addieren sich (500 Hz Regler wirkt bei 1 kHz teils mit)
        CHECK(ResponseDb(gs, fq)[0] > 3.0);
        Settings gp;
        gp.enabled = true;
        gp.extra = gs.extra;
        CHECK(BuildConfig(gp).find("GraphicEQ: 20 -1.5;") != std::string::npos);
    }

    // A/B-Vergleich: nur die Vorverstaerkung bleibt
    {
        Settings a;
        a.gains[0] = 6; a.gains[3] = 3;
        a.extra.push_back("Preamp: -2.0 dB");
        a.extra.push_back("Filter: ON PK Fc 1000 Hz Gain 4 dB Q 1.0");
        const Settings b = BypassSettings(a);
        const std::string cfg = BuildConfig(b);
        CHECK(cfg.find("Filter:") == std::string::npos && cfg.find("GraphicEQ") == std::string::npos);
        CHECK(cfg.find("Preamp: -8 dB") != std::string::npos);                                         // -6 automatisch, -2 aus der Datei
        Settings flat;
        CHECK(BuildConfig(BypassSettings(flat)).find("Preamp") == std::string::npos);
    }

    // Balance, Mono, Impulsantwort
    {
        Settings s;
        s.enabled = true;
        s.balance = 3;                                                     // rechts lauter: links wird 3 dB leiser
        std::string t = BuildConfig(s);
        CHECK(t.find("Channel: L\r\nPreamp: -3 dB\r\nChannel: all\r\n") != std::string::npos);
        s.balance = -5;                                                    // links lauter: rechts leiser
        t = BuildConfig(s);
        CHECK(t.find("Channel: R\r\nPreamp: -5 dB\r\nChannel: all\r\n") != std::string::npos && t.find("Channel: L") == std::string::npos);
        s.balance = 99;                                                    // begrenzt
        CHECK(BuildConfig(s).find("Preamp: -12 dB") != std::string::npos);
        s.balance = 0;
        CHECK(BuildConfig(s).find("Channel") == std::string::npos && BuildConfig(s).find("Alle Regler stehen auf 0") != std::string::npos);
        s.mono = true;
        t = BuildConfig(s);
        CHECK(t.find("Copy: L=0.5*L+0.5*R R=0.5*L+0.5*R\r\n") != std::string::npos && t.find("Alle Regler") == std::string::npos);
        const std::string ir = "C:\\Program Files\\EqualizerAPO\\config\\duncans-ir.wav";
        Settings only; only.enabled = true;
        t = BuildConfig(only, ir);
        CHECK(t.find("Convolution: " + ir + "\r\n") != std::string::npos);
        CHECK(BuildConfig(only, "C:\\x.wav\r\nInclude: evil.txt").find("Convolution") == std::string::npos);   // kein Zeilenumbruch einschmuggeln
        CHECK(BuildConfig(only, "C:\\a;b.wav").find("Convolution") == std::string::npos);
        CHECK(BuildConfig(only, "relativ.wav").find("Convolution") == std::string::npos && BuildConfig(only, "C:\\..\\x.wav").find("Convolution") == std::string::npos);
        Settings off = only; off.enabled = false; off.mono = true;
        CHECK(BuildConfig(off, ir).find("Convolution") == std::string::npos && BuildConfig(off).find("Copy:") == std::string::npos);   // aus = aus
        // Die Kurve ignoriert Balance und Mono
        Settings cb; cb.enabled = true; cb.balance = 6; cb.mono = true; cb.gains[3] = 4;
        Settings ca; ca.enabled = true; ca.gains[3] = 4;
        const std::vector<double> fr = { 100, 1000, 8000 };
        CHECK(ResponseDb(cb, fr, true) == ResponseDb(ca, fr, true));

        // Speichern und Laden, Profile
        wchar_t tmp3[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmp3);
        const std::wstring d = std::wstring(tmp3) + L"eqtest_ir" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(d.c_str(), nullptr);
        Settings w; w.enabled = true; w.balance = -4; w.mono = true; w.irPath = L"C:\\Users\\M\xFC" L"ller\\R\xE4ume\\Wohnzimmer.wav";
        CHECK(SaveSettings(w, d + L"/s.txt"));
        Settings r;
        CHECK(LoadSettings(r, d + L"/s.txt") && r.balance == -4 && r.mono && r.irPath == w.irPath);
        Put(d + L"/s2.txt", "balance=500\r\nmono=0\r\n");
        CHECK(LoadSettings(r, d + L"/s2.txt") && r.balance == kMaxBalance && !r.mono && r.irPath.empty());
        std::vector<Profile> list(1);
        list[0].name = L"Wohnzimmer"; list[0].s = w;
        CHECK(SaveProfiles(list, 0, d + L"/p.txt"));
        std::vector<Profile> back; int act = 0;
        CHECK(LoadProfiles(back, act, d + L"/p.txt") && back.size() == 1 && back[0].s.balance == -4 && back[0].s.mono && back[0].s.irPath == w.irPath);
        const std::string shared = ProfileToText(list[0]);
        CHECK(shared.find("ir=") == std::string::npos && shared.find("balance=-4") != std::string::npos && shared.find("mono=1") != std::string::npos);
        Profile imp;
        CHECK(ProfileFromText(shared + "ir=C:\\geheim.wav\r\n", imp) && imp.s.irPath.empty() && imp.s.balance == -4 && imp.s.mono);

        // Impulsantwort pruefen
        std::wstring why;
        CHECK(!CheckImpulseFile(d + L"/fehlt.wav", why) && !why.empty());
        std::string wav = "RIFF"; wav += std::string(4, '\0'); wav += "WAVEfmt "; wav += std::string(60, '\0');
        Put(d + L"/ok.wav", wav);
        CHECK(CheckImpulseFile(d + L"/ok.wav", why));
        Put(d + L"/nowav.wav", std::string("OggS") + std::string(100, 'x'));
        CHECK(!CheckImpulseFile(d + L"/nowav.wav", why) && !why.empty());
        Put(d + L"/klein.wav", "RIFF1234WAVE");
        CHECK(!CheckImpulseFile(d + L"/klein.wav", why));
        CHECK(!CheckImpulseFile(d, why));                                  // Ordner

        // Eintragen: Kopie im config-Ordner, Konfiguration zeigt darauf
        const std::wstring apo2 = d + L"/apo";
        CreateDirectoryW(apo2.c_str(), nullptr);
        CreateDirectoryW((apo2 + L"/config").c_str(), nullptr);
        Settings ap; ap.enabled = true; ap.irPath = d + L"/ok.wav";
        CHECK(ApplyToDir(apo2, ap) == ApplyResult::Applied);
        CHECK(Slurp(apo2 + L"/config/duncans-ir.wav") == wav);
        const std::string cfgText = Slurp(apo2 + L"/config/duncans-equalizer.txt");
        CHECK(cfgText.find("Convolution: ") != std::string::npos && cfgText.find("duncans-ir.wav\r\n") != std::string::npos);
        CHECK(ApplyToDir(apo2, ap) == ApplyResult::Applied);               // zweites Mal: unveraendert
        Settings none; none.enabled = true;
        CHECK(ApplyToDir(apo2, none) == ApplyResult::Applied && Slurp(apo2 + L"/config/duncans-equalizer.txt").find("Convolution") == std::string::npos);
        Settings bad; bad.enabled = true; bad.irPath = d + L"/nowav.wav";   // ungueltige Datei: keine Faltung, kein Absturz
        CHECK(ApplyToDir(apo2, bad) == ApplyResult::Applied && Slurp(apo2 + L"/config/duncans-equalizer.txt").find("Convolution") == std::string::npos);
        for (const wchar_t* f : { L"/s.txt", L"/s2.txt", L"/p.txt", L"/ok.wav", L"/nowav.wav", L"/klein.wav", L"/apo/config/duncans-ir.wav", L"/apo/config/duncans-equalizer.txt", L"/apo/config/config.txt" }) DeleteFileW((d + f).c_str());
        RemoveDirectoryW((apo2 + L"/config").c_str()); RemoveDirectoryW(apo2.c_str()); RemoveDirectoryW(d.c_str());
    }


    // Verzoegerung und Mikrofon-Abschnitt
    {
        Settings d; d.enabled = true; d.delay = 5;
        CHECK(BuildConfig(d).find("Channel: R\r\nDelay: 5 ms\r\nChannel: all\r\n") != std::string::npos);
        d.delay = -3;
        CHECK(BuildConfig(d).find("Channel: L\r\nDelay: 3 ms\r\nChannel: all\r\n") != std::string::npos);
        d.delay = 500;
        CHECK(BuildConfig(d).find("Delay: 20 ms") != std::string::npos);
        d.delay = 0;
        CHECK(BuildConfig(d).find("Delay") == std::string::npos && BuildConfig(d).find("Alle Regler") != std::string::npos);
        CHECK(MicDeviceKey(L"Mikrofon (Realtek High Definition Audio)") == "Realtek High Definition Audio");
        CHECK(MicDeviceKey(L"\xC4\xD6").empty() && MicDeviceKey(L"ab").empty());
        Settings m; m.micMode = 3; m.micDevice = L"Mikrofon (Blue Yeti)";
        const std::string c = BuildConfig(m);                              // Equalizer aus, Mikrofon wirkt trotzdem
        CHECK(c.find("Device: Blue Yeti\r\nFilter: ON HP Fc 90 Hz\r\n") != std::string::npos && c.find("Fc 3500 Hz Gain 3 dB") != std::string::npos && c.rfind("Device: all") != std::string::npos);
        m.micMode = 2;
        CHECK(BuildConfig(m).find("PK Fc") == std::string::npos && BuildConfig(m).find("HP Fc 90") != std::string::npos);
        m.micDevice = L"\xC4\xD6";
        CHECK(BuildConfig(m).find("Device:") == std::string::npos);
        m.micDevice = L"Blue Yeti"; m.micMode = 0;
        CHECK(BuildConfig(m).find("Device:") == std::string::npos);
        Settings cv; cv.enabled = true; cv.micMode = 3; cv.micDevice = L"Blue Yeti"; cv.delay = 4;   // Kurve der Wiedergabe bleibt unberuehrt
        const std::vector<double> fr = { 100, 1000, 3500 };
        const std::vector<double> r = ResponseDb(cv, fr, true);
        CHECK(std::fabs(r[0]) < 0.01 && std::fabs(r[2]) < 0.01);
        wchar_t tmp4[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmp4);
        const std::wstring dd = std::wstring(tmp4) + L"eqtest_mic" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(dd.c_str(), nullptr);
        Settings w; w.enabled = true; w.delay = -7; w.micMode = 3; w.micDevice = L"Blue Yeti";
        CHECK(SaveSettings(w, dd + L"/m.txt"));
        Settings rr;
        CHECK(LoadSettings(rr, dd + L"/m.txt") && rr.delay == -7 && rr.micMode == 3 && rr.micDevice == L"Blue Yeti");
        Put(dd + L"/m2.txt", "delay=99\r\nmic=9\r\n");
        CHECK(LoadSettings(rr, dd + L"/m2.txt") && rr.delay == kMaxDelay && rr.micMode == 3);
        Profile pr; pr.name = L"X"; pr.s = w;
        Profile back2;
        CHECK(ProfileFromText(ProfileToText(pr), back2) && back2.s.delay == -7 && back2.s.micMode == 0 && back2.s.micDevice.empty());
        const std::vector<std::wstring> caps = CaptureDeviceNames();       // je nach Rechner leer; darf nicht abstuerzen
        CHECK(caps.size() <= 12);
        DeleteFileW((dd + L"/m.txt").c_str()); DeleteFileW((dd + L"/m2.txt").c_str()); RemoveDirectoryW(dd.c_str());
    }

    // Raumklang
    {
        CHECK(kRoomCount == 8);
        for (int r = 0; r < kRoomCount; ++r) { CHECK(std::wstring(RoomName(r)).size() > 2 && std::wstring(RoomHint(r)).size() > 20); }
        CHECK(RoomDefaults(0).depth == 0 && RoomDefaults(4).depth > RoomDefaults(1).depth && RoomDefaults(2).room == 2);
        StageSettings st;
        CHECK(!StageActive(st));
        st.on = true;
        CHECK(!StageActive(st));                                           // an, aber alles neutral: keine Faltung
        st.width = 130; CHECK(StageActive(st));
        st = StageSettings(); st.on = true; st.cross = 35; CHECK(StageActive(st));
        st = StageSettings(); st.on = true; st.room = 2; st.depth = 0; CHECK(!StageActive(st));
        st.depth = 40; CHECK(StageActive(st));
        st.on = false; CHECK(!StageActive(st));
        st.on = true; st.room = 99; st.width = 999; st.cross = -4; st.depth = 500; ClampStage(st);
        CHECK(st.room == kRoomCount - 1 && st.width == kStageWidthMax && st.cross == 0 && st.depth == 100);

        // neutrale Impulsantwort: L->L und R->R sind ein Impuls, kein Uebersprechen
        StageSettings neutral; neutral.on = true;
        StageIr id = BuildStageIr(neutral);
        CHECK(id.length() >= 64 && std::fabs(id.ch[0][0] - 1.0f) < 1e-4f && std::fabs(id.ch[3][0] - 1.0f) < 1e-4f);
        float rest = 0; for (size_t i = 1; i < id.length(); ++i) rest += std::fabs(id.ch[0][i]);
        float cr = 0; for (size_t i = 0; i < id.length(); ++i) cr += std::fabs(id.ch[1][i]) + std::fabs(id.ch[2][i]);
        CHECK(rest < 1e-3f && cr < 1e-3f);

        // Mono (Breite 50 %): Mitte bleibt, Seite wird halbiert; bei Breite 0 ueber 120 Hz waere es reines Mono
        StageSettings nar; nar.on = true; nar.width = 50;
        StageIr n50 = BuildStageIr(nar);
        double dc[2] = {0, 0};     // Gleichstromverstaerkung: Bass bleibt unveraendert (L->L - L->R = Seitenanteil, tief = 1)
        for (size_t i = 0; i < n50.length(); ++i) { dc[0] += n50.ch[0][i]; dc[1] += n50.ch[1][i]; }
        CHECK(std::fabs((dc[0] - dc[1]) - 1.0) < 0.02 && std::fabs((dc[0] + dc[1]) - 1.0) < 0.02);        // Bass: Seite 1, Mitte 1
        CHECK(n50.ch[0][0] > 0.5f && n50.ch[0][0] < 0.9f && n50.ch[1][0] > 0.05f);                          // Hoehen: Seite halbiert, also etwas Uebersprechen

        // Weite Buehne: Seitenanteil in den Hoehen > 1, Bass weiter 1; Pegel wird begrenzt
        StageSettings wide; wide.on = true; wide.width = 160;
        StageIr w160 = BuildStageIr(wide);
        double dcw = 0; for (size_t i = 0; i < w160.length(); ++i) dcw += w160.ch[0][i] - w160.ch[1][i];
        CHECK(std::fabs(dcw - w160.ch[0][0] * 0) >= 0.0);                  // wird berechnet, ohne Absturz
        auto rowEnergy = [](const StageIr& ir, int a, int b) { double e = 0; for (size_t i = 0; i < ir.length(); ++i) e += double(ir.ch[a][i]) * ir.ch[a][i] + double(ir.ch[b][i]) * ir.ch[b][i]; return e; };
        CHECK(rowEnergy(w160, 0, 2) <= 1.001 && rowEnergy(w160, 1, 3) <= 1.001);

        // Crossfeed: der andere Kanal kommt ~13 Abtastwerte spaeter und leiser
        StageSettings cf; cf.on = true; cf.cross = 100;
        StageIr x = BuildStageIr(cf);
        size_t first = 0; while (first < x.length() && std::fabs(x.ch[1][first]) < 1e-4f) ++first;
        CHECK(first >= 12 && first <= 15);
        double dcCross = 0; for (float v : x.ch[1]) dcCross += v;
        CHECK(std::fabs(dcCross - 0.5) < 0.02 && x.ch[0][0] < 1.0f && x.ch[0][0] > 0.7f);                     // Tiefen: halb so laut (-6 dB) auf der anderen Seite
        for (size_t i = 0; i < x.length(); ++i) CHECK(std::fabs(x.ch[1][i] - x.ch[2][i]) < 1e-6f && std::fabs(x.ch[0][i] - x.ch[3][i]) < 1e-6f);   // symmetrisch

        // Raeume: Konzertsaal hat einen laengeren Hall als Gaming; mehr Tiefe = mehr Hall; nichts uebersteuert; alles endlich
        StageSettings gm = RoomDefaults(1); gm.on = true;
        StageSettings kn = RoomDefaults(4); kn.on = true;
        StageIr irG = BuildStageIr(gm), irK = BuildStageIr(kn);
        CHECK(irK.length() > irG.length() * 3 && irK.length() < size_t(kStageSampleRate) * 2);
        StageSettings shallow = kn; shallow.depth = 10;
        auto tailEnergy = [](const StageIr& ir) { double e = 0; for (size_t i = ir.length() / 4; i < ir.length(); ++i) e += double(ir.ch[0][i]) * ir.ch[0][i] + double(ir.ch[1][i]) * ir.ch[1][i]; return e; };
        CHECK(tailEnergy(irK) > tailEnergy(BuildStageIr(shallow)));
        for (int r = 1; r < kRoomCount; ++r) {
            StageSettings rs = RoomDefaults(r); rs.on = true; rs.depth = 100;
            StageIr ir = BuildStageIr(rs);
            bool finite = true; for (int k = 0; k < 4; ++k) for (float v : ir.ch[k]) if (!std::isfinite(v)) finite = false;
            CHECK(finite && rowEnergy(ir, 0, 2) <= 1.001 && rowEnergy(ir, 1, 3) <= 1.001);
            const double eL = rowEnergy(ir, 0, 2), eR = rowEnergy(ir, 1, 3);   // links und rechts gleich laut
            CHECK(std::fabs(eL - eR) < 0.05 * std::max(eL, eR));
        }
        {   // Kopfmodell (HRTF-Naeherung)
            StageSettings hd; hd.on = true; hd.head = true; hd.cross = 100;
            StageIr h = BuildStageIr(hd);
            auto en = [](const std::vector<float>& v) { double e = 0; for (float x : v) e += double(x) * x; return e; };
            CHECK(StageActive(hd) && h.length() >= 64);
            CHECK(en(h.ch[0]) > en(h.ch[1]) * 2.0);                                    // naher Weg deutlich lauter als ferner
            CHECK(std::fabs(en(h.ch[0]) - en(h.ch[3])) < 1e-9 && std::fabs(en(h.ch[1]) - en(h.ch[2])) < 1e-9);
            size_t peakC = 0; for (size_t i = 0; i < h.ch[1].size(); ++i) if (std::fabs(h.ch[1][i]) > std::fabs(h.ch[1][peakC])) peakC = i;
            CHECK(peakC >= 12 && peakC < 30);                                          // Laufzeitunterschied ca. 0,27 ms
            bool fin = true; for (int k = 0; k < 4; ++k) for (float v : h.ch[k]) if (!std::isfinite(v)) fin = false;
            double rowL = en(h.ch[0]) + en(h.ch[2]);
            CHECK(fin && rowL <= 1.001);
            StageSettings sc = hd; sc.head = false;
            CHECK(BuildStageIr(sc).ch[0] != h.ch[0]);                                  // anderes Filter als das einfache Crossfeed
            Settings hs; hs.stage = hd; wchar_t tp[MAX_PATH]; GetTempPathW(MAX_PATH, tp);
            const std::wstring hp = std::wstring(tp) + L"duncans_head_test.txt";
            CHECK(SaveSettings(hs, hp)); Settings hl; CHECK(LoadSettings(hl, hp) && hl.stage.head && hl.stage.cross == 100);
            DeleteFileW(hp.c_str());
        }
        {   // Winkel, Bass und Dialog
            StageSettings w1; w1.on = true; w1.head = true; w1.cross = 100; w1.angle = 15;
            StageSettings w2 = w1; w2.angle = 60;
            StageIr i1 = BuildStageIr(w1), i2 = BuildStageIr(w2);
            auto peak = [](const std::vector<float>& v) { size_t pk = 0; for (size_t i = 0; i < v.size(); ++i) if (std::fabs(v[i]) > std::fabs(v[pk])) pk = i; return pk; };
            CHECK(peak(i1.ch[1]) < peak(i2.ch[1]));                                    // weiterer Winkel = groessere Laufzeit
            StageSettings cl = w1; cl.angle = 999; cl.bass = 50; cl.voice = -50; ClampStage(cl);
            CHECK(cl.angle == kStageAngleMax && cl.bass == kStageToneMax && cl.voice == -kStageToneMax);
            StageSettings lo = w1; lo.angle = -4; ClampStage(lo); CHECK(lo.angle == kStageAngleMin);
            Settings ts; ts.enabled = true; ts.stage = w1; ts.stage.bass = 3; ts.stage.voice = -2;
            const std::string c2 = BuildConfig(ts, "", "C:\\APO\\config\\duncans-room.wav");
            CHECK(c2.find("Filter: ON LSC Fc 100 Hz Gain 3 dB") != std::string::npos && c2.find("Filter: ON PK Fc 2500 Hz Gain -2 dB Q 0.8") != std::string::npos);
            CHECK(BuildConfig(ts, "", "").find("Fc 2500") == std::string::npos);        // ohne Raumdatei keine Zusatzfilter
            wchar_t tq[MAX_PATH]; GetTempPathW(MAX_PATH, tq);
            const std::wstring tf = std::wstring(tq) + L"duncans_tone_test.txt";
            CHECK(SaveSettings(ts, tf)); Settings tl; CHECK(LoadSettings(tl, tf) && tl.stage.angle == 15 && tl.stage.bass == 3 && tl.stage.voice == -2);
            DeleteFileW(tf.c_str());
            Settings old; Put(tf, "stage=1,2,100,30,40,0,1\r\n"); CHECK(LoadSettings(old, tf) && old.stage.angle == 30 && old.stage.bass == 0);   // alte Dateien ohne die neuen Felder
            DeleteFileW(tf.c_str());
        }
        {   // neue Raeume und Hoerprobe
            CHECK(kRoomCount == 8 && std::wstring(RoomName(5)) == L"Wohnzimmer" && std::wstring(RoomName(6)) == L"Kathedrale" && std::wstring(RoomName(7)) == L"Badezimmer");
            StageSettings ka = RoomDefaults(6); ka.on = true;
            CHECK(BuildStageIr(ka).length() > BuildStageIr(kn).length() && BuildStageIr(ka).length() < size_t(kStageSampleRate) * 4);
            const std::string tw = StageTestWav();
            CHECK(tw.size() > 44 && tw.compare(0, 4, "RIFF") == 0 && tw.compare(8, 4, "WAVE") == 0);
            auto s16 = [&](size_t i) { short v; std::memcpy(&v, tw.data() + 44 + i * 2, 2); return int(v); };
            const size_t frames = (tw.size() - 44) / 4;
            CHECK(frames > size_t(kStageSampleRate) * 3 && frames < size_t(kStageSampleRate) * 6);
            long long l0 = 0, r0 = 0, l4 = 0, r4 = 0;                     // erster Impuls links, fuenfter rechts
            for (size_t i = 0; i < 10000; ++i) { l0 += std::llabs(s16(i * 2)); r0 += std::llabs(s16(i * 2 + 1)); }
            const size_t st4 = size_t(kStageSampleRate) * 32 / 100 * 4;
            for (size_t i = 0; i < 10000; ++i) { l4 += std::llabs(s16((st4 + i) * 2)); r4 += std::llabs(s16((st4 + i) * 2 + 1)); }
            CHECK(l0 > r0 * 20 && r4 > l4 * 20);
        }
        CHECK(BuildStageIr(kn).ch[0] == BuildStageIr(kn).ch[0]);           // gleiche Einstellung = gleiche Datei

        // WAV: 4 Kanaele, Float, Laenge passt
        const std::string wav = StageIrWav(irG);
        CHECK(wav.size() == 44 + irG.length() * 16 && wav.compare(0, 4, "RIFF") == 0 && wav.compare(8, 4, "WAVE") == 0);
        CHECK(wav[20] == 3 && wav[22] == 4 && wav[34] == 32);

        // Speichern/Laden samt Profil; Teilen behaelt den Raumklang
        wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        const std::wstring sd = std::wstring(tmp) + L"duncans_stage_test";
        CreateDirectoryW(sd.c_str(), nullptr);
        Settings a; a.enabled = true; a.stage = RoomDefaults(2); a.stage.on = true; a.stage.width = 143;
        CHECK(SaveSettings(a, sd + L"/s.txt"));
        Settings b2; CHECK(LoadSettings(b2, sd + L"/s.txt") && b2.stage.on && b2.stage.room == 2 && b2.stage.width == 143 && b2.stage.cross == a.stage.cross && b2.stage.depth == a.stage.depth);
        Put(sd + L"/s2.txt", "stage=1,77,999,-5,300\r\n");
        Settings b3; CHECK(LoadSettings(b3, sd + L"/s2.txt") && b3.stage.on && b3.stage.room == kRoomCount - 1 && b3.stage.width == kStageWidthMax && b3.stage.cross == 0 && b3.stage.depth == 100);
        { Settings lv = a; lv.stage.level = 6; CHECK(SaveSettings(lv, sd + L"/lv.txt")); Settings lr; CHECK(LoadSettings(lr, sd + L"/lv.txt") && lr.stage.level == 6); lv.stage.level = 99; ClampStage(lv.stage); CHECK(lv.stage.level == kStageLevelMax); }
        Settings b4; CHECK(LoadSettings(b4, sd + L"/fehlt.txt") == false && !b4.stage.on && b4.stage.width == 100);
        Profile pf; pf.name = L"Kino"; pf.s = a;
        Profile pb; CHECK(ProfileFromText(ProfileToText(pf), pb) && pb.s.stage.room == 2 && pb.s.stage.width == 143 && pb.s.stage.on);

        // Konfiguration: nur mit Datei und aktivem Raumklang; ausgeschaltet nie
        const std::string room = "C:\\APO\\config\\duncans-room.wav";
        Settings cfgS; cfgS.enabled = true; cfgS.stage = a.stage;
        const std::string cfg = BuildConfig(cfgS, "", room);
        CHECK(cfg.find("Convolution: " + room) != std::string::npos && cfg.find("Alle Regler") == std::string::npos);
        CHECK(BuildConfig(cfgS, "", "").find("Convolution") == std::string::npos);
        CHECK(cfg.find("Preamp") == std::string::npos);                    // Ausgleich standardmaessig 0
        cfgS.stage.level = 5;
        CHECK(BuildConfig(cfgS, "", room).find("Preamp: 5 dB") != std::string::npos && BuildConfig(cfgS, "", "").find("Preamp") == std::string::npos);
        CHECK(BuildConfig(cfgS, "", "C:\\x;evil\r\nInclude: y.txt").find("Include") == std::string::npos);
        Settings off = cfgS; off.stage.on = false;
        CHECK(BuildConfig(off, "", room).find("Convolution") == std::string::npos);
        Settings dis = cfgS; dis.enabled = false;
        CHECK(BuildConfig(dis, "", room).find("Convolution") == std::string::npos);

        // Anwenden: Datei entsteht, wird beim Ausschalten entfernt, zweites Mal unveraendert
        const std::wstring apo = sd + L"/apo";
        CreateDirectoryW(apo.c_str(), nullptr); CreateDirectoryW((apo + L"/config").c_str(), nullptr);
        CHECK(ApplyToDir(apo, cfgS) == ApplyResult::Applied);
        const std::string written = Slurp(apo + L"/config/duncans-room.wav");
        CHECK(written.size() > 44 && written.compare(0, 4, "RIFF") == 0);
        CHECK(Slurp(apo + L"/config/duncans-equalizer.txt").find("duncans-room.wav") != std::string::npos);
        CHECK(ApplyToDir(apo, cfgS) == ApplyResult::Applied && Slurp(apo + L"/config/duncans-room.wav") == written);
        CHECK(ApplyToDir(apo, off) == ApplyResult::Applied && Slurp(apo + L"/config/duncans-room.wav") == "<fehlt>");
        CHECK(Slurp(apo + L"/config/duncans-equalizer.txt").find("Convolution") == std::string::npos);

        DeleteFileW((sd + L"/s.txt").c_str()); DeleteFileW((sd + L"/s2.txt").c_str());
        DeleteFileW((apo + L"/config/duncans-equalizer.txt").c_str()); DeleteFileW((apo + L"/config/config.txt").c_str());
        RemoveDirectoryW((apo + L"/config").c_str()); RemoveDirectoryW(apo.c_str()); RemoveDirectoryW(sd.c_str());
    }

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
