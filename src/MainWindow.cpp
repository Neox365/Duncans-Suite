#include "MainWindow.h"

#include <dwmapi.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <memory>
#include <thread>

#include "Icons.h"
#include "Utils.h"

#ifndef APP_VERSION
#define APP_VERSION L"1.1.9"   // normalerweise aus CMake (PROJECT_VERSION)
#endif

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "msimg32.lib")
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace
{
    const wchar_t* kWindowClass = L"DuncansDriverUpdaterWnd";

    // ---- Farbpalette (Dark / Card Style) ---------------------------------------------------
    const COLORREF kBackground = RGB(18, 19, 22);
    const COLORREF kCard       = RGB(28, 30, 36);
    const COLORREF kCardBorder = RGB(45, 49, 61);   // #2D313D
    const COLORREF kCardBorderHover = RGB(96, 104, 126);
    constexpr UINT WM_BACKUP_DONE = WM_APP + 6;   // wParam = Exit-Code, lParam = new std::wstring* (Zielordner)
    const COLORREF kText       = RGB(235, 237, 242);
    const COLORREF kTextDim    = RGB(150, 155, 168);
    const COLORREF kAccent     = RGB(64, 156, 255);
    const COLORREF kAccentDark = RGB(38, 98, 170);
    const COLORREF kGreen      = RGB(52, 199, 120);
    const COLORREF kRed        = RGB(240, 84, 84);
    const COLORREF kAmber      = RGB(240, 180, 60);
    const COLORREF kDisabled   = RGB(52, 55, 64);
    const COLORREF kCardRaised = RGB(35, 38, 47);   // Treiber-Karten: etwas heller als die Panels
    const COLORREF kCardHover  = RGB(43, 47, 59);
    const COLORREF kOrange     = RGB(255, 150, 50);

    COLORREF Blend(COLORREF a, COLORREF b, int percentB)
    {
        auto mix = [&](int ca, int cb) { return (ca * (100 - percentB) + cb * percentB) / 100; };
        return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
    }

    void FillRound(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF border)
    {
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    }

    void DrawLabel(HDC dc, HFONT font, COLORREF color, RECT rc, const std::wstring& text, UINT flags = DT_LEFT)
    {
        HGDIOBJ old = SelectObject(dc, font);
        SetTextColor(dc, color);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rc,
                  flags | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, old);
    }

    int TextWidth(HDC dc, HFONT font, const std::wstring& text)
    {
        HGDIOBJ old = SelectObject(dc, font);
        SIZE size{};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
        SelectObject(dc, old);
        return size.cx;
    }

    struct Badge
    {
        std::wstring text;
        COLORREF color;
    };

    bool ContainsNoCase(std::wstring hay, std::wstring needle)
    {
        CharLowerBuffW(&hay[0], static_cast<DWORD>(hay.size()));
        CharLowerBuffW(&needle[0], static_cast<DWORD>(needle.size()));
        return hay.find(needle) != std::wstring::npos;
    }

    // Blau = Microsoft-Standardtreiber, Gruen = optimal (Herstellertreiber), Orange = Update, Rot = Problem
    Badge BadgeFor(const DriverInfo& info)
    {
        switch (info.status)
        {
        case DriverStatus::Problem:         return { L"Problem erkannt", kRed };
        case DriverStatus::UpdateAvailable: return { L"Update verfügbar", kOrange };
        case DriverStatus::Ok:
            if (ContainsNoCase(info.provider, L"microsoft"))
                return { L"Microsoft-Standard", kAccent };
            return { L"Optimal", kGreen };
        case DriverStatus::Pending:
        default:                            return { L"Scan ausstehend", kTextDim };
        }
    }

    constexpr int kGroupCount = 7;
    const wchar_t* const kGroupNames[kGroupCount] = {
        L"Grafik & Monitor", L"Audio", L"Netzwerk & Bluetooth", L"Peripherie", L"Speicher", L"System & Prozessor", L"Sonstige"
    };
    const CategoryIcon kGroupIcons[kGroupCount] = {
        CategoryIcon::Gpu, CategoryIcon::Audio, CategoryIcon::Network, CategoryIcon::Usb,
        CategoryIcon::Storage, CategoryIcon::Cpu, CategoryIcon::Other
    };

    // SetupAPI-Geraeteklasse (ueber CategoryIcon) -> Hauptgruppe
    int GroupOf(CategoryIcon icon)
    {
        switch (icon)
        {
        case CategoryIcon::Gpu:
        case CategoryIcon::Monitor:   return 0;
        case CategoryIcon::Audio:     return 1;
        case CategoryIcon::Network:
        case CategoryIcon::Bluetooth: return 2;
        case CategoryIcon::Usb:
        case CategoryIcon::Input:
        case CategoryIcon::Camera:
        case CategoryIcon::Printer:
        case CategoryIcon::Port:      return 3;
        case CategoryIcon::Storage:   return 4;
        case CategoryIcon::Cpu:
        case CategoryIcon::System:
        case CategoryIcon::Battery:   return 5;
        default:                      return 6;
        }
    }

    COLORREF IconColor(CategoryIcon icon)
    {
        switch (icon)
        {
        case CategoryIcon::Gpu:       return RGB(167, 139, 250);
        case CategoryIcon::Audio:     return RGB(244, 114, 182);
        case CategoryIcon::Network:   return RGB(34, 211, 238);
        case CategoryIcon::Usb:       return RGB(251, 191, 36);
        case CategoryIcon::Storage:   return RGB(45, 212, 191);
        case CategoryIcon::Cpu:       return RGB(96, 165, 250);
        case CategoryIcon::Input:     return RGB(129, 140, 248);
        case CategoryIcon::Monitor:   return RGB(56, 189, 248);
        case CategoryIcon::Bluetooth: return RGB(59, 130, 246);
        case CategoryIcon::Camera:    return RGB(251, 113, 133);
        case CategoryIcon::Printer:   return RGB(163, 230, 53);
        case CategoryIcon::Battery:   return RGB(74, 222, 128);
        case CategoryIcon::Port:      return RGB(251, 146, 60);
        default:                      return RGB(148, 163, 184);
        }
    }
}

namespace
{
    // %LOCALAPPDATA%\DuncansDriverUpdater\settings.ini
    std::wstring SettingsFile(bool createDir)
    {
        wchar_t buf[MAX_PATH * 2] = {};
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH * 2);
        if (n == 0 || n >= MAX_PATH * 2)
            return std::wstring();
        const std::wstring dir = std::wstring(buf) + L"\\DuncansDriverUpdater";
        if (createDir)
            CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\settings.ini";
    }

    bool ReadFlag(const wchar_t* key, bool fallback)
    {
        const std::wstring ini = SettingsFile(false);
        return ini.empty() ? fallback : GetPrivateProfileIntW(L"Allgemein", key, fallback ? 1 : 0, ini.c_str()) != 0;
    }

    bool WriteFlag(const wchar_t* key, bool value)
    {
        const std::wstring ini = SettingsFile(true);
        return !ini.empty() && WritePrivateProfileStringW(L"Allgemein", key, value ? L"1" : L"0", ini.c_str()) != FALSE;
    }
}

MainWindow::~MainWindow()
{
    m_scanner.Cancel();
    m_updater.Cancel();
    DestroyFonts();
    ReleaseBackBuffer();
    ReleaseBannerCache();
    IconsShutdown();
}

int MainWindow::S(int px) const
{
    return MulDiv(px, static_cast<int>(m_dpi), 96);
}

bool MainWindow::Create(HINSTANCE instance, int showCommand, bool autoInstall)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // alles wird selbst gezeichnet
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc))
        return false;

    m_autoInstall = autoInstall;
    m_dpi = GetDpiForSystem();
    m_hwnd = CreateWindowExW(0, kWindowClass, L"Duncan's Driver Updater", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, S(1040), S(850),
                             nullptr, nullptr, instance, this);
    if (!m_hwnd)
        return false;

    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    m_motion = animations != FALSE;
    RefreshVendorReleasesAsync();   // aktuelle Hersteller-Tabelle im Hintergrund holen (signiert, zwischengespeichert)
    m_settings.autoScan = ReadFlag(L"AutoScan", false);
    m_settings.splash = ReadFlag(L"StartAnimation", true);

    if (m_motion && m_settings.splash && !m_autoInstall && RunSplash(instance, showCommand))
    {
        // Splash hat das Hauptfenster bereits gezeigt
    }
    else
    {
        ShowWindow(m_hwnd, showCommand);
        UpdateWindow(m_hwnd);
        m_bannerP = 0.62f;
    }
    if (m_autoInstall || m_settings.autoScan)
        StartScan();   // nach dem UAC-Neustart fuer die Installation bzw. laut Einstellung
    return true;
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        OnCreate();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        OnPaint();
        return 0;
    case WM_SIZE:
        OnSize();
        return 0;
    case WM_GETMINMAXINFO:
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = S(760);
        mmi->ptMinTrackSize.y = S(660);
        return 0;
    }
    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        if (m_hoverCard != -1 || m_hoverHeader != -1)
        {
            m_hoverCard = -1;
            m_hoverHeader = -1;
            InvalidateRect(m_hwnd, &m_listRect, FALSE);
        }
        if (m_installHover)
        {
            m_installHover = false;
            InvalidateRect(m_hwnd, &m_installRect, FALSE);
        }
        if (m_buttonHover)
        {
            m_buttonHover = false;
            InvalidateRect(m_hwnd, &m_buttonRect, FALSE);
        }
        if (m_aboutHover)
        {
            m_aboutHover = false;
            InvalidateRect(m_hwnd, &m_aboutRect, FALSE);
        }
        if (m_gearHover || m_setHover != 0)
        {
            m_gearHover = false;
            m_setHover = 0;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_SETCURSOR:
        if (m_modal.active && LOWORD(lp) == HTCLIENT)
        {
            if (m_modal.hoverBtn >= 0)
            {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }
        if (LOWORD(lp) == HTCLIENT)
        {
            POINT cur{};
            GetCursorPos(&cur);
            ScreenToClient(m_hwnd, &cur);
            if (m_settingsOpen)
            {
                if (m_setHover == 0)
                    break;
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            bool clickable = (m_buttonHover && !Busy()) || m_installHover || m_gearHover || m_aboutHover || m_dragThumb ||
                             PtInRect(&m_sortRect, cur) || PtInRect(&m_exportRect, cur) || m_hoverAction != 0 || m_hoverHeader != -1;
            for (int i = 0; i < 3; ++i)
                clickable = clickable || PtInRect(&m_chipRect[i], cur);
            if (!clickable)
                break;
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONUP:
        OnLButtonUp();
        return 0;
    case WM_CAPTURECHANGED:
        if (m_pressAction != 0)
        {
            m_pressAction = 0;
            m_pressCard = -1;
            InvalidateRect(m_hwnd, &m_listRect, FALSE);
        }
        if (m_dragThumb)
        {
            m_dragThumb = false;
            InvalidateRect(m_hwnd, &m_listRect, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL:
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    case WM_KEYDOWN:
        OnKeyDown(wp);
        return 0;
    case WM_CHAR:
        if (m_modal.active || m_settingsOpen)
            return 0;
        if (wp == 8 && !m_filterText.empty())
            m_filterText.pop_back();
        else if (wp >= 32 && m_filterText.size() < 40)
            m_filterText.push_back(static_cast<wchar_t>(wp));
        else
            return 0;
        RebuildVisible();
        return 0;
    case WM_DPICHANGED:
        OnDpiChanged(HIWORD(wp), reinterpret_cast<const RECT*>(lp));
        return 0;
    case WM_SCAN_PROGRESS:
        m_progress = static_cast<int>(wp);
        m_progressTotal = static_cast<int>(lp);
        InvalidateRect(m_hwnd, &m_statusRect, FALSE);
        return 0;
    case WM_SCAN_DONE:
        m_scanning = false;
        if (lp != 0)
        {
            ApplyScanResults();
            StartUpdateCheck();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    case WM_UPDATE_DONE:
        m_checkingUpdates = false;
        KillTimer(m_hwnd, 1);
        StartBannerEase(1.0f);   // Scan fertig: Sonde faehrt ans Ende der Bahn (alles geprueft)
        m_updatesChecked = true;
        if (lp != 0)
        {
            m_updateError.clear();
            m_installableCount = UpdateChecker::Apply(m_drivers, m_updater.GetUpdates());
            m_vendorCount = ApplyVendorReleases(m_drivers, m_vendorReleases);   // Apply() hat Markierungen zurueckgesetzt
            m_updateCount = m_installableCount + m_vendorCount;
            RebuildVisible();
        }
        else
        {
            m_updateError = m_updater.GetError();
            m_installableCount = 0;
            m_updateCount = m_vendorCount;   // Hersteller-Abgleich funktioniert auch offline
        }
        if (m_autoInstall)
        {
            m_autoInstall = false;
            if (lp != 0 && m_installableCount > 0)
                StartInstall();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    case WM_INSTALL_STAGE:
        m_installStage = static_cast<int>(wp);
        InvalidateRect(m_hwnd, &m_statusRect, FALSE);
        return 0;
    case WM_INSTALL_DONE:
    {
        m_installing = false;
        KillTimer(m_hwnd, 1);
        const InstallResult r = m_installer.GetResult();

        std::wstring text;
        if (!r.error.empty())
            text = L"Die Installation wurde abgebrochen:\n" + r.error;
        else
        {
            text = std::to_wstring(r.installed) + L" Treiber installiert";
            if (r.failed > 0)
                text += L", " + std::to_wstring(r.failed) + L" fehlgeschlagen";
            if (r.skipped > 0)
                text += L", " + std::to_wstring(r.skipped) + L" übersprungen (nicht mehr verfügbar oder Lizenz nicht bestätigt)";
            text += L".";
            if (r.rebootRequired)
                text += L"\n\nEin Neustart ist erforderlich, um die Installation abzuschließen.";
        }
        text += r.restorePointCreated ? L"\n\nVorher wurde ein Wiederherstellungspunkt erstellt."
                                      : L"\n\n" + r.restorePointNote + L".";
        const bool good = r.error.empty() && r.failed == 0;
        ShowModal(good ? L"Installation abgeschlossen" : L"Installation mit Problemen", text, {}, { L"OK" }, good ? 0 : 1,
                  [this](int) { StartScan(); });   // Versionen neu einlesen
        return 0;
    }
    case WM_BACKUP_DONE:
    {
        std::unique_ptr<std::wstring> folder(reinterpret_cast<std::wstring*>(lp));
        if (wp == 0)
            ShowToast(L"Treiber gesichert in " + *folder, 0);
        else
            ShowModal(L"Sicherung fehlgeschlagen",
                      L"pnputil meldet Fehlercode " + std::to_wstring(static_cast<long>(wp)) +
                          L".\nInbox-Treiber von Windows lassen sich nicht exportieren.",
                      {}, { L"OK" }, 2, nullptr);
        return 0;
    }
    case WM_TIMER:
        if (wp == 3)
        {
            m_bannerP += (m_bannerTarget - m_bannerP) * 0.14f;
            if (std::fabs(m_bannerTarget - m_bannerP) < 0.003f)
            {
                m_bannerP = m_bannerTarget;
                KillTimer(m_hwnd, 3);
            }
            PaintRegion(m_bannerRect);
            return 0;
        }
        if (wp == 2)
        {
            if (!m_toastActive || GetTickCount() - m_toastStart > 2400)
            {
                m_toastActive = false;
                KillTimer(m_hwnd, 2);
                InvalidateRect(m_hwnd, m_toastRect.right > m_toastRect.left ? &m_toastRect : nullptr, FALSE);
            }
            else
                InvalidateRect(m_hwnd, &m_toastRect, FALSE);
            return 0;
        }
        m_marquee = (m_marquee + 2) % 100000;
        if (!Busy())
        {
            KillTimer(m_hwnd, 1);   // letzter Frame: Balken statisch gruen
            StartBannerEase(m_scannedOnce ? 1.0f : 0.62f);
        }
        {
            RECT strip{ m_statusRect.left, m_statusRect.bottom - S(50), m_statusRect.right, m_statusRect.bottom - S(4) };
            PaintRegion(strip);
        }
        return 0;
    case WM_DESTROY:
        if (m_splashHwnd)
            DestroyWindow(m_splashHwnd);
        m_scanner.Cancel();
        // Eine laufende Windows-Update-Suche laesst sich nicht abbrechen; sie nur zu beenden (Join) wuerde das
        // Schliessen lange blockieren. Reine Suche (kein Installieren) darf der Prozess hart beenden.
        if (m_updater.IsRunning() && !m_installer.IsRunning())
            ExitProcess(0);
        m_updater.Cancel();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ---- Nachrichten --------------------------------------------------------------------------

void MainWindow::OnCreate()
{
    m_dpi = GetDpiForWindow(m_hwnd);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    m_isAdmin = IsRunningAsAdmin();
    OleInitialize(nullptr);
    IconsInit();
    {
        // Dunkle Popup-Menues (undokumentierte uxtheme-Ordinale 135/136; ohne sie bleibt das Menue hell)
        if (HMODULE ux = LoadLibraryW(L"uxtheme.dll"))
        {
            using SetModeFn = int(WINAPI*)(int);
            using FlushFn = void(WINAPI*)();
            auto setMode = reinterpret_cast<SetModeFn>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))));
            auto flush = reinterpret_cast<FlushFn>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))));
            if (setMode)
                setMode(2);   // ForceDark
            if (flush)
                flush();
        }
    }
    CreateFonts();
}

