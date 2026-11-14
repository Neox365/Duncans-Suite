// Test der Zock-Modus-Logik (Duncan's Boost). Nur Windows. Startet fuer die Beendentests Kopien von sich selbst als Fenster-Attrappe.
// g++ -std=c++17 -DUNICODE -D_UNICODE -I.. BoostTest.cpp ../BoostLogic.cpp ../AudioFocus.cpp -o BoostTest.exe -lole32 -lpowrprof -lpsapi -ldwmapi -lshell32 -luser32 -ladvapi32 -static
#include "../BoostLogic.h"
#include "../AudioFocus.h"
#include "../Crosshair.h"
#include "../Profiles.h"

#include <shellapi.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace boostmode;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #cond); } } while (0)

// ---- Fenster-Attrappe: "--dummy" schliesst sich bei WM_CLOSE, "--stubborn" ignoriert WM_CLOSE ----
static bool g_stubborn = false;
static LRESULT CALLBACK DummyProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE) { if (!g_stubborn) DestroyWindow(h); return 0; }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

static int RunDummy(bool stubborn) {
    g_stubborn = stubborn;
    WNDCLASSW wc{}; wc.lpfnWndProc = DummyProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"BoostDummy";
    RegisterClassW(&wc);
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"BoostDummyFenster", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 200, 120, nullptr, nullptr, wc.hInstance, nullptr);
    if (!h) return 1;
    SetTimer(h, 1, 60000, nullptr);   // Notausstieg
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { if (m.message == WM_TIMER && m.wParam == 1 && m.hwnd == h) break; DispatchMessageW(&m); }
    return 0;
}

static std::wstring SelfPath() { wchar_t b[MAX_PATH * 2] = {}; GetModuleFileNameW(nullptr, b, MAX_PATH * 2); return b; }
static std::wstring SelfExe() { std::wstring p = Lower(SelfPath()); return p.substr(p.rfind(L'\\') + 1); }

