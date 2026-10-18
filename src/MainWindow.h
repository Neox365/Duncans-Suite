#pragma once
// MainWindow.h - Hauptfenster im Dark-/Card-Design (reines Win32 + GDI, doppelt gepuffert).
//
// Linker-Bibliotheken: user32.lib, gdi32.lib, dwmapi.lib, advapi32.lib

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "DriverInfo.h"
#include "HardwareScanner.h"
#include "UpdateChecker.h"
#include "UpdateInstaller.h"
#include "VendorReleases.h"

class MainWindow
{
public:
    MainWindow() = default;
    ~MainWindow();

    // autoInstall: Scan + Update-Suche + Installation ohne weitere Rueckfrage (nach UAC-Neustart)
    bool Create(HINSTANCE instance, int showCommand, bool autoInstall = false);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    // Nachrichten
    void OnCreate();
    void OnPaint();
    void OnSize();
    void OnMouseMove(int x, int y);
    void OnLButtonDown(int x, int y);
    void OnMouseWheel(int delta);
    void OnKeyDown(WPARAM key);
    void OnDpiChanged(UINT dpi, const RECT* suggested);

    // Logik
    void StartScan();
    void ApplyScanResults();
    void StartUpdateCheck();
    bool Busy() const { return m_scanning || m_checkingUpdates || m_installing; }
    bool ShowInstallButton() const { return m_installableCount > 0 && !Busy(); }
    void OnInstallClicked();
    void StartInstall(std::vector<std::wstring> ids = {});   // leer = alle installierbaren
    void ConfirmInstall(std::vector<std::wstring> ids, int count);
    void UpdateDriver(const DriverInfo& d);                  // Aktualisieren-Button / Menue
    void OpenUrl(const std::wstring& url);
    void CardExtraButtons(const RECT& card, const DriverInfo& info, RECT& update, RECT& download) const;
    bool RelaunchElevated();

    // Zeichnen
    void Layout();
    void Draw(HDC dc, int width, int height);
    void DrawStatusCard(HDC dc);
    void DrawDriverCard(HDC dc, const RECT& rc, const DriverInfo& info, bool hover);
    void UpdateHover(POINT pt);
    void OnLButtonUp();
    // Banner + Start-Animation
    void DrawBanner(HDC dc);
    float SweepP() const;                     // Sondenposition waehrend eines Scans (0..1, laeuft in Schleife)
    void StartBannerEase(float target);       // Sonde gleitet sanft zur Zielposition
    bool RunSplash(HINSTANCE instance, int showCommand);
    void OnSplashTick(HWND splash);
    void ReleaseBackBuffer();
    void ReleaseBannerCache();
    void PaintRegion(const RECT& rc);   // zeichnet einen Bereich sofort (Animation), ohne WM_PAINT-Umweg
    void DrawSplash(HDC dc, int w, int h);
    static LRESULT CALLBACK SplashProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void BuildRows();                  // Kopfzeilen + sichtbare Karten je nach Aufklapp-Zustand
    int  RowAt(int clientY) const;     // Zeilenindex unter y oder -1
    bool GroupOpen(int group) const;
    void DrawGroupHeader(HDC dc, const RECT& rc, int rowIndex, bool hover);
    void CardButtons(const RECT& card, RECT& details, RECT& more) const;
    RECT CardRect(int visibleIndex) const;
    int  HitAction(POINT pt, int& cardIdx) const;   // 0 = nichts, 1 = Details, 2 = Menue
    void RunCardAction(int cardIdx, int action);
    void ShowDetails(const DriverInfo& d);
    void ShowCardMenu(const DriverInfo& d, const RECT& anchor);
    void BackupDriver(const DriverInfo& d);