// Animationen zeichnen nur ihren kleinen Bereich direkt in den Puffer und auf den Bildschirm. Ueber
// InvalidateRect wuerden Banner und Balken zu einem grossen Rechteck verschmolzen (dazwischen liegt die
// halbe Status-Karte), das dann jedes Mal komplett neu gezeichnet werden muesste.
void MainWindow::PaintRegion(const RECT& rc)
{
    if (!m_backDc || IsIconic(m_hwnd))
    {
        InvalidateRect(m_hwnd, &rc, FALSE);
        return;
    }
    HDC screen = GetDC(m_hwnd);
    SaveDC(m_backDc);
    IntersectClipRect(m_backDc, rc.left, rc.top, rc.right, rc.bottom);
    Draw(m_backDc, m_backW, m_backH);
    RestoreDC(m_backDc, -1);
    BitBlt(screen, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, m_backDc, rc.left, rc.top, SRCCOPY);
    ReleaseDC(m_hwnd, screen);
}

void MainWindow::ReleaseBannerCache()
{
    if (m_bnDc)
    {
        SelectObject(m_bnDc, m_bnOld);
        DeleteObject(m_bnBmp);
        DeleteDC(m_bnDc);
    }
    m_bnDc = nullptr;
    m_bnBmp = nullptr;
    m_bnOld = nullptr;
    m_bnW = m_bnH = 0;
}

void MainWindow::ReleaseBackBuffer()
{
    if (m_backDc)
    {
        SelectObject(m_backDc, m_backOld);
        DeleteObject(m_backBmp);
        DeleteDC(m_backDc);
    }
    m_backDc = nullptr;
    m_backBmp = nullptr;
    m_backOld = nullptr;
    m_backW = m_backH = 0;
}

