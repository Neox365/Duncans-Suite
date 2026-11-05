// Test der Zock-Modus-Logik (Duncan's Boost). Nur Windows. Startet fuer die Beendentests Kopien von sich selbst als Fenster-Attrappe.
// g++ -std=c++17 -DUNICODE -D_UNICODE -I.. BoostTest.cpp ../BoostLogic.cpp -o BoostTest.exe -lole32 -lpowrprof -lpsapi -ldwmapi -lshell32 -luser32 -ladvapi32 -static
#include "../BoostLogic.h"

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
    CHECK(Join({ L"a", L"b" }) == L"a|b");
    GUID g{}, g2{};
    CHECK(GetActiveScheme(g));
    CHECK(GuidFromString(GuidToString(g), g2) && IsEqualGUID(g, g2));

    // Einstellungen und Zustand (inkl. Umlaute)
    Settings s;
    s.closeApps = { L"spotify.exe", L"überwacher.exe" };
    s.gameApps = { L"game.exe" };
    s.optPower = false; s.optToast = true; s.optAuto = true; s.optRestart = false;
    CHECK(SaveSettings(s));
    Settings s2 = LoadSettings();
    CHECK(s2.closeApps == s.closeApps && s2.gameApps == s.gameApps);
    CHECK(!s2.optPower && s2.optToast && s2.optAuto && !s2.optRestart);
    State st; st.active = true; st.auto_ = true; st.powerChanged = true; st.prevScheme = g; st.toastChanged = true; st.prevToast = { true, 1 };
    st.closedPaths = { L"C:\\Spiele\\Größe\\a.exe", L"C:\\b.exe" };
    CHECK(SaveState(st));
    State st2 = LoadState();
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

    std::printf("%d bestanden, %d fehlgeschlagen\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