    // Eigene Dialoge / Toasts (statt MessageBox)
    struct ModalRow
    {
        std::wstring label;
        std::wstring value;
        COLORREF color = 0;
        bool hasColor = false;
    };
    struct Modal
    {
        bool active = false;
        int  kind = 0;                       // 0 Info, 1 Warnung, 2 Fehler, 3 Frage
        std::wstring title;
        std::wstring text;
        std::vector<ModalRow> rows;
        std::vector<std::wstring> buttons;   // letzter Button = Hauptaktion
        std::function<void(int)> onClose;    // Button-Index, -1 bei Esc
        RECT box{};
        std::vector<RECT> btnRects;
        int  hoverBtn = -1;
        int  pressBtn = -1;
        int  scroll = 0;
        int  contentH = 0;
        int  viewH = 0;
    };
    void ShowModal(const std::wstring& title, const std::wstring& text, std::vector<ModalRow> rows,
                   std::vector<std::wstring> buttons, int kind, std::function<void(int)> onClose);
    void CloseModal(int result);
    void ShowToast(const std::wstring& text, int kind = 0);   // 0 OK, 1 Warnung, 2 Fehler
    void CopyToClipboard(const std::wstring& text);
    void DrawModal(HDC dc, int width, int height);
    void DrawToast(HDC dc, int width, int height);
    void OnModalMouseMove(int x, int y);
    void OnModalMouse(int x, int y, bool down);
    void OnModalKey(WPARAM key);
    RECT ScrollThumbRect() const;   // leer, wenn nichts zu scrollen ist
    void ExportCsv();
    void DrawScrollbar(HDC dc);
    void DrawFilterBar(HDC dc);
    void RebuildVisible();

    // Helfer
    int  S(int px) const;            // DPI-Skalierung
    void CreateFonts();
    void DestroyFonts();
    void SetScroll(int value);
    int  MaxScroll() const;
    int  CardHeight() const;
    int  CardGap() const;

    HWND  m_hwnd = nullptr;
    UINT  m_dpi = 96;

    HFONT m_fontTitle = nullptr;
    HFONT m_fontHeading = nullptr;
    HFONT m_fontBody = nullptr;
    HFONT m_fontSmall = nullptr;
    HFONT m_fontBig = nullptr;
    HFONT m_fontSym = nullptr;

    HardwareScanner m_scanner;
    UpdateChecker m_updater;
    UpdateInstaller m_installer;
    bool m_installing = false;
    bool m_autoInstall = false;
    int  m_installStage = 0;
    bool m_installHover = false;
    RECT m_installRect{};
    bool m_checkingUpdates = false;
    bool m_updatesChecked = false;
    int  m_updateCount = 0;         // alle Updates (Windows Update + Hersteller-Abgleich) = Zaehler oben
    int  m_installableCount = 0;    // davon ueber Windows Update installierbar
    int  m_vendorCount = 0;         // davon nur per Hersteller-Abgleich erkannt (Download beim Hersteller)
    std::vector<VendorRelease> m_vendorReleases;
    int  m_marquee = 0;
    std::wstring m_updateError;
    std::vector<DriverInfo> m_drivers;
    bool m_scanning = false;
    bool m_scannedOnce = false;
    int  m_progress = 0;
    int  m_progressTotal = 0;
    std::wstring m_lastScanTime;
    bool m_isAdmin = false;