void MainWindow::OnSize()
{
    ReleaseBackBuffer();
    Layout();
    SetScroll(m_scroll);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::OnDpiChanged(UINT dpi, const RECT* suggested)
{
    m_dpi = dpi;
    CreateFonts();
    BuildRows();
    SetWindowPos(m_hwnd, nullptr, suggested->left, suggested->top,
                 suggested->right - suggested->left, suggested->bottom - suggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

int MainWindow::RowAt(int clientY) const
{
    const int rel = clientY - m_listRect.top + m_scroll;
    if (rel < 0)
        return -1;
    for (size_t i = 0; i < m_rows.size(); ++i)
        if (rel >= m_rows[i].top && rel < m_rows[i].top + m_rows[i].height)
            return static_cast<int>(i);
    return -1;
}

bool MainWindow::GroupOpen(int group) const
{
    const bool autoExpand = !m_filterText.empty() || m_filterMode != 0;   // Suche/Filter klappt Treffer auf
    if (m_groupState[group] == 1)
        return true;
    if (m_groupState[group] == 2)
        return false;
    return autoExpand;
}

void MainWindow::BuildRows()
{
    // Wechsel zwischen "Standard" und "Suche/Filter aktiv": manuelle Zu-Klapp-Zustaende zuruecksetzen
    const bool autoExpand = !m_filterText.empty() || m_filterMode != 0;
    if (autoExpand != m_autoExpandPrev)
    {
        for (int& s : m_groupState)
            if (s == 2)
                s = 0;
        m_autoExpandPrev = autoExpand;
    }

    std::vector<int> members[kGroupCount];
    for (size_t v = 0; v < m_visible.size(); ++v)
        members[GroupOf(m_drivers[m_visible[v]].icon)].push_back(static_cast<int>(v));

    m_rows.clear();
    m_cardRow.assign(m_visible.size(), -1);
    const int headerH = S(56);
    int y = 0;
    for (int g = 0; g < kGroupCount; ++g)
    {
        if (members[g].empty())
            continue;
        Row header;
        header.header = true;
        header.group = g;
        header.top = y;
        header.height = headerH;
        header.count = static_cast<int>(members[g].size());
        for (int v : members[g])
        {
            const DriverStatus st = m_drivers[m_visible[v]].status;
            header.updates += st == DriverStatus::UpdateAvailable ? 1 : 0;
            header.problems += st == DriverStatus::Problem ? 1 : 0;
        }
        m_rows.push_back(header);
        y += headerH + CardGap();

        if (GroupOpen(g))
            for (int v : members[g])
            {
                Row card;
                card.group = g;
                card.visibleIdx = v;
                card.top = y;
                card.height = CardHeight();
                m_cardRow[v] = static_cast<int>(m_rows.size());
                m_rows.push_back(card);
                y += CardHeight() + CardGap();
            }
    }
    m_contentHeight = std::max(0, y - CardGap());
    m_hoverHeader = -1;
}

void MainWindow::UpdateHover(POINT pt)
{
    int hover = -1;
    int header = -1;
    if (pt.x >= m_listRect.left && pt.x < m_listRect.right - S(14) && pt.y >= m_listRect.top && pt.y < m_listRect.bottom)
    {
        const int row = RowAt(pt.y);
        if (row >= 0)
        {
            if (m_rows[row].header)
                header = row;
            else
                hover = m_rows[row].visibleIdx;
        }
    }
    int hoverCardIdx = -1;
    const int action = HitAction(pt, hoverCardIdx);
    if (hover != m_hoverCard || action != m_hoverAction || header != m_hoverHeader)
    {
        m_hoverCard = hover;
        m_hoverAction = action;
        m_hoverHeader = header;
        InvalidateRect(m_hwnd, &m_listRect, FALSE);
    }
}
RECT MainWindow::CardRect(int visibleIndex) const
{
    if (visibleIndex < 0 || visibleIndex >= static_cast<int>(m_cardRow.size()) || m_cardRow[visibleIndex] < 0)
        return RECT{};
    const int top = m_listRect.top + m_rows[m_cardRow[visibleIndex]].top - m_scroll;
    return RECT{ m_listRect.left, top, m_listRect.right - S(14), top + CardHeight() };
}

int MainWindow::HitAction(POINT pt, int& cardIdx) const
{
    cardIdx = -1;
    if (m_rows.empty() || pt.x < m_listRect.left || pt.x >= m_listRect.right - S(14) ||
        pt.y < m_listRect.top || pt.y >= m_listRect.bottom)
        return 0;
    const int row = RowAt(pt.y);
    if (row < 0 || m_rows[row].header)
        return 0;
    const int idx = m_rows[row].visibleIdx;
    const DriverInfo& info = m_drivers[m_visible[idx]];
    const RECT card = CardRect(idx);
    RECT details{}, more{}, update{}, download{};
    CardButtons(card, details, more);
    CardExtraButtons(card, info, update, download);
    cardIdx = idx;
    if (PtInRect(&details, pt))
        return 1;
    if (PtInRect(&more, pt))
        return 2;
    if (PtInRect(&update, pt))
        return 3;
    if (PtInRect(&download, pt))
        return 4;
    return 0;
}

void MainWindow::CardButtons(const RECT& card, RECT& details, RECT& more) const
{
    const int top = card.top + S(56);
    const int h = S(28);
    more = RECT{ card.right - S(16) - S(32), top, card.right - S(16), top + h };
    details = RECT{ more.left - S(8) - S(78), top, more.left - S(8), top + h };
}

void MainWindow::CardExtraButtons(const RECT& card, const DriverInfo& info, RECT& update, RECT& download) const
{
    update = RECT{};
    download = RECT{};
    if (info.status != DriverStatus::UpdateAvailable)
        return;
    RECT details{}, more{};
    CardButtons(card, details, more);
    const int top = details.top;
    const int h = details.bottom - details.top;
    int right = details.left - S(8);
    update = RECT{ right - S(118), top, right, top + h };   // direkt links neben "Details"
    right = update.left - S(8);
    if (!info.updateUrl.empty())
        download = RECT{ right - S(124), top, right, top + h };
}

void MainWindow::RunCardAction(int cardIdx, int action)
{
    if (cardIdx < 0 || cardIdx >= static_cast<int>(m_visible.size()))
        return;
    const DriverInfo& d = m_drivers[m_visible[cardIdx]];
    if (action == 1)
        ShowDetails(d);
    else if (action == 3)
        UpdateDriver(d);
    else if (action == 4)
        OpenUrl(d.updateUrl);
    else if (action == 2)
    {
        RECT details{}, more{};
        CardButtons(CardRect(cardIdx), details, more);
        ShowCardMenu(d, more);
    }
}

void MainWindow::ShowDetails(const DriverInfo& d)
{
    auto orDash = [](const std::wstring& s) { return s.empty() ? std::wstring(L"–") : s; };
    const Badge badge = BadgeFor(d);

    std::vector<ModalRow> rows;
    auto add = [&](const wchar_t* label, const std::wstring& value, bool colored = false, COLORREF color = 0)
    {
        ModalRow row;
        row.label = label;
        row.value = value;
        row.hasColor = colored;
        row.color = color;
        rows.push_back(std::move(row));
    };
    add(L"Kategorie", d.category);
    add(L"Hersteller", orDash(d.manufacturer));
    add(L"Treiber-Anbieter", orDash(d.provider));
    add(L"Version", orDash(d.driverVersion));
    add(L"Datum", orDash(d.driverDate));
    add(L"INF-Datei", orDash(d.infName));
    std::wstring status = badge.text;
    if (d.problemCode != 0)
        status += L" (Problemcode " + std::to_wstring(d.problemCode) + L")";
    add(L"Status", status, true, badge.color);
    if (d.status == DriverStatus::UpdateAvailable)
    {
        add(L"Update", d.updateTitle + L" (" + d.updateDate + L")", true, kOrange);
        add(L"Quelle", d.updateId.empty() ? L"Hersteller-Abgleich – Download und Installation über die Hersteller-Seite"
                                            : L"Windows Update – installierbar mit ""Updates installieren""");
    }

    std::wstring ids;
    const size_t shown = std::min<size_t>(d.hardwareIds.size(), 8);
    for (size_t i = 0; i < shown; ++i)
        ids += (i ? L"\n" : L"") + d.hardwareIds[i];
    if (d.hardwareIds.size() > shown)
        ids += L"\n… (+" + std::to_wstring(d.hardwareIds.size() - shown) + L" weitere)";
    add(L"Hardware-IDs", orDash(ids));

    const std::wstring id = d.hardwareId;
    const std::wstring url = d.status == DriverStatus::UpdateAvailable ? d.updateUrl : std::wstring();
    std::vector<std::wstring> buttons{ L"Hardware-ID kopieren" };
    if (!url.empty())
        buttons.push_back(L"Download-Seite öffnen");
    buttons.push_back(L"Schließen");
    const int openIndex = url.empty() ? -1 : 1;
    ShowModal(d.deviceName, L"", std::move(rows), std::move(buttons), 0, [this, id, url, openIndex](int button)
    {
        if (button == 0 && !id.empty())
            CopyToClipboard(id);
        else if (button == openIndex && button >= 0)
            OpenUrl(url);
    });
}

void MainWindow::ShowCardMenu(const DriverInfo& d, const RECT& anchor)
{
    HMENU menu = CreatePopupMenu();
    if (d.status == DriverStatus::UpdateAvailable)
    {
        AppendMenuW(menu, MF_STRING, 5, L"Treiber aktualisieren …");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, 1, L"Details anzeigen");
    AppendMenuW(menu, MF_STRING, 2, L"Im Geräte-Manager öffnen");
    AppendMenuW(menu, MF_STRING | (d.hardwareId.empty() ? MF_GRAYED : 0), 3, L"Hardware-ID kopieren");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, L"Treiber sichern …");

    POINT pt{ anchor.right, anchor.bottom + S(4) };
    ClientToScreen(m_hwnd, &pt);
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    switch (cmd)
    {
    case 1:
        ShowDetails(d);
        break;
    case 2:
        ShellExecuteW(m_hwnd, L"open", L"devmgmt.msc", nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case 3:
        CopyToClipboard(d.hardwareId);
        break;
    case 4:
        BackupDriver(d);
        break;
    case 5:
        UpdateDriver(d);
        break;
    default:
        break;
    }
}

void MainWindow::BackupDriver(const DriverInfo& d)
{
    std::wstring inf = d.infName;
    CharLowerBuffW(&inf[0], static_cast<DWORD>(inf.size()));
    // Nur "oem<Zahl>.inf": der Name geht unquotiert in die Kommandozeile von pnputil
    const bool validInf = inf.size() > 7 && inf.rfind(L"oem", 0) == 0 && inf.compare(inf.size() - 4, 4, L".inf") == 0 &&
                          std::all_of(inf.begin() + 3, inf.end() - 4, [](wchar_t c) { return c >= L'0' && c <= L'9'; });
    if (!validInf)
    {
        ShowModal(L"Treiber sichern", L"Dieser Treiber gehört zu Windows (Inbox-Treiber) und kann nicht einzeln gesichert werden.",
                  {}, { L"OK" }, 0, nullptr);
        return;
    }
    if (!m_isAdmin)
    {
        ShowModal(L"Treiber sichern", L"Zum Sichern werden Administratorrechte benötigt.", {}, { L"OK" }, 1, nullptr);
        return;
    }

    wchar_t folder[MAX_PATH] = {};
    BROWSEINFOW bi{};
    bi.hwndOwner = m_hwnd;
    bi.lpszTitle = L"Zielordner für die Treiber-Sicherung wählen";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl)
        return;
    const bool ok = SHGetPathFromIDListW(pidl, folder) != FALSE;
    CoTaskMemFree(pidl);
    if (!ok)
        return;

    std::wstring target = folder;
    while (target.size() > 3 && target.back() == L'\\')
        target.pop_back();   // ein "\" vor dem schliessenden Anfuehrungszeichen wuerde es entwerten (z. B. "D:\")
    const HWND hwnd = m_hwnd;
    const std::wstring infName = d.infName;
    std::thread([hwnd, target, infName]()
    {
        std::wstring cmd = L"pnputil.exe /export-driver " + infName + L" \"" + target + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        DWORD code = 1;
        if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            WaitForSingleObject(pi.hProcess, 120000);
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        auto* payload = new std::wstring(target);
        if (!IsWindow(hwnd) || !PostMessageW(hwnd, WM_BACKUP_DONE, static_cast<WPARAM>(code), reinterpret_cast<LPARAM>(payload)))
            delete payload;
    }).detach();
}

void MainWindow::OnMouseMove(int x, int y)
{
    if (m_modal.active)
    {
        OnModalMouseMove(x, y);
        return;
    }
    if (!m_trackingMouse)
    {
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hwnd, 0 };
        TrackMouseEvent(&tme);
        m_trackingMouse = true;
    }
    if (m_settingsOpen)
    {
        SettingsMouse(x, y, false);
        return;
    }
    {
        const bool gear = PtInRect(&m_gearRect, POINT{ x, y }) != FALSE;
        if (gear != m_gearHover)
        {
            m_gearHover = gear;
            InvalidateRect(m_hwnd, &m_gearRect, FALSE);
        }
    }
    if (m_dragThumb)
    {
        const RECT thumb = ScrollThumbRect();
        const int travel = (m_listRect.bottom - m_listRect.top) - (thumb.bottom - thumb.top);
        if (travel > 0)
            SetScroll((y - m_dragOffset - m_listRect.top) * MaxScroll() / travel);
        return;
    }
    UpdateHover(POINT{ x, y });
    {
        const bool about = PtInRect(&m_aboutRect, POINT{ x, y }) != FALSE;
        if (about != m_aboutHover)
        {
            m_aboutHover = about;
            InvalidateRect(m_hwnd, &m_aboutRect, FALSE);
        }
    }
    if (!m_trackingMouse)
    {
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hwnd, 0 };
        TrackMouseEvent(&tme);
        m_trackingMouse = true;
    }
    POINT pt{ x, y };
    bool hover = PtInRect(&m_buttonRect, pt) != FALSE;
    if (hover != m_buttonHover)
    {
        m_buttonHover = hover;
        InvalidateRect(m_hwnd, &m_buttonRect, FALSE);
    }
    bool installHover = ShowInstallButton() && PtInRect(&m_installRect, pt) != FALSE;
    if (installHover != m_installHover)
    {
        m_installHover = installHover;
        InvalidateRect(m_hwnd, &m_installRect, FALSE);
    }
}

