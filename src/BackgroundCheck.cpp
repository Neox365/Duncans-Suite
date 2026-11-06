// BackgroundCheck.cpp - "--check": Hintergrund-Pruefung ohne Fenster (von der geplanten Aufgabe gestartet).
// Ablauf: Geraete scannen, Windows Update und Hersteller-Tabelle abfragen; gibt es Updates, erscheint eine Benachrichtigung
// (Klick oeffnet das Programm), sonst beendet sich das Programm still.
#include "CheckTask.h"

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <vector>

#include "HardwareScanner.h"
#include "SuiteSafety.h"
#include "UpdateChecker.h"
#include "Utils.h"
#include "VendorReleases.h"

namespace drvcheck {

namespace {
    NOTIFYICONDATAW g_nid{};
    const UINT WM_CHECK_TRAY = WM_APP + 31;

    LRESULT CALLBACK CheckProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_CHECK_TRAY:
        {
            const UINT ev = LOWORD(lp);
            if (ev == NIN_BALLOONUSERCLICK || ev == NIN_SELECT || ev == WM_LBUTTONUP)      // Klick: das Programm oeffnen
            {
                wchar_t exe[MAX_PATH] = {};
                GetModuleFileNameW(nullptr, exe, MAX_PATH);
                ShellExecuteW(nullptr, L"open", exe, nullptr, nullptr, SW_SHOWNORMAL);
                DestroyWindow(h);
            }
            else if (ev == NIN_BALLOONTIMEOUT || ev == NIN_BALLOONHIDE)
                SetTimer(h, 2, 4000, nullptr);
            return 0;
        }
        case WM_TIMER:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }

    std::wstring IniPath()
    {
        wchar_t local[MAX_PATH] = {};
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local)))
            return L"";
        const std::wstring dir = std::wstring(local) + L"\\DuncansDriverUpdater";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\settings.ini";
    }
}

int RunCheck(HINSTANCE inst)
{
    suite_safety::Log(L"DriverUpdater", L"Hintergrund-Prüfung gestartet");
    RefreshVendorReleasesAsync();                                      // neue Hersteller-Tabelle, wirkt, sobald sie da ist
    std::vector<DriverInfo> drivers = HardwareScanner::ScanBlocking();
    if (drivers.empty())
    {
        suite_safety::Log(L"DriverUpdater", L"Hintergrund-Prüfung: keine Geräte gefunden");
        return 1;
    }
    UpdateChecker checker;
    checker.StartAsync(nullptr);
    const ULONGLONG t0 = GetTickCount64();
    while (checker.IsRunning() && GetTickCount64() - t0 < 5ull * 60 * 1000)
        Sleep(500);
    if (checker.IsRunning())
    {
        suite_safety::Log(L"DriverUpdater", L"Hintergrund-Prüfung: Zeitüberschreitung bei Windows Update");
        ExitProcess(2);                                                // der Such-Thread laeuft noch; Prozess ohne Warten beenden
    }
    UpdateChecker::Apply(drivers, checker.GetUpdates());
    ApplyVendorReleases(drivers, LoadVendorReleases());

    std::vector<std::wstring> names;
    for (const DriverInfo& d : drivers)
        if (d.status == DriverStatus::UpdateAvailable)
            names.push_back(d.deviceName);
    const std::wstring ini = IniPath();
    if (!ini.empty())
        WritePrivateProfileStringW(L"Allgemein", L"LetztePruefung", CurrentTimeText().c_str(), ini.c_str());
    suite_safety::Log(L"DriverUpdater", L"Hintergrund-Prüfung: " + std::to_wstring(names.size()) + L" Update(s) gefunden" + (checker.GetError().empty() ? L"" : L" (Hinweis: " + checker.GetError() + L")"));
    if (names.empty())
        return 0;

    std::wstring text = std::to_wstring(names.size()) + (names.size() == 1 ? L" Treiber-Update" : L" Treiber-Updates") + L" verfügbar: ";
    for (size_t i = 0; i < names.size() && i < 2; ++i)
        text += (i ? L", " : L"") + names[i];
    if (names.size() > 2)
        text += L" …";
    text += L"  (Klick öffnet den Driver Updater)";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = CheckProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"DuncansDriverUpdaterCheck";
    RegisterClassExW(&wc);
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!h)
        return 1;
    g_nid = NOTIFYICONDATAW{};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = h;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
    g_nid.uCallbackMessage = WM_CHECK_TRAY;
    g_nid.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wcscpy_s(g_nid.szTip, L"Duncan's Driver Updater");
    wcscpy_s(g_nid.szInfoTitle, L"Treiber-Updates gefunden");
    wcsncpy_s(g_nid.szInfo, text.c_str(), _TRUNCATE);
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    SetTimer(h, 1, 25000, nullptr);                                    // spaetestens nach 25 s beenden
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

}  // namespace drvcheck