    // Layout (Client-Koordinaten)
    RECT m_buttonRect{};
    RECT m_statusRect{};
    RECT m_listTitleRect{};
    RECT m_listRect{};
    RECT m_searchRect{};
    RECT m_chipRect[3]{};
    bool m_dragThumb = false;       // Scrollbalken wird gezogen
    int  m_dragOffset = 0;
    int  m_sortMode = 0;            // 0 = Kategorie, 1 = Name, 2 = Datum, 3 = Status
    RECT m_sortRect{};
    RECT m_exportRect{};
    int  m_hoverAction = 0;         // Quick-Action unter der Maus (siehe HitAction)
    int  m_pressCard = -1;          // gedrueckter Quick-Action-Button
    int  m_pressAction = 0;
    HICON m_appIcon = nullptr;
    struct Row
    {
        bool header = false;
        int  group = 0;
        int  visibleIdx = -1;   // Karte: Index in m_visible
        int  top = 0;           // relativ zum Listeninhalt
        int  height = 0;
        int  count = 0;         // nur Kopfzeile: Geraete in der Gruppe
        int  updates = 0;
        int  problems = 0;
    };
    std::vector<Row> m_rows;
    std::vector<int> m_cardRow;     // Index in m_visible -> Zeile (oder -1, wenn eingeklappt)
    int  m_contentHeight = 0;
    int  m_groupState[7] = {};      // 0 = Standard (zu, ausser bei Suche/Filter), 1 = vom Nutzer auf, 2 = vom Nutzer zu
    bool m_autoExpandPrev = false;
    int  m_hoverHeader = -1;        // Zeilenindex der Kopfzeile unter der Maus
    Modal m_modal;

    // Einstellungsdialog (Zahnrad oben rechts)
    struct Settings
    {
        bool autoScan = false;   // beim Start automatisch scannen
        bool splash = true;      // Start-Animation zeigen
    };
    Settings m_settings, m_draft;
    bool m_settingsOpen = false;
    bool m_gearHover = false;
    bool m_gearPress = false;
    int  m_setHover = 0;         // 1 Scan-Haken, 2 Splash-Haken, 3 Speichern, 4 Abbrechen
    int  m_setPress = 0;
    RECT m_gearRect{}, m_chkScan{}, m_chkSplash{}, m_btnSave{}, m_btnCancel{};
    void OpenSettings();
    void CloseSettings(bool save);
    void DrawSettings(HDC dc, int width, int height);
    void SettingsMouse(int x, int y, bool down);
    std::deque<Modal> m_modalQueue;
    std::wstring m_toastText;
    int   m_toastKind = 0;
    DWORD m_toastStart = 0;
    bool  m_toastActive = false;
    RECT  m_toastRect{};
    RECT  m_bannerRect{};
    float m_bannerP = 0.62f;        // aktuelle Sondenposition im Ruhezustand
    float m_bannerTarget = 0.62f;
    bool  m_motion = true;          // false, wenn Windows Animationen abgeschaltet hat
    float m_splashP = 0.0f;
    HICON m_splashIcon = nullptr;
    HWND  m_splashHwnd = nullptr;
    ULONGLONG m_splashStart = 0;
    ULONGLONG m_splashFadeStart = 0;
    bool  m_splashMainShown = false;
    int   m_splashShowCmd = SW_SHOWNORMAL;
    // gecachter Zeichenpuffer (nur der ungueltige Bereich wird neu gezeichnet)
    // vorgerenderter, statischer Banner-Hintergrund (Verlauf, Raster, Schatten)
    HDC     m_bnDc = nullptr;
    HBITMAP m_bnBmp = nullptr;
    HGDIOBJ m_bnOld = nullptr;
    int     m_bnW = 0, m_bnH = 0;
    UINT    m_bnDpi = 0;
    HDC     m_backDc = nullptr;
    HBITMAP m_backBmp = nullptr;
    HGDIOBJ m_backOld = nullptr;
    int     m_backW = 0, m_backH = 0;
    int  m_hoverCard = -1;          // Index in m_visible unter dem Mauszeiger
    int  m_filterMode = 0;          // 0 = Alle, 1 = Updates, 2 = Probleme
    std::wstring m_filterText;
    std::vector<size_t> m_visible;  // Indizes in m_drivers nach Filter/Suche
    bool m_buttonHover = false;
    RECT m_aboutRect{};             // Fusszeile "© 2026 Duncan · vX.Y.Z" (Klick oeffnet "Ueber")
    bool m_aboutHover = false;
    void ShowAbout();
    bool m_trackingMouse = false;
    int  m_scroll = 0;
};