void MainWindow::OnLButtonUp()
{
    if (m_modal.active)
    {
        POINT cur{};
        GetCursorPos(&cur);
        ScreenToClient(m_hwnd, &cur);
        OnModalMouse(cur.x, cur.y, false);
        return;
    }
    if (m_settingsOpen)
    {
        POINT cur{};
        GetCursorPos(&cur);
        ScreenToClient(m_hwnd, &cur);
        const int pressed = m_setPress;
        m_setPress = 0;
        SettingsMouse(cur.x, cur.y, false);
        if (pressed != 0 && pressed == m_setHover)
        {
            if (pressed == 1)
                m_draft.autoScan = !m_draft.autoScan;
            else if (pressed == 2)
                m_draft.splash = !m_draft.splash;
            else
                CloseSettings(pressed == 3);
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (m_gearPress)
    {
        m_gearPress = false;
        POINT cur{};
        GetCursorPos(&cur);
        ScreenToClient(m_hwnd, &cur);
        if (PtInRect(&m_gearRect, cur))
            OpenSettings();
        return;
    }
    if (m_pressAction != 0)
    {
        const int pressCard = m_pressCard;
        const int pressAction = m_pressAction;
        m_pressCard = -1;
        m_pressAction = 0;
        ReleaseCapture();
        InvalidateRect(m_hwnd, &m_listRect, FALSE);

        POINT cur{};
        GetCursorPos(&cur);
        ScreenToClient(m_hwnd, &cur);
        int hitCard = -1;
        if (HitAction(cur, hitCard) == pressAction && hitCard == pressCard)
            RunCardAction(pressCard, pressAction);
        return;
    }
    if (m_dragThumb)
    {
        m_dragThumb = false;
        ReleaseCapture();
        InvalidateRect(m_hwnd, &m_listRect, FALSE);
    }
}

RECT MainWindow::ScrollThumbRect() const
{
    const int maxScroll = MaxScroll();
    if (maxScroll <= 0)
        return RECT{};
    const int viewH = m_listRect.bottom - m_listRect.top;
    const int contentH = viewH + maxScroll;
    const int thumbH = std::max(S(32), viewH * viewH / contentH);
    const int travel = viewH - thumbH;
    const int thumbTop = m_listRect.top + travel * m_scroll / maxScroll;
    return RECT{ m_listRect.right - S(9), thumbTop, m_listRect.right - S(1), thumbTop + thumbH };
}

void MainWindow::OnLButtonDown(int x, int y)
{
    if (m_modal.active)
    {
        OnModalMouse(x, y, true);
        return;
    }
    if (m_settingsOpen)
    {
        SettingsMouse(x, y, true);
        m_setPress = m_setHover;
        return;
    }
    POINT pt{ x, y };
    if (PtInRect(&m_gearRect, pt))
    {
        m_gearPress = true;
        return;
    }
    if (PtInRect(&m_aboutRect, pt))
    {
        ShowAbout();
        return;
    }

    // Scrollbalken: Thumb ziehen, in die Leiste klicken = seitenweise scrollen
    if (MaxScroll() > 0 && x >= m_listRect.right - S(14) && x < m_listRect.right + S(2) &&
        y >= m_listRect.top && y < m_listRect.bottom)
    {
        const RECT thumb = ScrollThumbRect();
        if (y >= thumb.top && y < thumb.bottom)
        {
            m_dragThumb = true;
            m_dragOffset = y - thumb.top;
            SetCapture(m_hwnd);
            InvalidateRect(m_hwnd, &m_listRect, FALSE);
        }
        else
        {
            const int page = m_listRect.bottom - m_listRect.top;
            SetScroll(m_scroll + (y < thumb.top ? -page : page));
        }
        return;
    }

    if (x >= m_listRect.left && x < m_listRect.right - S(14) && y >= m_listRect.top && y < m_listRect.bottom)
    {
        const int row = RowAt(y);
        if (row >= 0 && m_rows[row].header)
        {
            const int g = m_rows[row].group;
            m_groupState[g] = GroupOpen(g) ? 2 : 1;
            BuildRows();
            SetScroll(m_scroll);
            POINT cursor{ x, y };
            UpdateHover(cursor);
            InvalidateRect(m_hwnd, &m_listRect, FALSE);
            return;
        }
    }

    {
        int cardIdx = -1;
        const int action = HitAction(pt, cardIdx);
        if (action != 0)
        {
            m_pressCard = cardIdx;
            m_pressAction = action;
            SetCapture(m_hwnd);
            InvalidateRect(m_hwnd, &m_listRect, FALSE);
            return;
        }
    }

    if (PtInRect(&m_sortRect, pt))
    {
        m_sortMode = (m_sortMode + 1) % 4;
        RebuildVisible();
    }
    else if (PtInRect(&m_exportRect, pt))
        ExportCsv();
    else if (PtInRect(&m_buttonRect, pt))
        StartScan();
    else if (ShowInstallButton() && PtInRect(&m_installRect, pt))
        OnInstallClicked();
    else
        for (int i = 0; i < 3; ++i)
            if (PtInRect(&m_chipRect[i], pt) && m_filterMode != i)
            {
                m_filterMode = i;
                RebuildVisible();
            }
}

void MainWindow::OnMouseWheel(int delta)
{
    if (m_modal.active)
    {
        m_modal.scroll = std::clamp(m_modal.scroll - delta * S(40) / WHEEL_DELTA, 0, std::max(0, m_modal.contentH - m_modal.viewH));
        InvalidateRect(m_hwnd, &m_modal.box, FALSE);
        return;
    }
    SetScroll(m_scroll - delta * S(60) / WHEEL_DELTA);
}

void MainWindow::OnKeyDown(WPARAM key)
{
    if (m_modal.active)
    {
        OnModalKey(key);
        return;
    }
    if (m_settingsOpen)
    {
        if (key == VK_ESCAPE || key == VK_RETURN)
            CloseSettings(key == VK_RETURN);
        return;
    }
    const int page = m_listRect.bottom - m_listRect.top;
    switch (key)
    {
    case VK_F5:     StartScan(); break;
    case VK_ESCAPE: m_filterText.clear(); RebuildVisible(); break;
    case VK_UP:     SetScroll(m_scroll - S(60)); break;
    case VK_DOWN:   SetScroll(m_scroll + S(60)); break;
    case VK_PRIOR:  SetScroll(m_scroll - page); break;
    case VK_NEXT:   SetScroll(m_scroll + page); break;
    case VK_HOME:   SetScroll(0); break;
    case VK_END:    SetScroll(MaxScroll()); break;
    default: break;
    }
}

// ---- Logik --------------------------------------------------------------------------------

void MainWindow::StartScan()
{
    if (Busy())
        return;

    m_scanning = true;
    m_progress = 0;
    m_progressTotal = 0;
    m_scroll = 0;
    if (m_scanner.StartAsync(m_hwnd))
        SetTimer(m_hwnd, 1, 40, nullptr);   // Animation des Glow-Balkens
    else
        m_scanning = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::ApplyScanResults()
{
    m_drivers = m_scanner.GetResults();
    m_vendorReleases = LoadVendorReleases();
    m_vendorCount = ApplyVendorReleases(m_drivers, m_vendorReleases);
    RebuildVisible();
    m_scannedOnce = true;
    m_lastScanTime = CurrentTimeText();
    m_scroll = 0;
    m_updatesChecked = false;
    m_updateCount = m_vendorCount;
    m_installableCount = 0;
    m_updateError.clear();
}

void MainWindow::OnInstallClicked()
{
    ConfirmInstall({}, m_installableCount);
}

void MainWindow::ConfirmInstall(std::vector<std::wstring> ids, int count)
{
    std::wstring question = count == 1 ? std::wstring(L"Es wird 1 Treiber-Update über Windows Update installiert.\n\n")
                                       : L"Es werden " + std::to_wstring(count) + L" Treiber-Updates über Windows Update installiert.\n\n";
    question += L"Vorher wird ein Systemwiederherstellungspunkt erstellt.";
    if (!m_isAdmin)
        question += L"\n\nDafür wird die App mit Administratorrechten neu gestartet (Windows fragt nach Bestätigung).";

    ShowModal(L"Updates installieren", question, {}, { L"Abbrechen", L"Installieren" }, 3, [this, ids](int button)
    {
        if (button != 1)
            return;
        if (m_isAdmin)
            StartInstall(ids);
        else if (!RelaunchElevated())
            ShowToast(L"Neustart mit Administratorrechten abgebrochen", 1);
    });
}

void MainWindow::ShowAbout()
{
    std::vector<ModalRow> rows;
    rows.push_back({ L"Programm", L"Duncan's Driver Updater", 0, false });
    rows.push_back({ L"Version", std::wstring(L"v") + APP_VERSION, 0, false });
    rows.push_back({ L"Entwickler", L"Duncan  ·  © 2026", 0, false });
    rows.push_back({ L"Teil von", L"Duncan's Suite (Driver Updater, PDF Reader, Tidy)", 0, false });
    rows.push_back({ L"Quellcode", L"github.com/Neox365/Duncans-Suite", 0, false });
    ShowModal(L"Über Duncan's Driver Updater", L"", std::move(rows), { L"GitHub öffnen", L"Schließen" }, 0,
              [this](int button)
              {
                  if (button == 0)
                      OpenUrl(L"https://github.com/Neox365/Duncans-Suite");
              });
}

void MainWindow::OpenUrl(const std::wstring& url)
{
    // Die URL kommt u. a. aus vendor_releases.json: nur https-Webadressen oeffnen, keine Dateipfade/Programme
    if (url.rfind(L"https://", 0) == 0)
        ShellExecuteW(m_hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void MainWindow::UpdateDriver(const DriverInfo& d)
{
    if (!d.updateId.empty())
    {
        ConfirmInstall({ d.updateId }, 1);   // Windows-Update-Treiber: direkt installierbar
        return;
    }
    if (!d.updateUrl.empty())
    {
        OpenUrl(d.updateUrl);                // Hersteller-Treiber: offizielle Download-Seite im Standardbrowser
        ShowToast(L"Download-Seite geöffnet – Treiber laden und installieren", 0);
        return;
    }
    ShowToast(L"Für dieses Gerät ist keine Update-Quelle hinterlegt", 1);
}

bool MainWindow::RelaunchElevated()
{
    wchar_t path[MAX_PATH * 2] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH * 2) == 0)
        return false;

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC;
    sei.hwnd = m_hwnd;
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.lpParameters = L"/install";
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei))
        return false;   // z. B. UAC abgelehnt (ERROR_CANCELLED)

    DestroyWindow(m_hwnd);   // die erhoehte Instanz uebernimmt: Scan -> Suche -> Installation
    return true;
}

void MainWindow::StartInstall(std::vector<std::wstring> ids)
{
    if (ids.empty())
        for (const auto& d : m_drivers)
            if (d.status == DriverStatus::UpdateAvailable && !d.updateId.empty() &&
                std::find(ids.begin(), ids.end(), d.updateId) == ids.end())
                ids.push_back(d.updateId);
    if (ids.empty())
        return;

    m_installStage = StageRestorePoint;
    m_installing = m_installer.StartAsync(m_hwnd, std::move(ids));
    if (m_installing)
    {
        m_marquee = 0;
        SetTimer(m_hwnd, 1, 40, nullptr);
    }
    m_installHover = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::ExportCsv()
{
    if (m_visible.empty())
        return;

    wchar_t file[MAX_PATH] = L"Treiber.csv";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"CSV-Datei (*.csv)\0*.csv\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn))
        return;

    // Excel-tauglich: Semikolon, UTF-8 mit BOM, Felder in Anfuehrungszeichen (Formel-Zeichen entschaerft)
    auto field = [](std::wstring s)
    {
        if (!s.empty() && (s[0] == L'=' || s[0] == L'+' || s[0] == L'-' || s[0] == L'@'))
            s.insert(s.begin(), L'\'');
        std::wstring out = L"\"";
        for (wchar_t c : s)
        {
            if (c == L'"')
                out += L'"';
            out += c;
        }
        return out + L"\"";
    };

    std::wstring csv = L"Gerät;Kategorie;Hersteller;Version;Datum;Anbieter;Hardware-ID;Status;Update\r\n";
    for (size_t index : m_visible)
    {
        const DriverInfo& d = m_drivers[index];
        csv += field(d.deviceName) + L';' + field(d.category) + L';' + field(d.manufacturer) + L';' +
               field(d.driverVersion) + L';' + field(d.driverDate) + L';' + field(d.provider) + L';' +
               field(d.hardwareId) + L';' + field(BadgeFor(d).text) + L';' +
               field(d.status == DriverStatus::UpdateAvailable ? d.updateTitle + L" (" + d.updateDate + L")" : std::wstring()) +
               L"\r\n";
    }

    const int bytes = WideCharToMultiByte(CP_UTF8, 0, csv.c_str(), static_cast<int>(csv.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, csv.c_str(), static_cast<int>(csv.size()), &utf8[0], bytes, nullptr, nullptr);

    HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool ok = h != INVALID_HANDLE_VALUE;
    if (ok)
    {
        const char bom[3] = { '\xEF', '\xBB', '\xBF' };
        DWORD written = 0;
        ok = WriteFile(h, bom, 3, &written, nullptr) &&
             WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        CloseHandle(h);
    }
    if (ok)
        ShowToast(std::to_wstring(m_visible.size()) + L" Einträge als CSV exportiert", 0);
    else
        ShowModal(L"CSV-Export", L"Die Datei konnte nicht geschrieben werden.", {}, { L"OK" }, 2, nullptr);
}

void MainWindow::StartUpdateCheck()
{
    m_checkingUpdates = m_updater.StartAsync(m_hwnd);
    if (m_checkingUpdates || m_installing)
    {
        m_marquee = 0;
        SetTimer(m_hwnd, 1, 40, nullptr);
    }
}

void MainWindow::RebuildVisible()
{
    auto lower = [](std::wstring s)
    {
        if (!s.empty())
            CharLowerBuffW(&s[0], static_cast<DWORD>(s.size()));
        return s;
    };
    const std::wstring needle = lower(m_filterText);

    m_visible.clear();
    for (size_t i = 0; i < m_drivers.size(); ++i)
    {
        const DriverInfo& d = m_drivers[i];
        if (m_filterMode == 1 && d.status != DriverStatus::UpdateAvailable)
            continue;
        if (m_filterMode == 2 && d.status != DriverStatus::Problem)
            continue;
        if (!needle.empty())
        {
            const std::wstring hay = lower(d.deviceName + L"\n" + d.category + L"\n" + d.manufacturer + L"\n" +
                                           d.provider + L"\n" + d.driverVersion + L"\n" + d.hardwareId);
            if (hay.find(needle) == std::wstring::npos)
                continue;
        }
        m_visible.push_back(i);
    }

    // Basisreihenfolge ist (Kategorie, Name) aus dem Scan; stable_sort behaelt sie als Zweitkriterium
    auto statusRank = [](DriverStatus s)
    {
        switch (s)
        {
        case DriverStatus::Problem:         return 0;
        case DriverStatus::UpdateAvailable: return 1;
        case DriverStatus::Ok:              return 2;
        default:                            return 3;
        }
    };
    if (m_sortMode == 1)
        std::stable_sort(m_visible.begin(), m_visible.end(), [&](size_t a, size_t b)
        {
            return CompareStringOrdinal(m_drivers[a].deviceName.c_str(), -1, m_drivers[b].deviceName.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        });
    else if (m_sortMode == 2)
        std::stable_sort(m_visible.begin(), m_visible.end(), [&](size_t a, size_t b)
        {
            return m_drivers[a].driverDateKey > m_drivers[b].driverDateKey;
        });
    else if (m_sortMode == 3)
        std::stable_sort(m_visible.begin(), m_visible.end(), [&](size_t a, size_t b)
        {
            return statusRank(m_drivers[a].status) < statusRank(m_drivers[b].status);
        });
    BuildRows();
    m_scroll = 0;
    m_hoverCard = -1;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::DrawFilterBar(HDC dc)
{
    static const wchar_t* const kLabels[3] = { L"Alle", L"Updates", L"Probleme" };
    for (int i = 0; i < 3; ++i)
    {
        const bool active = m_filterMode == i;
        FillRound(dc, m_chipRect[i], S(32), active ? Blend(kCard, kAccent, 25) : kCard,
                  active ? kAccent : kCardBorder);
        DrawLabel(dc, m_fontSmall, active ? kAccent : kTextDim,
                  { m_chipRect[i].left, m_chipRect[i].top + S(7), m_chipRect[i].right, m_chipRect[i].bottom },
                  kLabels[i], DT_CENTER);
    }

    FillRound(dc, m_searchRect, S(32), kCard, m_filterText.empty() ? kCardBorder : kAccent);
    const int iconSize = S(16);
    const int iconTop = m_searchRect.top + (m_searchRect.bottom - m_searchRect.top - iconSize) / 2;
    DrawSearchIcon(dc, { m_searchRect.left + S(14), iconTop, m_searchRect.left + S(14) + iconSize, iconTop + iconSize },
                   m_filterText.empty() ? kTextDim : kAccent);
    DrawLabel(dc, m_fontSmall, m_filterText.empty() ? kTextDim : kText,
              { m_searchRect.left + S(14) + iconSize + S(8), m_searchRect.top + S(7), m_searchRect.right - S(14), m_searchRect.bottom },
              m_filterText.empty() ? std::wstring(L"Tippen zum Suchen …") : m_filterText);
}

// ---- Layout -------------------------------------------------------------------------------

void MainWindow::Layout()
{
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const int m = S(24);

    m_bannerRect = { m, S(12), rc.right - m, S(12) + S(120) };
    m_gearRect = { m_bannerRect.right - S(22) - S(42), m_bannerRect.top + S(18), m_bannerRect.right - S(22), m_bannerRect.top + S(18) + S(42) };
    m_buttonRect = { m_gearRect.left - S(12) - S(170), m_bannerRect.top + S(18), m_gearRect.left - S(12), m_bannerRect.top + S(18) + S(42) };
    m_installRect = { m_buttonRect.left - S(12) - S(230), m_buttonRect.top, m_buttonRect.left - S(12), m_buttonRect.bottom };
    m_statusRect = { m, m_bannerRect.bottom + S(14), rc.right - m, m_bannerRect.bottom + S(14) + S(168) };
    m_listTitleRect = { m, m_statusRect.bottom + S(22), rc.right - m, m_statusRect.bottom + S(22) + S(32) };
    {
        const int pad = S(22);
        const int top = m_statusRect.top + S(12);
        m_exportRect = { m_statusRect.right - pad - S(170), top, m_statusRect.right - pad, top + S(30) };
        m_sortRect = { m_exportRect.left - S(10) - S(250), top, m_exportRect.left - S(10), top + S(30) };
    }
    m_searchRect = { rc.right - m - S(240), m_listTitleRect.top, rc.right - m, m_listTitleRect.bottom };
    {
        const int widths[3] = { S(64), S(92), S(100) };
        int right = m_searchRect.left - S(12);
        for (int i = 2; i >= 0; --i)
        {
            m_chipRect[i] = { right - widths[i], m_listTitleRect.top, right, m_listTitleRect.bottom };
            right -= widths[i] + S(8);
        }
    }
    m_listRect = { m, m_listTitleRect.bottom + S(8), rc.right - m, std::max<LONG>(rc.bottom - m, m_listTitleRect.bottom + S(8)) };
    m_aboutRect = { m, rc.bottom - S(22), m + S(200), rc.bottom - S(4) };   // im unteren Rand unter der Liste
}

int MainWindow::CardHeight() const { return S(100); }
int MainWindow::CardGap() const { return S(10); }

int MainWindow::MaxScroll() const
{
    const int content = m_rows.empty() ? CardHeight() : m_contentHeight;
    const int view = m_listRect.bottom - m_listRect.top;
    return std::max(0, content - view);
}

void MainWindow::SetScroll(int value)
{
    value = std::clamp(value, 0, MaxScroll());
    if (value != m_scroll)
    {
        m_scroll = value;
        InvalidateRect(m_hwnd, &m_listRect, FALSE);
        POINT cursor{};
        if (GetCursorPos(&cursor) && ScreenToClient(m_hwnd, &cursor))
            UpdateHover(cursor);
    }
}

// ---- Zeichnen -----------------------------------------------------------------------------

void MainWindow::OnPaint()
{
    PAINTSTRUCT ps;
    HDC screen = BeginPaint(m_hwnd, &ps);

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const int w = rc.right, h = rc.bottom;
    if (w > 0 && h > 0)
    {
        RECT paint = ps.rcPaint;
        if (!m_backDc || m_backW != w || m_backH != h)
        {
            ReleaseBackBuffer();
            m_backDc = CreateCompatibleDC(screen);
            m_backBmp = CreateCompatibleBitmap(screen, w, h);
            m_backOld = SelectObject(m_backDc, m_backBmp);
            m_backW = w;
            m_backH = h;
            paint = rc;   // neuer Puffer ist leer: komplett zeichnen
        }

        // Nur den ungueltigen Bereich neu aufbauen (Banner-/Balken-Animation = kleine Bereiche)
        SaveDC(m_backDc);
        IntersectClipRect(m_backDc, paint.left, paint.top, paint.right, paint.bottom);
        Draw(m_backDc, w, h);
        RestoreDC(m_backDc, -1);
        BitBlt(screen, paint.left, paint.top, paint.right - paint.left, paint.bottom - paint.top, m_backDc, paint.left, paint.top, SRCCOPY);
    }
    EndPaint(m_hwnd, &ps);
}
void MainWindow::Draw(HDC dc, int width, int height)
{
    RECT all{ 0, 0, width, height };
    HBRUSH bg = CreateSolidBrush(kBackground);
    FillRect(dc, &all, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);

    // Banner mit Titel und Platinen-Szene
    if (RectVisible(dc, &m_bannerRect))
        DrawBanner(dc);

    // Install-Button (nur sichtbar, wenn Updates gefunden wurden)
    if (ShowInstallButton())
    {
        COLORREF fill = m_installHover ? Blend(kGreen, RGB(255, 255, 255), 12) : kGreen;
        FillRound(dc, m_installRect, S(12), fill, fill);
        DrawLabel(dc, m_fontHeading, RGB(6, 30, 18),
                  { m_installRect.left, m_installRect.top + S(10), m_installRect.right, m_installRect.bottom },
                  std::to_wstring(m_installableCount) + L" Update(s) installieren", DT_CENTER);
    }

    // Scan-Button
    {
        COLORREF fill = Busy() ? kDisabled : (m_buttonHover ? Blend(kAccent, RGB(255, 255, 255), 12) : kAccent);
        FillRound(dc, m_buttonRect, S(12), fill, fill);
        DrawLabel(dc, m_fontHeading, Busy() ? kTextDim : RGB(8, 20, 36),
                  { m_buttonRect.left, m_buttonRect.top + S(10), m_buttonRect.right, m_buttonRect.bottom },
                  m_scanning ? L"Scan läuft …" : (m_installing ? L"Installiere …" : (m_checkingUpdates ? L"Suche Updates …" : L"Scan starten")), DT_CENTER);
    }

    // Zahnrad
    {
        const bool hover = m_gearHover && !m_modal.active;
        FillRound(dc, m_gearRect, S(12), hover ? kCardHover : kCardRaised, hover ? kCardBorderHover : kCardBorder);
        HGDIOBJ old = SelectObject(dc, m_fontSym);
        SetTextColor(dc, hover ? kText : kTextDim);
        RECT g = m_gearRect;
        DrawTextW(dc, L"⚙", 1, &g, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, old);
    }

    if (RectVisible(dc, &m_statusRect))
        DrawStatusCard(dc);

    // Listenueberschrift
    std::wstring title = L"Gefundene Treiber";
    if (m_scannedOnce)
        title += L"  (" + (m_visible.size() == m_drivers.size() ? std::to_wstring(m_drivers.size())
                                                                : std::to_wstring(m_visible.size()) + L" von " + std::to_wstring(m_drivers.size())) + L")";
    DrawLabel(dc, m_fontHeading, kText, { m_listTitleRect.left, m_listTitleRect.top + S(4), m_chipRect[0].left - S(12), m_listTitleRect.bottom }, title);
    if (RectVisible(dc, &m_listTitleRect))
        DrawFilterBar(dc);

    // Karten (geclippt)
    SaveDC(dc);
    IntersectClipRect(dc, m_listRect.left, m_listRect.top, m_listRect.right, m_listRect.bottom);

    const int cardRight = m_listRect.right - S(14);
    if (m_visible.empty())
    {
        DriverInfo placeholder;
        placeholder.category = L"System";
        if (m_scanning)
        {
            placeholder.deviceName = L"Scan läuft …";
            placeholder.manufacturer = L"Geräte und Treiber werden ausgelesen";
        }
        else if (m_scannedOnce && !m_drivers.empty())
        {
            placeholder.deviceName = L"Keine Treffer";
            placeholder.manufacturer = L"Passe Suche oder Filter an (Esc löscht die Suche)";
        }
        else if (m_scannedOnce)
        {
            placeholder.deviceName = L"Keine Treiber gefunden";
            placeholder.manufacturer = L"Starte den Scan erneut";
        }
        else
        {
            placeholder.deviceName = L"Noch kein Scan durchgeführt";
            placeholder.manufacturer = L"Klicke auf „Scan starten“, um alle Geräte zu erfassen";
        }
        RECT card{ m_listRect.left, m_listRect.top, cardRight, m_listRect.top + CardHeight() };
        DrawDriverCard(dc, card, placeholder, false);
    }
    else
    {
        for (size_t r = 0; r < m_rows.size(); ++r)
        {
            const Row& row = m_rows[r];
            const int top = m_listRect.top + row.top - m_scroll;
            if (top + row.height < m_listRect.top)
                continue;
            if (top > m_listRect.bottom)
                break;
            RECT rc{ m_listRect.left, top, cardRight, top + row.height };
            if (!RectVisible(dc, &rc))
                continue;
            if (row.header)
                DrawGroupHeader(dc, rc, static_cast<int>(r), static_cast<int>(r) == m_hoverHeader);
            else
                DrawDriverCard(dc, rc, m_drivers[m_visible[row.visibleIdx]], row.visibleIdx == m_hoverCard);
        }
    }
    DrawScrollbar(dc);
    RestoreDC(dc, -1);

    // Fusszeile: Urheber und Version, Klick oeffnet "Ueber"
    DrawLabel(dc, m_fontSmall, m_aboutHover ? kText : kTextDim, m_aboutRect, std::wstring(L"© 2026 Duncan · v") + APP_VERSION);

    if (m_settingsOpen)
        DrawSettings(dc, width, height);
    if (m_modal.active)
        DrawModal(dc, width, height);
    DrawToast(dc, width, height);
}

void MainWindow::OpenSettings()
{
    m_draft = m_settings;
    m_settingsOpen = true;
    m_setHover = m_setPress = 0;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::CloseSettings(bool save)
{
    m_settingsOpen = false;
    m_setHover = m_setPress = 0;
    if (save)
    {
        m_settings = m_draft;
        if (WriteFlag(L"AutoScan", m_settings.autoScan) && WriteFlag(L"StartAnimation", m_settings.splash))
            ShowToast(L"Einstellungen gespeichert");
        else
            ShowToast(L"Einstellungen konnten nicht gespeichert werden", 2);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::SettingsMouse(int x, int y, bool)
{
    const POINT pt{ x, y };
    int hover = 0;
    if (PtInRect(&m_chkScan, pt))
        hover = 1;
    else if (PtInRect(&m_chkSplash, pt))
        hover = 2;
    else if (PtInRect(&m_btnSave, pt))
        hover = 3;
    else if (PtInRect(&m_btnCancel, pt))
        hover = 4;
    if (hover != m_setHover)
    {
        m_setHover = hover;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void MainWindow::DrawSettings(HDC dc, int width, int height)
{
    FillAlphaRect(dc, RECT{ 0, 0, width, height }, RGB(0, 0, 0), 150);

    const int pad = S(26);
    const int boxW = std::min(S(560), width - S(40));
    const int titleH = S(30), btnH = S(38), rowH = S(50);
    const int boxH = pad + titleH + S(18) + rowH + S(10) + rowH + S(26) + btnH + pad;
    RECT box{ (width - boxW) / 2, (height - boxH) / 2, (width - boxW) / 2 + boxW, (height - boxH) / 2 + boxH };

    DrawShadow(dc, box, S(18), static_cast<int>(m_dpi));
    DrawPanelAA(dc, box, S(18), RGB(27, 29, 36), kCardBorder);
    DrawLabel(dc, m_fontHeading, kText, { box.left + pad, box.top + pad + S(3), box.right - pad, box.top + pad + titleH }, L"Einstellungen");

    auto check = [&](const RECT& r, bool on, bool hover, const wchar_t* label, const wchar_t* hint)
    {
        const int sz = S(22);
        RECT b{ r.left, r.top + S(2), r.left + sz, r.top + S(2) + sz };
        FillRound(dc, b, S(7), on ? kAccent : kCardRaised, on ? kAccent : (hover ? kCardBorderHover : kCardBorder));
        if (on)
        {
            HPEN pen = CreatePen(PS_SOLID, std::max(2, S(2)), RGB(8, 20, 36));
            HGDIOBJ old = SelectObject(dc, pen);
            MoveToEx(dc, b.left + S(5), b.top + S(11), nullptr);
            LineTo(dc, b.left + S(9), b.top + S(15));
            LineTo(dc, b.left + S(17), b.top + S(7));
            SelectObject(dc, old);
            DeleteObject(pen);
        }
        DrawLabel(dc, m_fontBody, kText, { r.left + sz + S(14), r.top, r.right, r.top + S(24) }, label);
        DrawLabel(dc, m_fontSmall, kTextDim, { r.left + sz + S(14), r.top + S(24), r.right, r.bottom }, hint);
    };

    int y = box.top + pad + titleH + S(18);
    m_chkScan = { box.left + pad, y, box.right - pad, y + rowH };
    check(m_chkScan, m_draft.autoScan, m_setHover == 1, L"Beim Start automatisch scannen", L"Startet den Geräte-Scan, sobald das Programm geöffnet ist.");
    y += rowH + S(10);
    m_chkSplash = { box.left + pad, y, box.right - pad, y + rowH };
    check(m_chkSplash, m_draft.splash, m_setHover == 2, L"Start-Animation anzeigen", L"Zeigt beim Öffnen den animierten Startbildschirm.");

    auto button = [&](const RECT& r, bool primary, int id, const wchar_t* text)
    {
        const bool hover = m_setHover == id;
        const bool pressed = m_setPress == id && hover;
        COLORREF fill, border;
        if (primary)
        {
            fill = pressed ? Blend(kAccent, RGB(0, 0, 0), 25) : (hover ? Blend(kAccent, RGB(255, 255, 255), 14) : kAccent);
            border = fill;
        }
        else
        {
            fill = pressed ? RGB(24, 26, 32) : (hover ? kCardHover : kCardRaised);
            border = hover ? kCardBorderHover : kCardBorder;
        }
        FillRound(dc, r, S(12), fill, border);
        DrawLabel(dc, m_fontBody, primary ? RGB(8, 20, 36) : kText, { r.left, r.top + S(8), r.right, r.bottom }, text, DT_CENTER);
    };
    const int bw = S(120);
    m_btnSave = { box.right - pad - bw, box.bottom - pad - btnH, box.right - pad, box.bottom - pad };
    m_btnCancel = { m_btnSave.left - S(10) - bw, m_btnSave.top, m_btnSave.left - S(10), m_btnSave.bottom };
    button(m_btnCancel, false, 4, L"Abbrechen");
    button(m_btnSave, true, 3, L"Speichern");
}

void MainWindow::DrawStatusCard(HDC dc)
{
    const RECT& r = m_statusRect;
    // Animation (Balken) invalidiert nur den unteren Streifen: dann Schatten, Titel und Kennzahlen auslassen
    RECT clip{};
    GetClipBox(dc, &clip);
    const bool strip = clip.left >= r.left && clip.right <= r.right && clip.top >= r.bottom - S(52) && clip.bottom <= r.bottom;
    if (!strip)
        DrawShadow(dc, r, S(16), static_cast<int>(m_dpi));
    FillRound(dc, r, S(16), kCard, kCardBorder);

    const int pad = S(22);
    if (!strip)
    {
    DrawLabel(dc, m_fontHeading, kText, { r.left + pad, r.top + S(16), r.right - pad, r.top + S(40) }, L"System Status");

    {
        static const wchar_t* const kSortNames[4] = { L"Kategorie", L"Name", L"Datum (neueste zuerst)", L"Status" };
        FillRound(dc, m_sortRect, S(30), kBackground, kCardBorder);
        DrawLabel(dc, m_fontSmall, kTextDim, { m_sortRect.left, m_sortRect.top + S(6), m_sortRect.right, m_sortRect.bottom },
                  std::wstring(L"Sortierung: ") + kSortNames[m_sortMode], DT_CENTER);
        const bool canExport = !m_visible.empty();
        FillRound(dc, m_exportRect, S(30), kBackground, canExport ? kCardBorder : kBackground);
        DrawLabel(dc, m_fontSmall, canExport ? kTextDim : kDisabled,
                  { m_exportRect.left, m_exportRect.top + S(6), m_exportRect.right, m_exportRect.bottom },
                  L"Als CSV exportieren", DT_CENTER);
    }

    // Kennzahlen
    std::set<std::wstring> categories;
    int problems = 0;
    for (const auto& d : m_drivers)
    {
        categories.insert(d.category);
        if (d.status == DriverStatus::Problem)
            ++problems;
    }

    struct Stat { std::wstring value; std::wstring label; COLORREF color; };
    const bool known = m_scannedOnce;
    const Stat stats[] = {
        { known ? std::to_wstring(m_drivers.size()) : L"–",    L"Geräte mit Treiber", kText },
        { known ? std::to_wstring(categories.size()) : L"–",   L"Kategorien",         kText },
        { known ? std::to_wstring(problems) : L"–",            L"Probleme",           problems > 0 ? kRed : kGreen },
        { (m_updatesChecked || m_vendorCount > 0) ? std::to_wstring(m_updateCount) : (m_checkingUpdates ? L"…" : L"–"),
                                                               L"Updates",            m_updateCount > 0 ? kAccent : kText },
        { m_isAdmin ? L"Ja" : L"Nein",                         L"Administrator",      m_isAdmin ? kGreen : kAmber },
    };

    const int innerW = (r.right - r.left) - 2 * pad;
    const int colW = innerW / 5;
    for (int i = 0; i < 5; ++i)
    {
        const int x = r.left + pad + i * colW;
        if (i > 0)
        {
            // dezenter vertikaler Divider zwischen den Kennzahlen
            HPEN pen = CreatePen(PS_SOLID, 1, kCardBorder);
            HGDIOBJ old = SelectObject(dc, pen);
            MoveToEx(dc, x - S(14), r.top + S(50), nullptr);
            LineTo(dc, x - S(14), r.top + S(104));
            SelectObject(dc, old);
            DeleteObject(pen);
        }
        DrawLabel(dc, m_fontBig, stats[i].color, { x, r.top + S(46), x + colW - S(8), r.top + S(86) }, stats[i].value);
        DrawLabel(dc, m_fontSmall, kTextDim, { x, r.top + S(86), x + colW - S(8), r.top + S(106) }, stats[i].label);
    }

    }   // !strip

    // Statuszeile + Fortschritt
    std::wstring message;
    if (m_scanning)
    {
        message = L"Scan läuft …";
        if (m_progressTotal > 0)
            message += L"  " + std::to_wstring(m_progress) + L" / " + std::to_wstring(m_progressTotal) + L" Geräte";
    }
    else if (m_installing)
    {
        static const wchar_t* const kStages[] = {
            L"Wiederherstellungspunkt wird erstellt …", L"Updates werden vorbereitet …",
            L"Treiber werden heruntergeladen …", L"Treiber werden installiert – bitte nicht beenden …" };
        message = kStages[std::clamp(m_installStage, 0, 3)];
    }
    else if (m_checkingUpdates)
        message = L"Suche nach Treiber-Updates über Windows Update …";
    else if (m_scannedOnce)
    {
        message = L"Letzter Scan: " + m_lastScanTime;
        if (!m_updateError.empty())
            message += L"  ·  Update-Abgleich nicht möglich: " + m_updateError;
        else if (m_updatesChecked)
            message += m_updateCount > 0 ? L"  ·  " + std::to_wstring(m_updateCount) + L" Update(s) verfügbar"
                                         : L"  ·  Alle Treiber sind aktuell (Windows Update + Hersteller-Abgleich)";
    }
    else
        message = L"Scan ausstehend – noch keine Daten erfasst";

    DrawLabel(dc, m_fontSmall, kTextDim, { r.left + pad, r.bottom - S(46), r.right - pad, r.bottom - S(28) }, message);

    RECT track{ r.left + pad, r.bottom - S(22), r.right - pad, r.bottom - S(14) };

    double fraction = 0.0;
    if (m_scanning && m_progressTotal > 0)
        fraction = static_cast<double>(m_progress) / m_progressTotal;
    else if (m_scannedOnce && !Busy())
        fraction = 1.0;

    const bool indeterminate = m_checkingUpdates || m_installing || (m_scanning && m_progressTotal == 0);
    if (Busy())
        DrawGlowBar(dc, track, fraction, indeterminate, m_marquee, kAccentDark, RGB(120, 195, 255), kBackground, true);
    else
        DrawGlowBar(dc, track, fraction, false, 0, RGB(30, 150, 92), kGreen, kBackground, false);
}

void MainWindow::DrawDriverCard(HDC dc, const RECT& rc, const DriverInfo& info, bool hover)
{
    const COLORREF cardColor = hover ? kCardHover : kCardRaised;
    DrawShadow(dc, rc, S(14), static_cast<int>(m_dpi));
    FillRound(dc, rc, S(14), cardColor, hover ? kCardBorderHover : kCardBorder);
    RECT btnDetails{}, btnMore{}, btnUpdate{}, btnDownload{};
    CardButtons(rc, btnDetails, btnMore);
    const bool hasActions = &info >= m_drivers.data() && &info < m_drivers.data() + m_drivers.size();
    if (hasActions)
        CardExtraButtons(rc, info, btnUpdate, btnDownload);
    int leftmost = btnDetails.left;
    if (btnUpdate.right > btnUpdate.left)
        leftmost = btnUpdate.left;
    if (btnDownload.right > btnDownload.left)
        leftmost = btnDownload.left;
    const int lineRight = hasActions ? leftmost - S(12) : rc.right - S(18);

    // Kategorie-Symbol links
    const int tileSize = S(52);
    const int tileTop = rc.top + (rc.bottom - rc.top - tileSize) / 2;
    const COLORREF iconColor = IconColor(info.icon);
    DrawCategoryTile(dc, { rc.left + S(16), tileTop, rc.left + S(16) + tileSize, tileTop + tileSize }, info.icon,
                     iconColor, cardColor);
    const int textX = rc.left + S(16) + tileSize + S(16);
    const int padR = S(18);

    // Status-Badge rechts oben
    const Badge badge = BadgeFor(info);
    const int pillW = TextWidth(dc, m_fontSmall, badge.text) + S(28);
    RECT pill{ rc.right - S(16) - pillW, rc.top + S(14), rc.right - S(16), rc.top + S(14) + S(26) };
    FillRound(dc, pill, S(26), Blend(cardColor, badge.color, 20), Blend(cardColor, badge.color, 50));
    // kleiner Punkt vor dem Text
    {
        HBRUSH dot = CreateSolidBrush(badge.color);
        HPEN none = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ ob = SelectObject(dc, dot);
        HGDIOBJ op = SelectObject(dc, none);
        const int cy = (pill.top + pill.bottom) / 2;
        Ellipse(dc, pill.left + S(10), cy - S(3), pill.left + S(10) + S(6) + 1, cy + S(3) + 1);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(dot);
        DeleteObject(none);
    }
    DrawLabel(dc, m_fontSmall, badge.color, { pill.left + S(18), pill.top + S(5), pill.right, pill.bottom }, badge.text, DT_CENTER);

    // Name
    DrawLabel(dc, m_fontHeading, kText, { textX, rc.top + S(12), pill.left - S(12), rc.top + S(36) }, info.deviceName);

    // Kategorie · Hersteller
    std::wstring sub = info.category;
    if (!info.manufacturer.empty())
        sub += L"  ·  " + info.manufacturer;
    DrawLabel(dc, m_fontSmall, iconColor, { textX, rc.top + S(38), rc.right - padR, rc.top + S(56) }, sub);

    // Version / Datum / Anbieter
    if (!info.driverVersion.empty() || !info.provider.empty())
    {
        std::wstring line = L"Version " + (info.driverVersion.empty() ? L"–" : info.driverVersion);
        line += L"     Datum " + (info.driverDate.empty() ? L"–" : info.driverDate);
        line += L"     Anbieter " + (info.provider.empty() ? L"–" : info.provider);
        DrawLabel(dc, m_fontBody, kText, { textX, rc.top + S(56), lineRight, rc.top + S(76) }, line);
    }

    // Update-Hinweis bzw. Hardware-ID
    if (info.status == DriverStatus::UpdateAvailable)
        DrawLabel(dc, m_fontSmall, kOrange, { textX, rc.top + S(76), lineRight, rc.bottom - S(6) },
                  L"Neu: " + info.updateTitle + L"  (" + info.updateDate + L")");
    else if (!info.hardwareId.empty())
        DrawLabel(dc, m_fontSmall, kTextDim, { textX, rc.top + S(76), lineRight, rc.bottom - S(6) },
                  L"PnP-ID  " + info.hardwareId);

    // Quick Actions: Details + Menue (Hover / gedrueckt)
    if (hasActions)
    {
        const int idx = static_cast<int>(&info - m_drivers.data());
        int visibleIdx = -1;
        for (size_t i = 0; i < m_visible.size(); ++i)
            if (static_cast<int>(m_visible[i]) == idx)
            {
                visibleIdx = static_cast<int>(i);
                break;
            }
        auto drawButton = [&](const RECT& b, int action, const wchar_t* label)
        {
            const bool pressed = m_pressAction == action && m_pressCard == visibleIdx;
            const bool over = hover && m_hoverAction == action;
            COLORREF fill = pressed ? Blend(cardColor, RGB(0, 0, 0), 25) : (over ? Blend(cardColor, RGB(255, 255, 255), 20) : Blend(cardColor, RGB(255, 255, 255), 5));
            COLORREF border = pressed ? kAccent : (over ? kCardBorderHover : kCardBorder);
            FillRound(dc, b, S(10), fill, border);
            DrawLabel(dc, m_fontSmall, over || pressed ? kText : kTextDim, { b.left, b.top + S(6), b.right, b.bottom }, label, DT_CENTER);
        };
        drawButton(btnDetails, 1, L"Details");
        drawButton(btnMore, 2, L"⋮");
        if (btnDownload.right > btnDownload.left)
            drawButton(btnDownload, 4, L"Download-Seite");
        if (btnUpdate.right > btnUpdate.left)
        {
            // hervorgehobener Haupt-Button (Orange wie der Badge)
            const bool pressed = m_pressAction == 3 && m_pressCard == visibleIdx;
            const bool over = hover && m_hoverAction == 3;
            const COLORREF fill = pressed ? Blend(kOrange, RGB(0, 0, 0), 25) : (over ? Blend(kOrange, RGB(255, 255, 255), 18) : kOrange);
            FillRound(dc, btnUpdate, S(10), fill, fill);
            DrawLabel(dc, m_fontSmall, RGB(44, 24, 0), { btnUpdate.left, btnUpdate.top + S(6), btnUpdate.right, btnUpdate.bottom },
                      L"Aktualisieren", DT_CENTER);
        }
    }
}
void MainWindow::DrawGroupHeader(HDC dc, const RECT& rc, int rowIndex, bool hover)
{
    const Row& row = m_rows[rowIndex];
    const bool open = GroupOpen(row.group);
    const COLORREF fill = hover ? kCardHover : kCard;

    DrawShadow(dc, rc, S(14), static_cast<int>(m_dpi));
    FillRound(dc, rc, S(14), fill, hover ? kCardBorderHover : kCardBorder);

    // Pfeil
    const int arrow = S(18);
    const int midY = (rc.top + rc.bottom) / 2;
    DrawChevron(dc, { rc.left + S(16), midY - arrow / 2, rc.left + S(16) + arrow, midY + arrow / 2 }, open,
                hover || open ? kText : kTextDim);

    // Gruppen-Symbol
    const int tile = S(36);
    const int tileX = rc.left + S(16) + arrow + S(14);
    DrawCategoryTile(dc, { tileX, midY - tile / 2, tileX + tile, midY + tile / 2 }, kGroupIcons[row.group],
                     IconColor(kGroupIcons[row.group]), fill);

    // Name + Zaehler
    const int textX = tileX + tile + S(14);
    DrawLabel(dc, m_fontHeading, kText, { textX, rc.top + S(15), rc.right - S(260), rc.bottom },
              std::wstring(kGroupNames[row.group]) + L" (" + std::to_wstring(row.count) + L")");

    // Hinweis-Pillen rechts (Updates / Probleme)
    int right = rc.right - S(18);
    auto pill = [&](int n, const wchar_t* label, COLORREF color)
    {
        if (n <= 0)
            return;
        const std::wstring text = std::to_wstring(n) + L" " + label;
        const int w = TextWidth(dc, m_fontSmall, text) + S(24);
        RECT p{ right - w, midY - S(13), right, midY + S(13) };
        FillRound(dc, p, S(26), Blend(fill, color, 20), Blend(fill, color, 50));
        DrawLabel(dc, m_fontSmall, color, { p.left, p.top + S(5), p.right, p.bottom }, text, DT_CENTER);
        right = p.left - S(8);
    };
    pill(row.problems, row.problems == 1 ? L"Problem" : L"Probleme", kRed);
    pill(row.updates, row.updates == 1 ? L"Update" : L"Updates", kOrange);
}

void MainWindow::DrawScrollbar(HDC dc)
{
    const RECT thumb = ScrollThumbRect();
    if (thumb.bottom <= thumb.top)
        return;
    // Fuehrungsschiene + Thumb (heller beim Ziehen)
    RECT rail{ m_listRect.right - S(6), m_listRect.top, m_listRect.right - S(4), m_listRect.bottom };
    FillRound(dc, rail, S(2), Blend(kBackground, kCardBorder, 40), Blend(kBackground, kCardBorder, 40));
    const COLORREF c = m_dragThumb ? kAccent : RGB(78, 84, 100);
    FillRound(dc, thumb, S(8), c, c);
}

// ---- Fonts --------------------------------------------------------------------------------

void MainWindow::CreateFonts()
{
    DestroyFonts();
    auto make = [this](int pt, int weight)
    {
        return CreateFontW(-MulDiv(pt, static_cast<int>(m_dpi), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    m_fontTitle   = make(18, FW_SEMIBOLD);
    m_fontBig     = make(22, FW_SEMIBOLD);
    m_fontHeading = make(11, FW_SEMIBOLD);
    m_fontBody    = make(10, FW_NORMAL);
    m_fontSmall   = make(9, FW_NORMAL);
    m_fontSym = CreateFontW(-MulDiv(16, static_cast<int>(m_dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
    m_appIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, S(48), S(48), LR_DEFAULTCOLOR));
}

void MainWindow::DestroyFonts()
{
    if (m_appIcon)
    {
        DestroyIcon(m_appIcon);
        m_appIcon = nullptr;
    }
    for (HFONT* f : { &m_fontTitle, &m_fontBig, &m_fontHeading, &m_fontBody, &m_fontSmall, &m_fontSym })
    {
        if (*f)
        {
            DeleteObject(*f);
            *f = nullptr;
        }
    }
}

// ---- Eigene Dialoge (Modal) und Toasts -----------------------------------------------------
// Alles wird im Hauptfenster gezeichnet (kein MessageBox): abgedunkelter Hintergrund, Panel im
// Dark-Theme mit Rand/Schatten und eigene Buttons. Mehrere Dialoge werden nacheinander gezeigt.

namespace
{
    COLORREF KindColor(int kind)
    {
        switch (kind)
        {
        case 1:  return kOrange;   // Warnung
        case 2:  return kRed;      // Fehler
        default: return kAccent;   // Info / Frage
        }
    }

    const wchar_t* KindGlyph(int kind)
    {
        switch (kind)
        {
        case 1:  return L"!";
        case 2:  return L"×";
        case 3:  return L"?";
        default: return L"i";
        }
    }

    int MeasureWrapped(HDC dc, HFONT font, const std::wstring& text, int width)
    {
        RECT r{ 0, 0, std::max(1, width), 0 };
        HGDIOBJ old = SelectObject(dc, font);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        return r.bottom - r.top;
    }

    void DrawWrapped(HDC dc, HFONT font, COLORREF color, RECT rc, const std::wstring& text)
    {
        HGDIOBJ old = SelectObject(dc, font);
        SetTextColor(dc, color);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rc, DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
    }
}

void MainWindow::ShowModal(const std::wstring& title, const std::wstring& text, std::vector<ModalRow> rows,
                           std::vector<std::wstring> buttons, int kind, std::function<void(int)> onClose)
{
    Modal modal;
    modal.active = true;
    modal.kind = kind;
    modal.title = title;
    modal.text = text;
    modal.rows = std::move(rows);
    modal.buttons = std::move(buttons);
    if (modal.buttons.empty())
        modal.buttons.push_back(L"OK");
    modal.onClose = std::move(onClose);

    if (m_modal.active)
        m_modalQueue.push_back(std::move(modal));
    else
        m_modal = std::move(modal);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::CloseModal(int result)
{
    if (!m_modal.active)
        return;
    auto callback = std::move(m_modal.onClose);
    m_modal = Modal();
    if (!m_modalQueue.empty())
    {
        m_modal = std::move(m_modalQueue.front());
        m_modalQueue.pop_front();
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
    if (callback)
        callback(result);
}

void MainWindow::ShowToast(const std::wstring& text, int kind)
{
    m_toastText = text;
    m_toastKind = kind;
    m_toastStart = GetTickCount();
    m_toastActive = true;
    SetTimer(m_hwnd, 2, 30, nullptr);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::CopyToClipboard(const std::wstring& text)
{
    bool ok = false;
    if (OpenClipboard(m_hwnd))
    {
        EmptyClipboard();
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            memcpy(GlobalLock(mem), text.c_str(), bytes);
            GlobalUnlock(mem);
            if (SetClipboardData(CF_UNICODETEXT, mem))
                ok = true;
            else
                GlobalFree(mem);
        }
        CloseClipboard();
    }
    if (ok)
        ShowToast(L"ID in Zwischenablage kopiert", 0);
    else
        ShowToast(L"Kopieren fehlgeschlagen", 2);
}

void MainWindow::OnModalMouseMove(int x, int y)
{
    POINT pt{ x, y };
    int hover = -1;
    for (size_t i = 0; i < m_modal.btnRects.size(); ++i)
        if (PtInRect(&m_modal.btnRects[i], pt))
            hover = static_cast<int>(i);
    if (hover != m_modal.hoverBtn)
    {
        m_modal.hoverBtn = hover;
        InvalidateRect(m_hwnd, &m_modal.box, FALSE);
    }
}

void MainWindow::OnModalMouse(int x, int y, bool down)
{
    OnModalMouseMove(x, y);
    if (down)
    {
        m_modal.pressBtn = m_modal.hoverBtn;
        InvalidateRect(m_hwnd, &m_modal.box, FALSE);
    }
    else
    {
        const int pressed = m_modal.pressBtn;
        m_modal.pressBtn = -1;
        if (pressed >= 0 && pressed == m_modal.hoverBtn)
            CloseModal(pressed);
        else
            InvalidateRect(m_hwnd, &m_modal.box, FALSE);
    }
}

void MainWindow::OnModalKey(WPARAM key)
{
    if (key == VK_ESCAPE)
        CloseModal(-1);
    else if (key == VK_RETURN || key == VK_SPACE)
        CloseModal(static_cast<int>(m_modal.buttons.size()) - 1);   // letzter Button = Hauptaktion
}

void MainWindow::DrawModal(HDC dc, int width, int height)
{
    FillAlphaRect(dc, RECT{ 0, 0, width, height }, RGB(0, 0, 0), 150);

    const int pad = S(26);
    const int boxW = std::min(S(600), width - S(40));
    const int inner = boxW - 2 * pad;
    const int titleH = S(30);
    const int btnH = S(38);
    const int labelW = S(150);
    const int valueW = inner - labelW - S(14);

    // Inhalt vermessen
    int contentH = 0;
    int textH = 0;
    if (!m_modal.text.empty())
    {
        textH = MeasureWrapped(dc, m_fontBody, m_modal.text, inner);
        contentH += textH;
    }
    std::vector<int> rowH(m_modal.rows.size());
    for (size_t i = 0; i < m_modal.rows.size(); ++i)
    {
        rowH[i] = std::max(S(20), MeasureWrapped(dc, m_fontBody, m_modal.rows[i].value, valueW)) + S(10);
        contentH += rowH[i];
    }
    if (textH > 0 && !m_modal.rows.empty())
        contentH += S(10);

    const int chrome = pad + titleH + S(14) + S(22) + btnH + pad;
    const int maxBody = std::max(S(80), height - S(80) - chrome);
    const int bodyH = std::min(contentH, maxBody);
    const int boxH = chrome + bodyH;
    m_modal.contentH = contentH;
    m_modal.viewH = bodyH;
    m_modal.scroll = std::clamp(m_modal.scroll, 0, std::max(0, contentH - bodyH));

    RECT box{ (width - boxW) / 2, (height - boxH) / 2, (width - boxW) / 2 + boxW, (height - boxH) / 2 + boxH };
    m_modal.box = box;

    DrawShadow(dc, box, S(18), static_cast<int>(m_dpi));
    DrawPanelAA(dc, box, S(18), RGB(27, 29, 36), kCardBorder);

    // Kopf: Symbol + Titel
    const COLORREF accent = KindColor(m_modal.kind);
    const int icon = S(30);
    RECT iconRc{ box.left + pad, box.top + pad, box.left + pad + icon, box.top + pad + icon };
    FillRound(dc, iconRc, icon, Blend(RGB(27, 29, 36), accent, 22), Blend(RGB(27, 29, 36), accent, 55));
    DrawLabel(dc, m_fontHeading, accent, { iconRc.left, iconRc.top + S(2), iconRc.right, iconRc.bottom }, KindGlyph(m_modal.kind), DT_CENTER);
    DrawLabel(dc, m_fontHeading, kText, { iconRc.right + S(12), box.top + pad + S(3), box.right - pad, box.top + pad + titleH }, m_modal.title);

    // Inhalt (geclippt, scrollbar per Mausrad)
    RECT body{ box.left + pad, box.top + pad + titleH + S(14), box.right - pad, box.top + pad + titleH + S(14) + bodyH };
    SaveDC(dc);
    IntersectClipRect(dc, body.left, body.top, body.right, body.bottom);
    int y = body.top - m_modal.scroll;
    if (textH > 0)
    {
        DrawWrapped(dc, m_fontBody, kText, RECT{ body.left, y, body.right, y + textH }, m_modal.text);
        y += textH + (m_modal.rows.empty() ? 0 : S(10));
    }
    for (size_t i = 0; i < m_modal.rows.size(); ++i)
    {
        const ModalRow& row = m_modal.rows[i];
        // feine Trennlinie
        HPEN pen = CreatePen(PS_SOLID, 1, kCardBorder);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, body.left, y + rowH[i] - S(5), nullptr);
        LineTo(dc, body.right, y + rowH[i] - S(5));
        SelectObject(dc, oldPen);
        DeleteObject(pen);

        DrawLabel(dc, m_fontSmall, kTextDim, RECT{ body.left, y + S(2), body.left + labelW, y + S(22) }, row.label);
        DrawWrapped(dc, m_fontBody, row.hasColor ? row.color : kText,
                    RECT{ body.left + labelW + S(14), y, body.right, y + rowH[i] }, row.value);
        y += rowH[i];
    }
    RestoreDC(dc, -1);

    if (contentH > bodyH)
    {
        const int travel = bodyH - std::max(S(28), bodyH * bodyH / contentH);
        const int thumbH = std::max(S(28), bodyH * bodyH / contentH);
        const int thumbTop = body.top + (contentH - bodyH > 0 ? travel * m_modal.scroll / (contentH - bodyH) : 0);
        FillRound(dc, RECT{ box.right - S(14), thumbTop, box.right - S(8), thumbTop + thumbH }, S(6), RGB(78, 84, 100), RGB(78, 84, 100));
    }

    // Buttons rechts unten, letzter = Hauptaktion
    m_modal.btnRects.assign(m_modal.buttons.size(), RECT{});
    int right = box.right - pad;
    const int btnTop = box.bottom - pad - btnH;
    for (int i = static_cast<int>(m_modal.buttons.size()) - 1; i >= 0; --i)
    {
        const std::wstring& label = m_modal.buttons[i];
        const int w = TextWidth(dc, m_fontBody, label) + S(44);
        RECT b{ right - w, btnTop, right, btnTop + btnH };
        m_modal.btnRects[i] = b;
        right = b.left - S(10);

        const bool primary = i == static_cast<int>(m_modal.buttons.size()) - 1;
        const bool hover = m_modal.hoverBtn == i;
        const bool pressed = m_modal.pressBtn == i && hover;
        COLORREF fill, border, text;
        if (primary)
        {
            fill = pressed ? Blend(accent, RGB(0, 0, 0), 25) : (hover ? Blend(accent, RGB(255, 255, 255), 14) : accent);
            border = fill;
            text = RGB(8, 20, 36);
        }
        else
        {
            fill = pressed ? RGB(24, 26, 32) : (hover ? RGB(45, 49, 61) : RGB(35, 38, 47));
            border = hover ? kCardBorderHover : kCardBorder;
            text = kText;
        }
        FillRound(dc, b, S(12), fill, border);
        DrawLabel(dc, m_fontBody, text, { b.left, b.top + S(8), b.right, b.bottom }, label, DT_CENTER);
    }
}

void MainWindow::DrawToast(HDC dc, int width, int height)
{
    if (!m_toastActive)
        return;
    const DWORD elapsed = GetTickCount() - m_toastStart;
    const DWORD life = 2000, fade = 280, fadeIn = 140;
    if (elapsed > life + fade)
        return;

    double alpha = 1.0;
    if (elapsed < fadeIn)
        alpha = static_cast<double>(elapsed) / fadeIn;
    else if (elapsed > life)
        alpha = 1.0 - static_cast<double>(elapsed - life) / fade;
    alpha = std::clamp(alpha, 0.0, 1.0);

    const int maxW = std::min(S(560), width - S(40));
    const int textW = TextWidth(dc, m_fontBody, m_toastText);
    const int w = std::min(maxW, textW + S(70));
    const int h = S(46);
    // leichtes Hochgleiten beim Einblenden
    const int slide = static_cast<int>((1.0 - std::min(1.0, static_cast<double>(elapsed) / fadeIn)) * S(10));
    RECT rc{ (width - w) / 2, height - S(34) - h + slide, (width - w) / 2 + w, height - S(34) + slide };
    m_toastRect = RECT{ rc.left - S(14), rc.top - S(14), rc.right + S(14), rc.bottom + S(18) };

    // Toast auf Kopie des Hintergrunds zeichnen und per AlphaBlend einblenden (Fade)
    const int mw = m_toastRect.right - m_toastRect.left, mh = m_toastRect.bottom - m_toastRect.top;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, mw, mh);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, mw, mh, dc, m_toastRect.left, m_toastRect.top, SRCCOPY);
    SetBkMode(mem, TRANSPARENT);

    RECT local{ rc.left - m_toastRect.left, rc.top - m_toastRect.top, rc.right - m_toastRect.left, rc.bottom - m_toastRect.top };
    DrawShadow(mem, local, S(14), static_cast<int>(m_dpi));
    DrawPanelAA(mem, local, S(14), RGB(35, 38, 47), kCardBorderHover);
    const COLORREF color = m_toastKind == 2 ? kRed : (m_toastKind == 1 ? kOrange : kGreen);
    const int dot = S(10);
    RECT d{ local.left + S(18), (local.top + local.bottom) / 2 - dot / 2, local.left + S(18) + dot, (local.top + local.bottom) / 2 + dot / 2 };
    FillRound(mem, d, dot, color, color);
    DrawLabel(mem, m_fontBody, kText, { local.left + S(40), local.top + S(11), local.right - S(16), local.bottom }, m_toastText);

    BLENDFUNCTION bf{ AC_SRC_OVER, 0, static_cast<BYTE>(alpha * 255), 0 };
    // Ergebnis = Mischung aus Original (dc) und gezeichneter Kopie
    HDC out = CreateCompatibleDC(dc);
    HBITMAP outBmp = CreateCompatibleBitmap(dc, mw, mh);
    HGDIOBJ oldOut = SelectObject(out, outBmp);
    BitBlt(out, 0, 0, mw, mh, dc, m_toastRect.left, m_toastRect.top, SRCCOPY);
    AlphaBlend(out, 0, 0, mw, mh, mem, 0, 0, mw, mh, bf);
    BitBlt(dc, m_toastRect.left, m_toastRect.top, mw, mh, out, 0, 0, SRCCOPY);
    SelectObject(out, oldOut);
    DeleteObject(outBmp);
    DeleteDC(out);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

// ---- Banner und Start-Animation (Splash) ----------------------------------------------------
// Banner: Platinen-Szene, eine Pruefsonde faehrt ueber die Leiterbahn und "prueft" die Knoten.
// Beim Scan faehrt sie in Schleife, danach bleibt sie am Ende (alles geprueft). Splash: kleines
// Fenster mit Logo und derselben Szene, blendet sanft ein, zeigt das Hauptfenster und blendet darueber aus.

namespace
{
    float EaseInOut(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    }

    constexpr ULONGLONG kSplashMs = 1600, kSplashFadeInMs = 220, kSplashFadeOutMs = 300;
}

float MainWindow::SweepP() const
{
    return EaseInOut(static_cast<float>(m_marquee % 120) / 120.0f);
}

void MainWindow::StartBannerEase(float target)
{
    m_bannerTarget = target;
    if (!m_motion)
    {
        m_bannerP = target;
        InvalidateRect(m_hwnd, &m_bannerRect, FALSE);
        return;
    }
    SetTimer(m_hwnd, 3, 16, nullptr);
}

void MainWindow::DrawBanner(HDC dc)
{
    const RECT& b = m_bannerRect;
    {
        // Panel samt Schatten nur bei Groessen-/DPI-Aenderung neu rendern, sonst nur kopieren
        const int mg = S(16);
        const int bw = b.right - b.left, bh = b.bottom - b.top;
        if (!m_bnDc || m_bnW != bw || m_bnH != bh || m_bnDpi != m_dpi)
        {
            ReleaseBannerCache();
            m_bnDc = CreateCompatibleDC(dc);
            m_bnBmp = CreateCompatibleBitmap(dc, bw + 2 * mg, bh + 2 * mg);
            m_bnOld = SelectObject(m_bnDc, m_bnBmp);
            RECT all{ 0, 0, bw + 2 * mg, bh + 2 * mg };
            HBRUSH bg = CreateSolidBrush(kBackground);
            FillRect(m_bnDc, &all, bg);
            DeleteObject(bg);
            DrawBannerPanel(m_bnDc, RECT{ mg, mg, mg + bw, mg + bh }, S(18), static_cast<int>(m_dpi));
            m_bnW = bw;
            m_bnH = bh;
            m_bnDpi = m_dpi;
        }
        BitBlt(dc, b.left - mg, b.top - mg, bw + 2 * mg, bh + 2 * mg, m_bnDc, 0, 0, SRCCOPY);
    }
    DrawScanScene(dc, b, m_bannerP, m_marquee, static_cast<int>(m_dpi), false);   // Banner bleibt beim Scan ruhig (der Balken darunter zeigt den Fortschritt)

    const int iconSize = S(48);
    if (m_appIcon)
        DrawIconEx(dc, b.left + S(22), b.top + S(14), m_appIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    const int titleX = b.left + S(22) + iconSize + S(14);
    const int textRight = ShowInstallButton() ? m_installRect.left - S(12) : m_buttonRect.left - S(12);
    DrawLabel(dc, m_fontTitle, kText, { titleX, b.top + S(10), textRight, b.top + S(42) }, L"Duncan's Driver Updater");
    DrawLabel(dc, m_fontSmall, kTextDim, { titleX, b.top + S(42), textRight, b.top + S(60) },
              L"Hardware- & Treiber-Manager");
}

void MainWindow::DrawSplash(HDC dc, int w, int h)
{
    RECT whole{ 0, 0, w, h };
    FillRect(dc, &whole, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetBkMode(dc, TRANSPARENT);
    DrawBannerPanel(dc, RECT{ S(1), S(1), w - S(1), h - S(1) }, S(22), static_cast<int>(m_dpi));

    if (m_splashIcon)
        DrawIconEx(dc, (w - S(72)) / 2, S(32), m_splashIcon, S(72), S(72), 0, nullptr, DI_NORMAL);
    DrawLabel(dc, m_fontTitle, kText, { 0, S(114), w, S(148) }, L"Duncan's Driver Updater", DT_CENTER);
    DrawLabel(dc, m_fontSmall, kTextDim, { 0, S(148), w, S(168) }, L"Hardware- & Treiber-Manager", DT_CENTER);

    const int phase = static_cast<int>(GetTickCount() / 40);
    DrawScanScene(dc, RECT{ S(20), S(172), w - S(20), S(262) }, EaseInOut(m_splashP), phase, static_cast<int>(m_dpi), true);
    DrawLabel(dc, m_fontSmall, kTextDim, { 0, S(266), w, S(288) }, L"wird geladen …", DT_CENTER);
}

LRESULT CALLBACK MainWindow::SplashProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_TIMER && self)
    {
        self->OnSplashTick(hwnd);
        return 0;
    }
    if (msg == WM_PAINT && self)
    {
        PAINTSTRUCT ps;
        HDC screen = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        self->DrawSplash(mem, rc.right, rc.bottom);
        BitBlt(screen, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Der Splash laeuft ueber einen Timer in der normalen Nachrichtenschleife (keine blockierenden Schleifen):
// einblenden -> Hauptfenster zeigen -> ueber dem Hauptfenster ausblenden -> sich selbst zerstoeren.
void MainWindow::OnSplashTick(HWND splash)
{
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG t = now - m_splashStart;

    if (!m_splashMainShown)
    {
        const double fade = std::min(1.0, static_cast<double>(t) / kSplashFadeInMs);
        SetLayeredWindowAttributes(splash, 0, static_cast<BYTE>(std::lround(255 * fade)), LWA_ALPHA);
        m_splashP = static_cast<float>(std::clamp((static_cast<double>(t) - 150.0) / (kSplashMs - 350.0), 0.0, 1.0));
        InvalidateRect(splash, nullptr, FALSE);
        UpdateWindow(splash);

        if (t >= kSplashMs)
        {
            m_bannerP = 0.0f;
            ShowWindow(m_hwnd, m_splashShowCmd);
            UpdateWindow(m_hwnd);
            StartBannerEase(0.62f);
            m_splashMainShown = true;
            m_splashFadeStart = GetTickCount64();
        }
        return;
    }

    const ULONGLONG f = now - m_splashFadeStart;
    if (f >= kSplashFadeOutMs)
    {
        KillTimer(splash, 1);
        m_splashHwnd = nullptr;
        if (m_splashIcon)
        {
            DestroyIcon(m_splashIcon);
            m_splashIcon = nullptr;
        }
        DestroyWindow(splash);
        return;
    }
    SetLayeredWindowAttributes(splash, 0, static_cast<BYTE>(std::lround(255 * (1.0 - static_cast<double>(f) / kSplashFadeOutMs))), LWA_ALPHA);
}

// Startet den Splash und kehrt sofort zurueck; das Hauptfenster wird erst vom Splash-Timer gezeigt.
bool MainWindow::RunSplash(HINSTANCE instance, int showCommand)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &MainWindow::SplashProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DuncansDriverUpdaterSplash";
    RegisterClassExW(&wc);

    const int w = S(480), h = S(300);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    const int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
    const int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;

    m_splashIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, S(72), S(72), LR_DEFAULTCOLOR));
    m_splashHwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP,
                                   x, y, w, h, nullptr, nullptr, instance, this);
    if (!m_splashHwnd)
    {
        if (m_splashIcon)
            DestroyIcon(m_splashIcon);
        m_splashIcon = nullptr;
        return false;
    }
    SetWindowRgn(m_splashHwnd, CreateRoundRectRgn(0, 0, w + 1, h + 1, S(44), S(44)), TRUE);
    SetLayeredWindowAttributes(m_splashHwnd, 0, 0, LWA_ALPHA);
    ShowWindow(m_splashHwnd, SW_SHOWNOACTIVATE);

    m_splashShowCmd = showCommand;
    m_splashMainShown = false;
    m_splashStart = GetTickCount64();
    SetTimer(m_splashHwnd, 1, 16, nullptr);
    return true;
}