static HANDLE Spawn(const wchar_t* arg) {
    std::wstring cmd = L"\"" + SelfPath() + L"\" " + arg;
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return nullptr;
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

static bool WaitListed(const std::wstring& exe, int ms = 8000) {
    for (int t = 0; t < ms; t += 200) {
        for (const AppInfo& a : ListApps()) if (a.exe == exe) return true;
        Sleep(200);
    }
    return false;
}

static bool Contains(const std::vector<std::wstring>& v, const std::wstring& s) { for (const auto& x : v) if (x == s) return true; return false; }

// ---- Ton nach Fokus mit Attrappe statt echter Lautsprecher ----
struct FakeAudio : AudioBackend {
    std::vector<AudioSession> s;
    std::vector<AudioSession> List() override { return s; }
    bool SetVolume(const std::wstring& id, float v) override { for (auto& x : s) if (x.id == id) { x.volume = v; return true; } return false; }
    bool SetMute(const std::wstring& id, bool m) override { for (auto& x : s) if (x.id == id) { x.muted = m; return true; } return false; }
    AudioSession* Get(const std::wstring& id) { for (auto& x : s) if (x.id == id) return &x; return nullptr; }
};
static bool Near(float a, float b) { return a > b - 0.01f && a < b + 0.01f; }

static void AudioTests() {
    const std::vector<std::wstring> games = { L"game.exe" };
    FakeAudio a;
    a.s = { {L"g", L"game.exe", 1.0f, false}, {L"b1", L"chrome.exe", 0.8f, false}, {L"b2", L"chrome.exe", 0.5f, false},
            {L"d", L"discord.exe", 1.0f, false}, {L"sys", L"", 1.0f, false} };
    AudioFocus f;
    AudioOptions both; both.duck = true; both.muteGame = true;

    f.Tick(a, games, L"game.exe", both);                                   // Spiel vorne: andere leiser, Spiel hoerbar
    CHECK(Near(a.Get(L"b1")->volume, 0.32f) && Near(a.Get(L"b2")->volume, 0.2f));
    CHECK(Near(a.Get(L"d")->volume, 1.0f) && Near(a.Get(L"sys")->volume, 1.0f) && Near(a.Get(L"g")->volume, 1.0f));
    CHECK(!a.Get(L"g")->muted && f.Active());
    f.Tick(a, games, L"game.exe", both);                                   // zweiter Durchlauf aendert nichts (kein doppeltes Leisermachen)
    CHECK(Near(a.Get(L"b1")->volume, 0.32f));

    f.Tick(a, games, L"chrome.exe", both);                                 // Alt+Tab: Browser wieder normal, Spiel stumm
    CHECK(Near(a.Get(L"b1")->volume, 0.8f) && Near(a.Get(L"b2")->volume, 0.5f));
    CHECK(a.Get(L"g")->muted);
    f.Tick(a, games, L"game.exe", both);                                   // zurueck: Spiel hoerbar, Browser wieder leiser
    CHECK(!a.Get(L"g")->muted && Near(a.Get(L"b1")->volume, 0.32f));

    // Du bewegst den Regler selbst: Boost arbeitet nicht dagegen
    a.Get(L"b1")->volume = 0.9f;
    f.Tick(a, games, L"game.exe", both);
    CHECK(Near(a.Get(L"b1")->volume, 0.9f));
    f.Tick(a, games, L"chrome.exe", both);
    CHECK(Near(a.Get(L"b1")->volume, 0.9f));                               // dein Wert bleibt

    // Spiel beendet: alles normal
    f.Tick(a, games, L"game.exe", both);
    a.s.erase(a.s.begin());                                                // Spiel-Sitzung weg
    f.Tick(a, games, L"explorer.exe", both);
    CHECK(Near(a.Get(L"b2")->volume, 0.5f));

    // Spiel war schon von dir stumm geschaltet: Boost schaltet nicht ein und nicht wieder aus
    FakeAudio m; m.s = { {L"g", L"game.exe", 1.0f, true} };
    AudioFocus f2;
    f2.Tick(m, games, L"chrome.exe", both);
    f2.Tick(m, games, L"game.exe", both);
    CHECK(m.Get(L"g")->muted && !f2.Active());

    // Optionen einzeln und RestoreAll
    FakeAudio r; r.s = { {L"g", L"game.exe", 1.0f, false}, {L"b", L"chrome.exe", 1.0f, false} };
    AudioFocus f3; AudioOptions duckOnly; duckOnly.duck = true;
    f3.Tick(r, games, L"chrome.exe", duckOnly);
    CHECK(!r.Get(L"g")->muted && Near(r.Get(L"b")->volume, 1.0f));         // kein Spiel vorne: nichts passiert
    f3.Tick(r, games, L"game.exe", duckOnly);
    CHECK(Near(r.Get(L"b")->volume, 0.4f));
    f3.RestoreAll(r);
    CHECK(Near(r.Get(L"b")->volume, 1.0f) && !f3.Active());
    AudioFocus f4; AudioOptions muteOnly; muteOnly.muteGame = true;
    f4.Tick(r, games, L"chrome.exe", muteOnly);
    CHECK(r.Get(L"g")->muted && Near(r.Get(L"b")->volume, 1.0f));
    f4.Tick(r, games, L"chrome.exe", AudioOptions{});                      // Option aus: Spiel wieder hoerbar
    CHECK(!r.Get(L"g")->muted);

    CHECK(IsNeverDucked(L"discord.exe") && IsNeverDucked(L"ts3client_win64.exe") && !IsNeverDucked(L"chrome.exe"));
}

static void ProfileTests() {
    std::vector<Profile> all(2);
    all[0].name = L"Arbeit"; all[0].paths = { L"C:\\a\\Chrome.exe", L"C:\\a\\word.exe", L"C:\\Windows\\explorer.exe" };
    all[1].name = L"Gaming"; all[1].paths = { L"C:\\b\\steam.exe", L"C:\\a\\chrome.exe" };
    CHECK(ExeOfPath(L"C:\\a\\Chrome.EXE") == L"chrome.exe" && ExeOfPath(L"x.exe") == L"x.exe");

    SwitchPlan p = PlanSwitch(all, 1, { L"chrome.exe", L"word.exe", L"explorer.exe" });   // Arbeit -> Gaming
    CHECK(p.closeExes == std::vector<std::wstring>{ L"word.exe" });                      // Browser bleibt (in beiden), explorer ist gesperrt
    CHECK(p.startPaths == std::vector<std::wstring>{ L"C:\\b\\steam.exe" });

    p = PlanSwitch(all, 0, { L"steam.exe", L"chrome.exe" });                              // Gaming -> Arbeit
    CHECK(p.closeExes == std::vector<std::wstring>{ L"steam.exe" });
    CHECK(p.startPaths == std::vector<std::wstring>{ L"C:\\a\\word.exe" });   // chrome laeuft schon, explorer wird nie gestartet
    p = PlanSwitch(all, 1, { L"steam.exe", L"chrome.exe" });                              // schon alles da
    CHECK(p.closeExes.empty() && p.startPaths.empty());
    p = PlanSwitch(all, 7, {});
    CHECK(p.closeExes.empty() && p.startPaths.empty());

    CHECK(UniqueProfileName(all, L"Neu") == L"Neu" && UniqueProfileName(all, L"arbeit") == L"arbeit 2");
    CHECK(UniqueProfileName(all, L"   ") == L"Profil" && UniqueProfileName(all, L"a=b|c") == L"a b c");
    CHECK(UniqueProfileName(all, std::wstring(80, L'x')).size() == 30);

    CHECK(SaveProfiles(all));
    const std::vector<Profile> back = LoadProfiles();
    CHECK(back.size() == 2 && back[0].name == L"Arbeit" && back[0].paths == all[0].paths && back[1].paths == all[1].paths);
    CHECK(SaveProfiles({}) && LoadProfiles().empty());
}

static void ClipTests() {
    CHECK(ClampClipSeconds(30) == 30 && ClampClipSeconds(15) == 15 && ClampClipSeconds(60) == 60);
    CHECK(ClampClipSeconds(0) == 15 && ClampClipSeconds(100) == 60 && ClampClipSeconds(40) == 30 && ClampClipSeconds(-5) == 15 && ClampClipSeconds(46) == 60);
    CHECK(NextClipSeconds(15) == 30 && NextClipSeconds(30) == 60 && NextClipSeconds(60) == 15 && NextClipSeconds(7) == 30);
    const std::vector<std::wstring> games = { L"game.exe" };
    CHECK(ClipShouldRecord({}, L"chrome.exe") && ClipShouldRecord({}, L""));                 // kein Spiel angehakt: immer
    CHECK(ClipShouldRecord(games, L"game.exe") && !ClipShouldRecord(games, L"chrome.exe") && !ClipShouldRecord(games, L""));
    const std::wstring dir = DataDir() + L"\\cliptest";
    Settings s; s.optClip = true; s.clipSeconds = 60;
    CHECK(SaveSettings(s));
    const Settings back = LoadSettings();
    CHECK(back.optClip && back.clipSeconds == 60);
    s.optClip = false; s.clipSeconds = 999;
    CHECK(SaveSettings(s));
    const Settings b2 = LoadSettings();
    CHECK(!b2.optClip && b2.clipSeconds == 60);
}

static void CrosshairTests() {
    CrosshairSpec s; s.on = true;
    const std::vector<std::wstring> games = { L"game.exe" };
    CHECK(CrosshairVisible(s, games, L"game.exe", L"duncansboost.exe"));
    CHECK(!CrosshairVisible(s, games, L"chrome.exe", L"duncansboost.exe"));          // nicht ueber dem Browser
    CHECK(CrosshairVisible(s, games, L"duncansboost.exe", L"duncansboost.exe"));     // Vorschau beim Einstellen
    CHECK(!CrosshairVisible(s, games, L"", L"duncansboost.exe"));
    CHECK(CrosshairVisible(s, {}, L"chrome.exe", L"duncansboost.exe"));              // kein Spiel angehakt: immer
    s.on = false;
    CHECK(!CrosshairVisible(s, games, L"game.exe", L"duncansboost.exe") && !CrosshairVisible(s, {}, L"x.exe", L""));
    CrosshairSpec bad; bad.style = 99; bad.color = -5; bad.size = 7;
    bad = ClampCrosshair(bad);
    CHECK(bad.style == CrosshairStyleCount() - 1 && bad.color == 0 && bad.size == CrosshairSizeCount() - 1);
    CHECK(CrosshairStyleCount() >= 4 && CrosshairColorCount() >= 4 && CrosshairSizeCount() == 3);
    CHECK(CrosshairSizePx(0) < CrosshairSizePx(1) && CrosshairSizePx(1) < CrosshairSizePx(2));
    CHECK(CrosshairColorValue(0) != CrosshairColorValue(1) && std::wstring(CrosshairColorName(2)) == L"Weiß");
    // Einstellungen speichern und laden
    Settings st; st.optCross = true; st.crossStyle = 3; st.crossColor = 4; st.crossSize = 2;
    CHECK(SaveSettings(st));
    const Settings back = LoadSettings();
    CHECK(back.optCross && back.crossStyle == 3 && back.crossColor == 4 && back.crossSize == 2);
}

int main() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1 && std::wstring(argv[1]) == L"--dummy") return RunDummy(false);
    if (argc > 1 && std::wstring(argv[1]) == L"--stubborn") return RunDummy(true);

    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    const std::wstring dir = std::wstring(tmp) + L"BoostTest_" + std::to_wstring(GetCurrentProcessId());
    SetDataDirForTest(dir);

    // Sperrliste und Hilfsfunktionen
    CHECK(IsProtectedExe(L"explorer.exe"));
    CHECK(IsProtectedExe(L"svchost.exe"));
    CHECK(IsProtectedExe(L"duncansboost.exe"));
    CHECK(!IsProtectedExe(L"spotify.exe"));
    CHECK(Lower(L"DisCord.EXE") == L"discord.exe");
    CHECK(Split(L"a.exe||b.exe|").size() == 2);
    {   // Vorschlagsliste: klein geschrieben, nie gesperrt, und keine Launcher/Chat/Musik, die beim Spielen gebraucht werden
        const std::vector<std::wstring> sug = SuggestedHelpers();
        CHECK(!sug.empty());
        for (const std::wstring& e : sug) { CHECK(e == Lower(e)); CHECK(!IsProtectedExe(e)); }
        CHECK(!Contains(sug, L"steam.exe") && !Contains(sug, L"epicgameslauncher.exe") && !Contains(sug, L"battle.net.exe"));
        CHECK(!Contains(sug, L"discord.exe") && !Contains(sug, L"spotify.exe") && !Contains(sug, L"brave.exe"));
    }
    CHECK(Join({ L"a", L"b" }) == L"a|b");
    GUID g{}, g2{};
    CHECK(GetActiveScheme(g));
    CHECK(GuidFromString(GuidToString(g), g2) && IsEqualGUID(g, g2));

    // Einstellungen und Zustand (inkl. Umlaute)
    Settings s;
    s.closeApps = { L"spotify.exe", L"überwacher.exe" };
    s.gameApps = { L"game.exe" };
    s.optPower = false; s.optToast = true; s.optAuto = true; s.optRestart = false;
    CHECK(!SettingsFileExists());
    CHECK(SaveSettings(s));
    CHECK(SettingsFileExists());
    Settings s2 = LoadSettings();
    CHECK(s2.closeApps == s.closeApps && s2.gameApps == s.gameApps);
    CHECK(!s2.optPower && s2.optToast && s2.optAuto && !s2.optRestart);
    State st; st.active = true; st.auto_ = true; st.powerChanged = true; st.prevScheme = g; st.toastChanged = true; st.prevToast = { true, 1 };
    st.closedPaths = { L"C:\\Spiele\\Größe\\a.exe", L"C:\\b.exe" };
    CHECK(SaveState(st));
    State st2 = LoadState();
    // Spiel-Einstellungen: gesichert und gelesen wie die Toast-Werte
    st.gameMask = 5; st.prevGame[0] = { true, 0 }; st.prevGame[2] = { false, 0 };
    CHECK(SaveState(st));
    {
        State g2 = LoadState();
        CHECK(g2.gameMask == 5 && g2.prevGame[0].exists && g2.prevGame[0].value == 0 && !g2.prevGame[2].exists);
    }
    CHECK(GameTweakWanted(0) == 1 && GameTweakWanted(1) == 0 && GameTweakWanted(2) == 0);
    for (int i = 0; i < kGameTweaks; ++i) { const RegDword r0 = GetGameTweak(i); CHECK(SetGameTweak(i, r0)); const RegDword r1 = GetGameTweak(i); CHECK(r1.exists == r0.exists && (!r0.exists || r1.value == r0.value)); }
    CHECK(st2.active && st2.auto_ && st2.powerChanged && IsEqualGUID(st2.prevScheme, g) && st2.toastChanged && st2.prevToast.exists && st2.prevToast.value == 1);
    CHECK(st2.closedPaths == st.closedPaths);
    CHECK(SaveState(State{}));
    CHECK(!LoadState().active);

    // Energiesparplan / Toast: nur lesen und denselben Wert zurueckschreiben
    CHECK(SetActiveScheme(g));
    GUID perf{}; const bool havePerf = FindPerformanceScheme(perf);
    std::printf("Hinweis: Leistungsplan %s\n", havePerf ? "vorhanden" : "nicht vorhanden");
    const ToastState t0 = GetToast();
    CHECK(SetToast(t0));
    const ToastState t1 = GetToast();
    CHECK(t1.exists == t0.exists && (!t0.exists || t1.value == t0.value));

    // Auflisten: der eigene Prozess und gesperrte Programme tauchen nie auf
    for (const AppInfo& a : ListApps()) { CHECK(!IsProtectedExe(a.exe)); CHECK(a.exe != SelfExe() || a.procCount >= 1); }

    // Freundlich schliessen
    const std::wstring me = SelfExe();
    HANDLE p1 = Spawn(L"--dummy");
    CHECK(p1 != nullptr);
    CHECK(WaitListed(me));
    bool listedAsOther = false;
    for (const AppInfo& a : ListApps()) if (a.exe == me) listedAsOther = (a.title == L"BoostDummyFenster" && a.ramBytes > 0 && a.procCount == 1);
    CHECK(listedAsOther);
    CHECK(AnyRunning({ me }));
    CloseResult r1 = CloseApps({ me }, 8000, false);
    CHECK(r1.closed == 1 && r1.forced == 0 && r1.failed == 0);
    CHECK(Contains(r1.names, me) && r1.paths.size() == 1);
    CHECK(WaitForSingleObject(p1, 3000) == WAIT_OBJECT_0);   // nur die Attrappe wurde beendet, dieser Test laeuft noch
    CloseHandle(p1);

    // Widerspenstiges Programm: ohne force bleibt es, mit force wird es hart beendet
    HANDLE p2 = Spawn(L"--stubborn");
    CHECK(p2 != nullptr);
    CHECK(WaitListed(me));
    CloseResult r2 = CloseApps({ me }, 1500, false);
    CHECK(r2.failed == 1 && r2.closed == 0);
    CloseResult r3 = CloseApps({ me }, 1000, true);
    CHECK(r3.closed == 1 && r3.forced == 1 && r3.failed == 0);
    CHECK(WaitForSingleObject(p2, 3000) == WAIT_OBJECT_0);
    CloseHandle(p2);

    // Gesperrte Programme werden nie beendet
    CloseResult r4 = CloseApps({ L"explorer.exe", L"svchost.exe" }, 100, true);
    CHECK(r4.closed == 0 && r4.failed == 0 && r4.paths.empty());

    // Gesamtablauf: aktivieren (Attrappe wird beendet, Plan umgestellt) und wieder herstellen
    const GUID original = g;                    // Plan vor dem Test, wird am Ende in jedem Fall wiederhergestellt
    const GUID balanced = { 0x381b4222, 0xf694, 0x41f0, { 0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e } };
    const bool switched = havePerf && !IsEqualGUID(perf, balanced) && SetActiveScheme(balanced);   // so wird der Umschaltweg wirklich geprueft
    HANDLE p3 = Spawn(L"--dummy");
    CHECK(WaitListed(me));
    Settings cfg; cfg.closeApps = { me }; cfg.optPower = true; cfg.optToast = false; cfg.optRestart = false;
    GUID before{}; GetActiveScheme(before);
    std::wstring note;
    CHECK(Activate(cfg, false, note));
    std::printf("Aktivieren: %ls\n", note.c_str());
    CHECK(LoadState().active);
    CHECK(WaitForSingleObject(p3, 3000) == WAIT_OBJECT_0);
    CloseHandle(p3);
    if (havePerf) { GUID now{}; GetActiveScheme(now); CHECK(IsEqualGUID(now, perf)); }
    CHECK(Activate(cfg, false, note) && note.find(L"bereits") != std::wstring::npos);
    CHECK(Deactivate(cfg, note));
    std::printf("Deaktivieren: %ls\n", note.c_str());
    GUID after{}; GetActiveScheme(after);
    CHECK(IsEqualGUID(before, after));
    CHECK(!LoadState().active);
    std::printf("Hinweis: Umschaltweg %s\n", switched ? "geprueft (Ausbalanciert -> Leistung -> Ausbalanciert)" : "nicht geprueft (Plan ist schon Leistung)");
    SetActiveScheme(original);                  // Sicherheitsnetz: urspruenglichen Plan in jedem Fall wiederherstellen
    GUID fin{}; GetActiveScheme(fin);
    CHECK(IsEqualGUID(fin, original));

    AudioTests();
    CrosshairTests();
    ProfileTests();
    ClipTests();
    std::printf("%d bestanden, %d fehlgeschlagen\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
