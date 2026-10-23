// Duncan's Photo Commander (Win32 + Direct2D/DirectWrite + WIC)
//
// Aufbau: Topbar | Ordnerbaum | Thumbnail-Grid | Detail-Panel | Statusleiste.
// Doppelklick/Enter oeffnet den Bildbetrachter (Zoom, Verschieben, Zuschneiden, Blaettern).
// Alles wird selbst in Direct2D gezeichnet (kein Common Controls), damit das dunkle
// Flat-Design der Suite pixelgenau umgesetzt werden kann.
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <dxgiformat.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Duplicates.h"
#include "ImageOps.h"
#include "Theme.h"
#include "ThumbnailLoader.h"

#ifndef APP_VERSION
#define APP_VERSION L"0.2.5"   // normalerweise aus CMake (PHOTO_VER)
#endif

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT WM_APP_THUMBS = WM_APP + 1;
constexpr UINT WM_APP_BATCH = WM_APP + 2;      // wParam = fertig, lParam = gesamt
constexpr UINT WM_APP_BATCHDONE = WM_APP + 3;  // Ergebnis steht in den Batch-Zaehlern der App
constexpr UINT WM_APP_SCAN = WM_APP + 4;       // Ordnerlisting fertig
constexpr UINT WM_APP_TREE = WM_APP + 5;       // Unterordner eines Baumknotens gelesen
constexpr UINT WM_APP_META = WM_APP + 6;       // Aufnahmedatum/GPS-Flags von Bildern gelesen
constexpr UINT WM_APP_DUP = WM_APP + 7;        // Duplikatsuche: Fortschritt oder fertig
constexpr UINT_PTR TIMER_SCROLL = 1;
constexpr UINT_PTR TIMER_SLIDE = 2;             // Diashow: naechstes Bild
constexpr UINT_PTR TIMER_FADE = 3;              // Diashow: Steuerleiste ausblenden
constexpr int kMaxThumbSize = 320, kMinThumbSize = 96;
constexpr UINT kShareEdge = 2048;               // Stapel "Verkleinern": laengste Kante in Pixeln

enum ButtonId {
    BTN_OPEN = 1, BTN_FULLSCREEN, BTN_BATCH, BTN_SETTINGS,
    BTN_ROT_L, BTN_ROT_R, BTN_CROP, BTN_STRIP, BTN_EDIT,
    VB_PREV = 20, VB_NEXT, VB_FIT, VB_ACTUAL, VB_CLOSE, VB_DELETE, VB_CROP_OK, VB_CROP_CANCEL,
    VB_EDIT_SAVE, VB_EDIT_SAVEAS, VB_EDIT_RESET, VB_EDIT_CANCEL,
    VB_SLIDE_PREV, VB_SLIDE_PLAY, VB_SLIDE_NEXT, VB_SLIDE_CLOSE
};
enum MenuId {
    MENU_SIZE_S = 100, MENU_SIZE_M, MENU_SIZE_L, MENU_OVERWRITE = 110, MENU_ABOUT = 120,
    MENU_SORT_NAME = 130, MENU_SORT_DATE, MENU_SORT_SIZE, MENU_SORT_TAKEN, MENU_SORT_DESC = 135,
    MENU_B_STRIP_COPY = 140, MENU_B_STRIP_OVERWRITE, MENU_B_RESIZE, MENU_B_ROT_CW, MENU_B_ROT_CCW,
    MENU_B_DUPES, MENU_B_DUPES_END,
    MENU_FILTER_GPS = 150,
    MENU_SLIDE_2 = 160, MENU_SLIDE_4, MENU_SLIDE_6, MENU_SLIDE_10
};

enum class BatchKind { StripCopy, StripOverwrite, Resize, RotateCw, RotateCcw };

struct Settings {
    int thumbSize = 170;              // DIP, laengste Kante
    bool overwriteOriginals = false;  // EXIF-Cleaner/Zuschneiden: Original ersetzen statt Kopie
    int sortBy = 0;                   // 0 Name, 1 Aenderungsdatum, 2 Dateigroesse, 3 Aufnahmedatum (EXIF)
    bool sortDesc = false;
    int slideSeconds = 4;             // Diashow: Sekunden pro Bild
    std::wstring lastFolder;
};

struct Btn {
    int id;
    std::wstring label;
    D2D1_RECT_F rc{};
    bool accent = false;
    bool danger = false;
    bool icon = false;  // Symbol-Font (Zahnrad)
    bool needsSelection = false;
};

struct TreeNode {
    std::wstring name, path;
    int depth = 0;
    bool expanded = false, loaded = false, empty = false;
    bool loading = false, wantExpand = false;   // Unterordner werden im Hintergrund gelesen
    TreeNode* parent = nullptr;
    std::vector<std::unique_ptr<TreeNode>> children;
};

struct Item {
    std::wstring path, name;
    ULONGLONG size = 0, mtime = 0;    // mtime: FILETIME als 64 Bit
    ComPtr<ID2D1Bitmap> thumb;
    bool failed = false;
    bool selected = false;
    // EXIF-Kurzinfo (wird nach dem Ordnerlisting im Hintergrund nachgeladen)
    bool metaDone = false, hasGps = false;
    ULONGLONG taken = 0;              // Aufnahmezeit als FILETIME (UTC), 0 = unbekannt
    // Duplikatansicht: Gruppe (1-basiert, 0 = keine), Rang in der Gruppe (0 = behalten)
    int dupGroup = 0, dupRank = 0;
    bool dupExact = false;
};

struct MetaResult {
    std::wstring path;
    ULONGLONG taken = 0;
    bool hasGps = false;
};
struct MetaState {
    std::mutex mutex;
    std::vector<MetaResult> results;
    std::vector<std::wstring> todo;
    std::atomic<size_t> next{0}, workersLeft{0};
    std::atomic<bool> cancel{false}, done{false}, pending{false};
};

struct DupState {
    dup::Progress pr;
    std::mutex mutex;
    std::vector<dup::File> files;
    std::vector<dup::Group> groups;
    std::atomic<bool> done{false}, pending{false};
    std::atomic<ULONGLONG> lastPost{0};
};

// Ergebnis des Unterordner-Lesens (Hintergrundthread -> UI-Thread). Der Knoten selbst wird nur im UI-Thread angefasst.
struct TreeResult {
    TreeNode* node = nullptr;
    std::vector<std::wstring> names;
};
struct TreeState {
    std::mutex mutex;
    std::vector<TreeResult> done;
};

struct FileEntry {
    std::wstring path, name;
    ULONGLONG size = 0, mtime = 0;
};

// Ergebnis des Ordnerlistings (Hintergrundthread -> UI-Thread). Der Thread haelt den Zustand per shared_ptr,
// damit er auch nach dem Schliessen des Fensters gefahrlos ausläuft.
struct ScanState {
    std::mutex mutex;
    uint32_t gen = 0;
    std::vector<FileEntry> files;
    bool ready = false;
};

struct GridM {
    float pad, cellW, cellH, thumb, content;
    int cols, rows;
};

inline bool In(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
inline float W(const D2D1_RECT_F& r) { return r.right - r.left; }
inline float H(const D2D1_RECT_F& r) { return r.bottom - r.top; }
inline D2D1_RECT_F Inset(D2D1_RECT_F r, float d) { return {r.left + d, r.top + d, r.right - d, r.bottom - d}; }

std::wstring FormatSize(ULONGLONG b) {
    wchar_t buf[48];
    if (b < 1024) swprintf_s(buf, L"%llu B", b);
    else if (b < 1024ull * 1024) swprintf_s(buf, L"%.1f KB", b / 1024.0);
    else if (b < 1024ull * 1024 * 1024) swprintf_s(buf, L"%.2f MB", b / 1048576.0);
    else swprintf_s(buf, L"%.2f GB", b / 1073741824.0);
    return buf;
}

std::wstring FormatTime(const FILETIME& ft) {
    FILETIME lf;
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(&ft, &lf) || !FileTimeToSystemTime(&lf, &st)) return L"-";
    wchar_t buf[48];
    swprintf_s(buf, L"%04d-%02d-%02d  %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring s;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) s = p;
    CoTaskMemFree(p);
    return s;
}

std::wstring SettingsPath() {
    std::wstring dir = KnownFolder(FOLDERID_LocalAppData);
    if (dir.empty()) return L"";
    dir += L"\\DuncansPhotoCommander";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

void LoadSettings(Settings& st) {
    const std::wstring ini = SettingsPath();
    if (ini.empty()) return;
    st.thumbSize = std::clamp((int)GetPrivateProfileIntW(L"Allgemein", L"ThumbnailGroesse", st.thumbSize, ini.c_str()),
                              kMinThumbSize, kMaxThumbSize);
    st.overwriteOriginals = GetPrivateProfileIntW(L"Allgemein", L"OriginalUeberschreiben", 0, ini.c_str()) != 0;
    st.sortBy = std::clamp((int)GetPrivateProfileIntW(L"Allgemein", L"Sortierung", 0, ini.c_str()), 0, 3);
    st.sortDesc = GetPrivateProfileIntW(L"Allgemein", L"Absteigend", 0, ini.c_str()) != 0;
    st.slideSeconds = std::clamp((int)GetPrivateProfileIntW(L"Allgemein", L"DiashowSekunden", 4, ini.c_str()), 1, 60);
    wchar_t buf[2048]{};
    GetPrivateProfileStringW(L"Allgemein", L"LetzterOrdner", L"", buf, 2048, ini.c_str());
    st.lastFolder = buf;
}

void SaveSettings(const Settings& st) {
    const std::wstring ini = SettingsPath();
    if (ini.empty()) return;
    auto put = [&](const wchar_t* key, const std::wstring& v) {
        WritePrivateProfileStringW(L"Allgemein", key, v.c_str(), ini.c_str());
    };
    put(L"ThumbnailGroesse", std::to_wstring(st.thumbSize));
    put(L"OriginalUeberschreiben", st.overwriteOriginals ? L"1" : L"0");
    put(L"Sortierung", std::to_wstring(st.sortBy));
    put(L"Absteigend", st.sortDesc ? L"1" : L"0");
    put(L"DiashowSekunden", std::to_wstring(st.slideSeconds));
    put(L"LetzterOrdner", st.lastFolder);
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t cut = path.find_last_of(L'\\');
    return cut == std::wstring::npos ? path : path.substr(cut + 1);
}

std::wstring ParentOf(const std::wstring& path) {
    const size_t cut = path.find_last_of(L'\\');
    return cut == std::wstring::npos ? L"" : path.substr(0, cut);
}

// "C:\\Fotos\\a.jpg" + "_clean" -> "C:\\Fotos\\a_clean.jpg"
std::wstring WithSuffix(const std::wstring& path, const std::wstring& suffix) {
    const size_t dot = path.find_last_of(L'.'), cut = path.find_last_of(L'\\');
    if (dot == std::wstring::npos || (cut != std::wstring::npos && dot < cut)) return path + suffix;
    return path.substr(0, dot) + suffix + path.substr(dot);
}

std::wstring UniquePath(const std::wstring& p) {
    if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) return p;
    for (int i = 2; i < 1000; ++i) {
        std::wstring c = WithSuffix(p, L"_" + std::to_wstring(i));
        if (GetFileAttributesW(c.c_str()) == INVALID_FILE_ATTRIBUTES) return c;
    }
    return p;
}

class App {
public:
    bool Create(HINSTANCE hi, int show, const std::wstring& startArg);
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    // --- Lebenszyklus / Geraet
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    bool CreateDeviceIndependent();
    void CreateFormats();
    void EnsureDevice();
    void DiscardDevice();
    void Layout();
    void Paint();

    // --- Zeichnen
    void SetCol(unsigned rgb, float a = 1.f) { brush->SetColor(D2D1::ColorF(rgb, a)); }
    void Fill(const D2D1_RECT_F& r, unsigned c, float a = 1.f) { SetCol(c, a); rt->FillRectangle(r, brush.Get()); }
    void FillRR(const D2D1_RECT_F& r, float rad, unsigned c, float a = 1.f) {
        SetCol(c, a);
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), brush.Get());
    }
    void StrokeRR(const D2D1_RECT_F& r, float rad, unsigned c, float w = 1.f) {
        SetCol(c);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(r, rad, rad), brush.Get(), w);
    }
    void Txt(const std::wstring& s, const D2D1_RECT_F& r, IDWriteTextFormat* f, unsigned c, float a = 1.f) {
        SetCol(c, a);
        rt->DrawText(s.c_str(), (UINT32)s.size(), f, r, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void DrawFit(ID2D1Bitmap* b, const D2D1_RECT_F& box);
    bool BtnEnabled(const Btn& b) const;
    const std::vector<Btn>& ActiveBtns() const { return m_viewer ? m_vbtns : m_btns; }
    void DrawButton(const Btn& b);
    void DrawScrollbar(const D2D1_RECT_F& area, float scroll, float content);
    void DrawTopbar();
    void DrawTree();
    void DrawGrid();
    void DrawDetails();
    void DrawStatus();
    void DrawViewer();

    // --- Ordnerbaum
    void InitTreeRoots();
    void LoadChildrenAsync(TreeNode* n);
    void OnTreeLoaded();
    void Flatten();
    void ToggleNode(TreeNode* n);

    // --- Ordner / Grid
    void OpenFolder(const std::wstring& path, const std::wstring& selectPath = L"", bool keepScroll = false);
    void OnScanDone();
    void SortItems();
    bool Visible(const Item& it) const;         // Filter: nur mit Standort / Duplikatansicht
    void ApplyFilter();                         // m_items/m_hidden nach Filter neu aufteilen, sortieren, neu laden
    std::wstring FolderStatus() const;
    void StartMetaPass();                       // Aufnahmedatum + GPS-Flag im Hintergrund lesen
    void OnMeta();
    void StartDupSearch();
    void OnDup();
    void EndDupMode();
    void ClearDupMarks();
    void BrowseForFolder();
    GridM Metrics() const;
    int HitGrid(float x, float y) const;
    void Select(int idx);                       // genau ein Bild auswaehlen (-1 = Auswahl leeren)
    void Focus(int idx);                        // Vorschau/Info fuer dieses Bild laden
    void SelectRange(int a, int b, bool add);
    void SelectAll();
    int SelCount() const;
    int FirstSelected() const;
    std::vector<int> SelectedIdx() const;       // Auswahl, sonst das fokussierte Bild
    UINT PreviewPx() const;
    void EnsureVisible(int idx);
    void SetGridScroll(float target, bool instant = false);
    void RequestVisible();
    void ConsumeThumbs();
    void ReloadItem(int idx);

    // --- Aktionen
    void Command(int id);
    void ToggleFullscreen();
    void ShowSettings();
    void StripSelected();
    std::wstring ChooseOutput(const std::wstring& src, const wchar_t* suffix, const wchar_t* question, const wchar_t* title);
    void FinishWrite(const std::wstring& src, const std::wstring& dst, const std::wstring& doneInPlace,
                     const std::wstring& doneCopy);
    void RotateSelected(bool cw);
    void DeleteSelected();
    void ShowBatchMenu();
    void StartBatch(BatchKind kind);
    void FinishBatch();
    void SaveSettingsNow() { SaveSettings(m_settings); }

    // --- Bildbetrachter
    void EnterViewer(bool crop);
    void ExitViewer();
    void LayoutViewer();
    D2D1_RECT_F ViewerImageRect() const;
    ID2D1Bitmap* ViewerBitmap() const;
    float ViewerFitScale(const D2D1_SIZE_F& sz) const;
    void ViewerFit();
    void ViewerActual(float px = -1.f, float py = -1.f);
    bool OrientedSize(double& w, double& h) const;
    void ViewerZoomAt(float factor, float px, float py);
    void ClampPan();
    void ViewerNav(int delta);
    void BeginCrop();
    void CancelCrop();
    void ApplyCrop();
    void OnViewerKey(WPARAM vk);

    // --- Diashow (Vollbild, Timer, ausblendende Steuerleiste)
    void StartSlideshow();
    void EndSlideshow();
    void SlideAdvance(int delta);               // mit Umbruch am Ordnerende
    void SlideRestartTimer();
    float SlideOverlayAlpha() const;            // 1 = Leiste sichtbar, 0 = ausgeblendet
    void DrawSlide();

    // --- Bildkorrektur (Bearbeiten-Modus im Betrachter)
    void BeginEdit();
    void EndEdit();
    void RebuildEditBitmap();                   // Farbkorrektur auf die Vorschau-Pixel, als D2D-Bitmap
    void SaveEdit(bool saveAs);
    int EditSliderAt(float x, float y) const;   // Regler unter dem Punkt, sonst -1
    D2D1_RECT_F EditSliderBlock(int i) const;
    D2D1_RECT_F EditSliderTrack(int i) const;
    double EditSliderValue(int i) const;
    void SetEditSlider(int i, double v);
    void SetEditSliderFromX(int i, float x);
    void DrawEditPreview();
    void DrawEditPanel();
    void OnDropFiles(HDROP drop);
    void SetStatus(const std::wstring& s) { m_status = s; }
    void Invalidate() { InvalidateRect(m_hwnd, nullptr, FALSE); }

    HWND m_hwnd = nullptr;
    float k = 1.f;  // DPI-Faktor (Layout in Pixeln = Design * k)

    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dw;
    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> fNormal, fSmall, fBold, fTitle, fIcon, fCenter;

    ComPtr<IDWriteTextFormat> fRight, fTag;

    D2D1_RECT_F rcTop{}, rcTree{}, rcGrid{}, rcDetail{}, rcStatus{};
    D2D1_RECT_F rcViewer{}, rcViewBar{};
    D2D1_RECT_F rcPreview{}, rcActionsHead{};
    float m_infoTop = 0;
    std::vector<Btn> m_btns, m_vbtns;
    int m_hoverBtn = 0;

    Settings m_settings;

    // Baum
    std::vector<std::unique_ptr<TreeNode>> m_roots;
    std::vector<TreeNode*> m_rows;
    TreeNode* m_selNode = nullptr;
    float m_treeScroll = 0;
    int m_hoverRow = -1;
    static constexpr float kRowH = 28.f;

    // Grid
    std::wstring m_folder;
    std::vector<Item> m_items;
    std::vector<Item> m_hidden;                  // durch Filter ausgeblendete Bilder des Ordners
    bool m_gpsOnly = false;                      // Filter: nur Bilder mit Standortdaten
    bool m_dupMode = false, m_dupRunning = false;
    bool m_resortPending = false;                // Neusortieren/Filtern wartet, bis der Betrachter schliesst
    std::shared_ptr<MetaState> m_meta;
    std::shared_ptr<DupState> m_dup;
    int m_sel = -1, m_hoverItem = -1, m_anchor = -1;
    float m_gridScroll = 0, m_gridTarget = 0;
    int m_lastReqRow = -1;
    size_t m_loaded = 0;
    ThumbnailLoader m_loader;

    // Ordnerlisting im Hintergrund
    std::shared_ptr<ScanState> m_scan = std::make_shared<ScanState>();
    std::shared_ptr<TreeState> m_treeState = std::make_shared<TreeState>();
    std::atomic<uint32_t> m_scanGen{0};
    bool m_scanning = false;
    std::wstring m_pendingSelect;
    float m_pendingScroll = 0;
    bool m_keepScroll = false;

    // Bildbetrachter
    bool m_viewer = false, m_cropping = false, m_cropDragging = false, m_panning = false, m_fullByViewer = false;
    float m_vZoom = 1.f, m_vPanX = 0, m_vPanY = 0;
    POINT m_dragStart{};
    float m_panStartX = 0, m_panStartY = 0;
    double m_crop[4] = {0, 0, 1, 1};       // x0, y0, x1, y1 als Bruchteile des aufrechten Bildes
    double m_cropAnchor[2] = {0, 0};
    bool m_cropHas = false;

    // Diashow
    bool m_slide = false, m_slidePaused = false, m_slideFromViewer = false, m_slideToggledFull = false;
    bool m_slideCursorHidden = false;
    ULONGLONG m_slideLastMove = 0;               // GetTickCount64 der letzten Mausbewegung
    POINT m_slideMouse{};
    float m_uiAlpha = 1.f;                       // Deckkraft der Steuerleisten-Knoepfe
    D2D1_RECT_F rcSlideBar{};

    // Bildkorrektur: Vorschau-Pixel (aufrecht, BGRA) + aktuelle Regler; gedreht wird erst beim Zeichnen
    bool m_editing = false, m_editHasAlpha = false;
    imgops::EditParams m_ep;
    std::vector<BYTE> m_editSrc;
    UINT m_editW = 0, m_editH = 0;
    ComPtr<ID2D1Bitmap> m_editBmp;
    int m_dragSlider = -1;
    D2D1_RECT_F rcEditPanel{};

    // Detail
    imgops::ImageInfo m_info;
    bool m_infoOk = false;
    ComPtr<ID2D1Bitmap> m_preview;
    bool m_previewFailed = false;

    std::wstring m_status = L"Bereit";
    std::wstring m_pendingStatus;
    LPCWSTR m_cursor = IDC_ARROW;
    bool m_full = false;
    WINDOWPLACEMENT m_prevPlacement{sizeof(WINDOWPLACEMENT)};
    bool m_tracking = false;

    std::thread m_batch;
    std::atomic<bool> m_batchRunning{false};
    std::atomic<bool> m_batchCancel{false};
    BatchKind m_batchKind = BatchKind::StripCopy;
    std::atomic<size_t> m_bDone{0}, m_bTotal{0}, m_bOk{0}, m_bFail{0}, m_bSkip{0}, m_bGps{0};
    std::mutex m_bErrMutex;
    std::wstring m_bErr;
};

// ============================================================================
// Setup
// ============================================================================

bool App::Create(HINSTANCE hi, int show, const std::wstring& startArg) {
    LoadSettings(m_settings);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"DuncansPhotoCommander";
    wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1));
    if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Duncan's Photo Commander", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1500, 900, nullptr, nullptr, hi, this);
    if (!m_hwnd) return false;

    BOOL dark = TRUE;  // dunkle Titelleiste (DWMWA_USE_IMMERSIVE_DARK_MODE = 20)
    DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));

    k = GetDpiForWindow(m_hwnd) / 96.f;
    if (!CreateDeviceIndependent()) return false;
    CreateFormats();
    InitTreeRoots();
    m_loader.Start(m_hwnd, WM_APP_THUMBS);

    // Fenster auf die DPI des Monitors skalieren (Erstellung erfolgte mit 96-DPI-Groesse)
    SetWindowPos(m_hwnd, nullptr, 0, 0, (int)(1500 * k), (int)(900 * k), SWP_NOMOVE | SWP_NOZORDER);
    ShowWindow(m_hwnd, show);
    UpdateWindow(m_hwnd);

    DragAcceptFiles(m_hwnd, TRUE);

    // Startordner: Kommandozeile (Ordner oder Bilddatei), sonst zuletzt benutzter Ordner, sonst Bilder
    auto isDir = [](const std::wstring& p) {
        DWORD a = GetFileAttributesW(p.c_str());
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    };
    auto isFile = [](const std::wstring& p) {
        DWORD a = GetFileAttributesW(p.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    };
    if (!startArg.empty() && isDir(startArg)) {
        OpenFolder(startArg);
    } else if (!startArg.empty() && isFile(startArg) && imgops::IsSupportedImage(startArg)) {
        OpenFolder(ParentOf(startArg), startArg);
    } else if (!m_settings.lastFolder.empty() && isDir(m_settings.lastFolder)) {
        OpenFolder(m_settings.lastFolder);
    } else {
        std::wstring pics = KnownFolder(FOLDERID_Pictures);
        if (!pics.empty()) OpenFolder(pics);
    }
    return true;
}

bool App::CreateDeviceIndependent() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, d2d.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dw.GetAddressOf()))))
        return false;
    return true;
}

void App::CreateFormats() {
    auto make = [&](ComPtr<IDWriteTextFormat>& f, const wchar_t* font, float size, DWRITE_FONT_WEIGHT w,
                    DWRITE_TEXT_ALIGNMENT align) {
        f.Reset();
        dw->CreateTextFormat(font, nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size * k,
                             L"de-de", &f);
        f->SetTextAlignment(align);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        ComPtr<IDWriteInlineObject> ell;
        dw->CreateEllipsisTrimmingSign(f.Get(), &ell);
        f->SetTrimming(&trim, ell.Get());
    };
    make(fNormal, L"Segoe UI", 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fSmall, L"Segoe UI", 11, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fBold, L"Segoe UI", 13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fTitle, L"Segoe UI", 15, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fCenter, L"Segoe UI", 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    make(fIcon, L"Segoe UI Symbol", 20, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    make(fRight, L"Segoe UI", 11, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
    make(fTag, L"Segoe UI", 10, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void App::EnsureDevice() {
    if (rt) return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    // 96 DPI: 1 DIP == 1 Pixel, die Skalierung uebernimmt k.
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE), 96.f, 96.f);
    d2d->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(rc.right, rc.bottom)),
                                &rt);
    if (!rt) return;
    rt->CreateSolidColorBrush(D2D1::ColorF(0), &brush);
    // Nach Geraeteverlust neu anfordern
    m_lastReqRow = -1;
    RequestVisible();
}

void App::DiscardDevice() {
    brush.Reset();
    rt.Reset();
    for (auto& it : m_items) it.thumb.Reset();
    for (auto& it : m_hidden) it.thumb.Reset();  // Bitmaps gehoeren zum alten Render-Target
    m_preview.Reset();
    m_editBmp.Reset();
    m_loaded = 0;
    m_loader.NewGeneration();
}

// ============================================================================
// Layout
// ============================================================================

void App::Layout() {
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    float Wd = (float)cr.right, Ht = (float)cr.bottom;
    float tb = 52 * k, sb = 26 * k, lw = 270 * k, rw = 340 * k;
    if (Wd < 1150 * k) { lw = 220 * k; rw = 290 * k; }
    rcTop = {0, 0, Wd, tb};
    rcStatus = {0, Ht - sb, Wd, Ht};
    rcTree = {0, tb, lw, Ht - sb};
    rcDetail = {Wd - rw, tb, Wd, Ht - sb};
    rcGrid = {lw, tb, Wd - rw, Ht - sb};

    m_btns.clear();
    float bh = 34 * k, by = (tb - bh) / 2, x = 300 * k;
    auto add = [&](int id, const wchar_t* label, bool accent) {
        Btn b;
        b.id = id;
        b.label = label;
        b.accent = accent;
        float bw = (float)wcslen(label) * 8.f * k + 32 * k;
        b.rc = {x, by, x + bw, by + bh};
        x += bw + 8 * k;
        m_btns.push_back(b);
    };
    add(BTN_OPEN, L"Ordner öffnen", true);
    add(BTN_FULLSCREEN, L"Vollbild", false);
    add(BTN_BATCH, L"Stapelverarbeitung", false);
    Btn gear;
    gear.id = BTN_SETTINGS;
    gear.label = L"⚙";
    gear.icon = true;
    gear.rc = {Wd - 16 * k - 38 * k, (tb - 38 * k) / 2, Wd - 16 * k, (tb + 38 * k) / 2};
    m_btns.push_back(gear);

    // Detail-Panel
    float pad = 16 * k, dx = rcDetail.left + pad, dw_ = W(rcDetail) - 2 * pad, y = rcDetail.top + pad;
    rcPreview = {dx, y, dx + dw_, y + dw_ * 0.72f};
    y = rcPreview.bottom + 16 * k;
    rcActionsHead = {dx, y, dx + dw_, y + 22 * k};
    y = rcActionsHead.bottom + 6 * k;
    float bw = (dw_ - 8 * k) / 2, ah = 34 * k;
    struct A { int id; const wchar_t* t; bool danger; } acts[] = {
        {BTN_ROT_L, L"↺  Links drehen", false}, {BTN_ROT_R, L"↻  Rechts drehen", false},
        {BTN_CROP, L"Zuschneiden", false}, {BTN_STRIP, L"EXIF entfernen", true}, {BTN_EDIT, L"Bearbeiten …", false}};
    for (int i = 0; i < 5; ++i) {
        Btn b;
        b.id = acts[i].id;
        b.label = acts[i].t;
        b.danger = acts[i].danger;
        b.needsSelection = true;
        float bx = dx + (i % 2) * (bw + 8 * k), byy = y + (i / 2) * (ah + 8 * k);
        b.rc = {bx, byy, i == 4 ? dx + dw_ : bx + bw, byy + ah};
        m_btns.push_back(b);
    }
    m_infoTop = y + 3 * (ah + 8 * k) + 10 * k;

    LayoutViewer();
}

// ============================================================================
// Zeichnen
// ============================================================================

void App::Paint() {
    EnsureDevice();
    if (!rt) return;
    ConsumeThumbs();
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(Theme::Bg));
    if (m_viewer) {
        DrawViewer();
    } else {
        DrawGrid();
        DrawTree();
        DrawDetails();
        DrawTopbar();
        DrawStatus();
    }
    HRESULT hr = rt->EndDraw();
    if (hr == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
        DiscardDevice();
        Invalidate();
    }
}

void App::DrawFit(ID2D1Bitmap* b, const D2D1_RECT_F& box) {
    D2D1_SIZE_F sz = b->GetSize();
    float s = std::min(W(box) / sz.width, H(box) / sz.height);
    float w = sz.width * s, h = sz.height * s;
    float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
    rt->DrawBitmap(b, D2D1::RectF(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2), 1.f,
                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

bool App::BtnEnabled(const Btn& b) const {
    const bool has = m_sel >= 0 && m_sel < (int)m_items.size();
    switch (b.id) {
        case VB_PREV: return has && m_sel > 0;
        case VB_NEXT: return has && m_sel + 1 < (int)m_items.size();
        case VB_CROP_OK: return m_cropHas;
        case BTN_ROT_L:
        case BTN_ROT_R:
        case BTN_CROP:
        case BTN_EDIT: return has && imgops::CanReencode(m_items[m_sel].path);
        case VB_SLIDE_PREV:
        case VB_SLIDE_PLAY:
        case VB_SLIDE_NEXT:
        case VB_SLIDE_CLOSE: return SlideOverlayAlpha() > 0.05f;
        case VB_EDIT_SAVE:
        case VB_EDIT_SAVEAS:
        case VB_EDIT_RESET: return m_editing && !m_ep.IsIdentity();
        case BTN_STRIP: return has && imgops::CanStripMetadata(m_items[m_sel].path);
        default: return !b.needsSelection || has;
    }
}

void App::DrawButton(const Btn& b) {
    bool hover = m_hoverBtn == b.id;
    bool enabled = BtnEnabled(b);
    unsigned bg = b.accent ? Theme::Accent : (hover && enabled ? Theme::CardHover : Theme::Card);
    float a = (enabled ? 1.f : 0.45f) * m_uiAlpha;
    FillRR(b.rc, 6 * k, bg, a);
    if (b.accent && hover && enabled) FillRR(b.rc, 6 * k, 0xFFFFFF, 0.12f * m_uiAlpha);
    unsigned fg = b.accent ? 0xFFFFFF : (b.danger ? Theme::Danger : Theme::Text);
    Txt(b.label, b.rc, b.icon ? fIcon.Get() : fCenter.Get(), fg, a);
}

void App::DrawScrollbar(const D2D1_RECT_F& area, float scroll, float content) {
    float view = H(area);
    if (content <= view + 1) return;
    float trackH = view - 8 * k, thumbH = std::max(30 * k, trackH * view / content);
    float t = scroll / (content - view);
    float y = area.top + 4 * k + (trackH - thumbH) * t;
    FillRR({area.right - 8 * k, y, area.right - 4 * k, y + thumbH}, 2 * k, Theme::TextDim, 0.45f);
}

void App::DrawTopbar() {
    Fill(rcTop, Theme::Topbar);
    Fill({0, rcTop.bottom - 1, rcTop.right, rcTop.bottom}, Theme::Border);
    FillRR({18 * k, 17 * k, 36 * k, 35 * k}, 4 * k, Theme::Accent);  // Platzhalter-Logo
    Txt(L"Photo Commander", {46 * k, 0, 290 * k, rcTop.bottom}, fTitle.Get(), Theme::Text);
    for (auto& b : m_btns)
        if (b.rc.top < rcTop.bottom) DrawButton(b);
}

void App::DrawTree() {
    Fill(rcTree, Theme::Panel);
    Fill({rcTree.right - 1, rcTree.top, rcTree.right, rcTree.bottom}, Theme::Border);
    rt->PushAxisAlignedClip(rcTree, D2D1_ANTIALIAS_MODE_ALIASED);
    Txt(L"ORDNER", {rcTree.left + 14 * k, rcTree.top, rcTree.right, rcTree.top + 30 * k}, fSmall.Get(), Theme::TextDim);
    float top = rcTree.top + 30 * k, rh = kRowH * k;
    D2D1_RECT_F list{rcTree.left, top, rcTree.right, rcTree.bottom};
    rt->PushAxisAlignedClip(list, D2D1_ANTIALIAS_MODE_ALIASED);
    for (size_t i = 0; i < m_rows.size(); ++i) {
        float y = top + i * rh - m_treeScroll;
        if (y + rh < top || y > rcTree.bottom) continue;
        TreeNode* n = m_rows[i];
        D2D1_RECT_F r{rcTree.left + 6 * k, y + 1, rcTree.right - 8 * k, y + rh - 1};
        if (n == m_selNode) FillRR(r, 5 * k, Theme::Accent, 0.22f);
        else if ((int)i == m_hoverRow) FillRR(r, 5 * k, Theme::Card);
        float x = rcTree.left + 12 * k + n->depth * 16 * k;
        if (n->loading) Txt(L"…", {x, y, x + 16 * k, y + rh}, fCenter.Get(), Theme::TextDim);
        else if (!n->empty) Txt(n->expanded ? L"▾" : L"▸", {x, y, x + 16 * k, y + rh}, fCenter.Get(), Theme::TextDim);
        Txt(n->name, {x + 18 * k, y, rcTree.right - 12 * k, y + rh}, fNormal.Get(),
            n == m_selNode ? Theme::Text : 0xC3C8D2);
    }
    rt->PopAxisAlignedClip();
    DrawScrollbar(list, m_treeScroll, m_rows.size() * rh);
    rt->PopAxisAlignedClip();
}

void App::DrawGrid() {
    Fill(rcGrid, Theme::Bg);
    rt->PushAxisAlignedClip(rcGrid, D2D1_ANTIALIAS_MODE_ALIASED);
    if (m_items.empty()) {
        Txt(m_folder.empty()   ? L"Ordner öffnen, um Bilder anzuzeigen"
            : m_gpsOnly && !m_hidden.empty() ? L"Keine Bilder mit Standortdaten in diesem Ordner (Filter aktiv)"
                                             : L"Keine Bilder in diesem Ordner",
            rcGrid, fCenter.Get(), Theme::TextDim);
        rt->PopAxisAlignedClip();
        return;
    }
    GridM g = Metrics();
    const bool multi = SelCount() > 1;
    int firstRow = std::max(0, (int)std::floor(m_gridScroll / g.cellH));
    int lastRow = std::min(g.rows - 1, (int)std::floor((m_gridScroll + H(rcGrid)) / g.cellH));
    for (int r = firstRow; r <= lastRow; ++r) {
        for (int c = 0; c < g.cols; ++c) {
            int idx = r * g.cols + c;
            if (idx >= (int)m_items.size()) break;
            float x = rcGrid.left + g.pad + c * g.cellW, y = rcGrid.top + g.pad + r * g.cellH - m_gridScroll;
            D2D1_RECT_F card{x, y, x + g.thumb, y + g.thumb};
            Item& it = m_items[idx];
            bool sel = it.selected, hov = idx == m_hoverItem;
            FillRR(card, 8 * k, hov || sel ? Theme::CardHover : Theme::Card);
            if (it.thumb) DrawFit(it.thumb.Get(), Inset(card, 6 * k));
            else if (it.failed) Txt(L"⚠", card, fCenter.Get(), Theme::Warn);
            else FillRR(Inset(card, g.thumb * 0.35f), 6 * k, Theme::Border, 0.7f);  // Platzhalter
            if (sel) StrokeRR(card, 8 * k, Theme::Accent, 2 * k);
            if (it.hasGps) {  // Standortdaten vorhanden: vor dem Teilen bereinigen
                D2D1_RECT_F tag{x + g.thumb - 38 * k, y + 6 * k, x + g.thumb - 6 * k, y + 22 * k};
                FillRR(tag, 4 * k, Theme::Danger);
                Txt(L"GPS", tag, fTag.Get(), 0xFFFFFF);
            }
            if (m_dupMode && it.dupGroup > 0) {
                const bool keep = it.dupRank == 0;
                const std::wstring label = L"#" + std::to_wstring(it.dupGroup) +
                                           (keep ? L" behalten" : (it.dupExact ? L" identisch" : L" ähnlich"));
                D2D1_RECT_F tag{x + 6 * k, y + g.thumb - 22 * k, x + std::min(g.thumb - 6 * k, 104 * k), y + g.thumb - 6 * k};
                FillRR(tag, 4 * k, keep ? Theme::Accent : Theme::Warn);
                Txt(label, tag, fTag.Get(), 0xFFFFFF);
            }
            if (sel && multi) {
                D2D1_RECT_F badge{x + 6 * k, y + 6 * k, x + 24 * k, y + 24 * k};
                FillRR(badge, 9 * k, Theme::Accent);
                Txt(L"✓", badge, fCenter.Get(), 0xFFFFFF);
            }
            Txt(it.name, {x, y + g.thumb + 2 * k, x + g.thumb, y + g.thumb + 24 * k}, fSmall.Get(),
                sel ? Theme::Text : Theme::TextDim);
        }
    }
    DrawScrollbar(rcGrid, m_gridScroll, g.content);
    rt->PopAxisAlignedClip();
}

void App::DrawDetails() {
    Fill(rcDetail, Theme::Panel);
    Fill({rcDetail.left, rcDetail.top, rcDetail.left + 1, rcDetail.bottom}, Theme::Border);
    rt->PushAxisAlignedClip(rcDetail, D2D1_ANTIALIAS_MODE_ALIASED);

    FillRR(rcPreview, 8 * k, Theme::Bg);
    if (m_preview) DrawFit(m_preview.Get(), Inset(rcPreview, 4 * k));
    else if (m_sel >= 0) Txt(m_previewFailed ? L"Keine Vorschau" : L"Lädt…", rcPreview, fCenter.Get(), Theme::TextDim);
    else Txt(L"Kein Bild gewählt", rcPreview, fCenter.Get(), Theme::TextDim);

    Txt(L"SCHNELLAKTIONEN", rcActionsHead, fSmall.Get(), Theme::TextDim);
    for (auto& b : m_btns)
        if (b.rc.top >= rcTop.bottom) DrawButton(b);

    if (m_sel >= 0 && m_infoOk) {
        float x = rcDetail.left + 16 * k, wd = W(rcDetail) - 32 * k, y = m_infoTop, rh = 24 * k, lw = 98 * k;
        auto row = [&](const std::wstring& l, const std::wstring& v, unsigned vc = Theme::Text) {
            if (y + rh > rcDetail.bottom) return;
            Txt(l, {x, y, x + lw, y + rh}, fSmall.Get(), Theme::TextDim);
            Txt(v, {x + lw, y, x + wd, y + rh}, fNormal.Get(), vc);
            y += rh;
        };
        auto head = [&](const wchar_t* t) {
            Fill({x, y + 2 * k, x + wd, y + 3 * k}, Theme::Border);
            y += 8 * k;
            Txt(t, {x, y, x + wd, y + 20 * k}, fSmall.Get(), Theme::TextDim);
            y += 22 * k;
        };
        head(L"DATEI");
        if (SelCount() > 1) {
            ULONGLONG total = 0;
            for (auto& it : m_items)
                if (it.selected) total += it.size;
            row(L"Auswahl", std::to_wstring(SelCount()) + L" Bilder (" + FormatSize(total) + L")", Theme::Accent);
        }
        row(L"Name", m_items[m_sel].name);
        wchar_t res[64];
        swprintf_s(res, L"%u × %u  (%.1f MP)", m_info.width, m_info.height, m_info.width * (double)m_info.height / 1e6);
        row(L"Auflösung", res);
        row(L"Dateigröße", FormatSize(m_info.fileSize));
        row(L"Erstellt", FormatTime(m_info.created));
        row(L"Geändert", FormatTime(m_info.modified));
        head(L"EXIF");
        if (m_info.rows.empty()) row(L"", L"Keine EXIF-Daten", Theme::TextDim);
        for (auto& r : m_info.rows) row(r.label, r.value, r.sensitive ? Theme::Danger : Theme::Text);
        if (m_info.hasGps) {
            y += 4 * k;
            Txt(L"⚠ Enthält Standortdaten – vor dem Teilen bereinigen.", {x, y, x + wd, y + 22 * k}, fSmall.Get(),
                Theme::Warn);
        }
    }
    rt->PopAxisAlignedClip();
}

void App::DrawStatus() {
    Fill(rcStatus, Theme::Topbar);
    Fill({0, rcStatus.top, rcStatus.right, rcStatus.top + 1}, Theme::Border);
    if (m_batchRunning && m_bTotal > 0) {
        float f = std::min(1.f, (float)m_bDone / (float)m_bTotal);
        Fill({0, rcStatus.top, rcStatus.right * f, rcStatus.top + 3 * k}, Theme::Accent);
    }
    Txt(m_status, {12 * k, rcStatus.top, rcStatus.right * 0.6f, rcStatus.bottom}, fSmall.Get(), Theme::TextDim);
    wchar_t buf[128];
    const int sc = SelCount();
    if (sc > 1)
        swprintf_s(buf, L"%d von %llu ausgewählt   ·   %llu Thumbnails geladen", sc, (unsigned long long)m_items.size(),
                   (unsigned long long)m_loaded);
    else
        swprintf_s(buf, L"%llu Bilder   ·   %llu Thumbnails geladen", (unsigned long long)m_items.size(),
                   (unsigned long long)m_loaded);
    D2D1_RECT_F r{rcStatus.right * 0.6f, rcStatus.top, rcStatus.right - 12 * k, rcStatus.bottom};
    Txt(buf, r, fRight.Get(), Theme::TextDim);
}

// ============================================================================
// Ordnerbaum
// ============================================================================

void App::InitTreeRoots() {
    auto addRoot = [&](const std::wstring& label, const std::wstring& path) {
        if (path.empty()) return;
        auto n = std::make_unique<TreeNode>();
        n->name = label;
        n->path = path;
        m_roots.push_back(std::move(n));
    };
    addRoot(L"Bilder", KnownFolder(FOLDERID_Pictures));
    addRoot(L"Desktop", KnownFolder(FOLDERID_Desktop));
    addRoot(L"Dokumente", KnownFolder(FOLDERID_Documents));
    addRoot(L"Downloads", KnownFolder(FOLDERID_Downloads));
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i)
        if (drives & (1u << i)) {
            std::wstring d = std::wstring(1, static_cast<wchar_t>(L'A' + i)) + L":\\";
            addRoot(d, d);
        }
    Flatten();
}

// Unterordner im Hintergrund lesen: Netzlaufwerke und grosse Ordner blockieren die Oberflaeche nicht.
void App::LoadChildrenAsync(TreeNode* n) {
    if (n->loaded || n->loading) return;
    n->loading = true;
    std::shared_ptr<TreeState> st = m_treeState;
    HWND hwnd = m_hwnd;
    std::wstring base = n->path;
    if (!base.empty() && base.back() != L'\\') base += L'\\';
    std::thread([st, hwnd, n, base] {
        TreeResult res;
        res.node = n;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW((base + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                    FIND_FIRST_EX_LARGE_FETCH);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
                if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                res.names.push_back(fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        {
            std::lock_guard<std::mutex> lk(st->mutex);
            st->done.push_back(std::move(res));
        }
        PostMessageW(hwnd, WM_APP_TREE, 0, 0);
    }).detach();
}

void App::OnTreeLoaded() {
    std::vector<TreeResult> done;
    {
        std::lock_guard<std::mutex> lk(m_treeState->mutex);
        done.swap(m_treeState->done);
    }
    for (auto& r : done) {
        TreeNode* n = r.node;  // Knoten werden waehrend der Programmlaufzeit nie entfernt
        std::wstring base = n->path;
        if (!base.empty() && base.back() != L'\\') base += L'\\';
        for (auto& name : r.names) {
            auto c = std::make_unique<TreeNode>();
            c->name = name;
            c->path = base + name;
            c->depth = n->depth + 1;
            c->parent = n;
            n->children.push_back(std::move(c));
        }
        std::sort(n->children.begin(), n->children.end(),
                  [](auto& a, auto& b) { return StrCmpLogicalW(a->name.c_str(), b->name.c_str()) < 0; });
        n->empty = n->children.empty();
        n->loaded = true;
        n->loading = false;
        if (n->wantExpand && !n->empty) n->expanded = true;
        n->wantExpand = false;
    }
    Flatten();
    Invalidate();
}

void App::Flatten() {
    m_rows.clear();
    auto rec = [&](auto&& self, TreeNode* n) -> void {
        m_rows.push_back(n);
        if (n->expanded)
            for (auto& c : n->children) self(self, c.get());
    };
    for (auto& r : m_roots) rec(rec, r.get());
}

void App::ToggleNode(TreeNode* n) {
    if (!n->loaded) {  // erst lesen, aufgeklappt wird beim Eintreffen des Ergebnisses
        n->wantExpand = true;
        LoadChildrenAsync(n);
        Invalidate();
        return;
    }
    if (!n->empty) n->expanded = !n->expanded;
    Flatten();
}

// ============================================================================
// Ordner & Grid
// ============================================================================

void App::BrowseForFolder() {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (FAILED(dlg->Show(m_hwnd))) return;
    ComPtr<IShellItem> item;
    PWSTR p = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
        OpenFolder(p);
        CoTaskMemFree(p);
    }
}

void App::OpenFolder(const std::wstring& path, const std::wstring& selectPath, bool keepScroll) {
    keepScroll = keepScroll && _wcsicmp(path.c_str(), m_folder.c_str()) == 0;
    m_folder = path;
    m_pendingSelect = selectPath;
    m_keepScroll = keepScroll;
    m_pendingScroll = m_gridTarget;
    if (!keepScroll) {
        if (m_viewer) ExitViewer();
        m_items.clear();
        m_hidden.clear();
        if (m_dup) m_dup->pr.cancel = true;  // Duplikatsuche gilt nur fuer den alten Ordner
        m_dupRunning = m_dupMode = false;
        m_loaded = 0;
        m_sel = m_hoverItem = m_anchor = -1;
        m_preview.Reset();
        m_infoOk = false;
        m_gridScroll = m_gridTarget = 0;
        m_lastReqRow = -1;
        m_loader.NewGeneration();
    }
    if (m_selNode && _wcsicmp(m_selNode->path.c_str(), path.c_str()) != 0) m_selNode = nullptr;
    m_settings.lastFolder = path;
    SaveSettingsNow();

    const uint32_t gen = ++m_scanGen;
    m_scanning = true;
    SetStatus(L"Lese Ordner …");
    SetWindowTextW(m_hwnd, (L"Duncan's Photo Commander – " + path).c_str());

    // Auflisten im Hintergrund: grosse Ordner oder Netzlaufwerke blockieren das Fenster nicht
    std::shared_ptr<ScanState> st = m_scan;
    HWND hwnd = m_hwnd;
    std::thread([st, hwnd, path, gen] {
        std::vector<FileEntry> files;
        WIN32_FIND_DATAW fd;
        std::wstring base = path;
        if (!base.empty() && base.back() != L'\\') base += L'\\';
        HANDLE h = FindFirstFileExW((base + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                    FIND_FIRST_EX_LARGE_FETCH);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                if (!imgops::IsSupportedImage(fd.cFileName)) continue;
                FileEntry e;
                e.name = fd.cFileName;
                e.path = base + fd.cFileName;
                e.size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                e.mtime = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
                files.push_back(std::move(e));
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        {
            std::lock_guard<std::mutex> lk(st->mutex);
            if (gen < st->gen) return;  // ein neueres Listing wurde bereits abgeliefert
            st->gen = gen;
            st->files = std::move(files);
            st->ready = true;
        }
        PostMessageW(hwnd, WM_APP_SCAN, 0, 0);
    }).detach();
    Invalidate();
}

void App::OnScanDone() {
    std::vector<FileEntry> files;
    {
        std::lock_guard<std::mutex> lk(m_scan->mutex);
        if (!m_scan->ready || m_scan->gen != m_scanGen.load()) return;
        files = std::move(m_scan->files);
        m_scan->ready = false;
    }
    m_scanning = false;

    // Bereits geladene Thumbnails unveraendert gebliebener Dateien uebernehmen (kein Neuladen nach Strippen, Sortieren ...)
    const std::wstring oldFocus = (m_sel >= 0 && m_sel < (int)m_items.size()) ? m_items[m_sel].path : L"";
    std::vector<Item> old;
    old.swap(m_items);
    for (auto& h : m_hidden) old.push_back(std::move(h));  // ausgeblendete Bilder behalten ihre Kurzinfo
    m_hidden.clear();
    std::unordered_map<std::wstring, size_t> oldIdx;
    for (size_t i = 0; i < old.size(); ++i) oldIdx[old[i].path] = i;
    m_items.reserve(files.size());
    m_loaded = 0;
    for (auto& f : files) {
        Item it;
        it.path = std::move(f.path);
        it.name = std::move(f.name);
        it.size = f.size;
        it.mtime = f.mtime;
        auto found = oldIdx.find(it.path);
        if (found != oldIdx.end()) {
            Item& o = old[found->second];
            if (o.size == it.size && o.mtime == it.mtime) {
                it.thumb = std::move(o.thumb);
                it.failed = o.failed;
                it.metaDone = o.metaDone;
                it.hasGps = o.hasGps;
                it.taken = o.taken;
            }
            it.dupGroup = o.dupGroup;
            it.dupRank = o.dupRank;
            it.dupExact = o.dupExact;
            it.selected = o.selected;
        }
        if (it.thumb) ++m_loaded;
        m_items.push_back(std::move(it));
    }
    old.clear();
    m_loader.NewGeneration();  // Indizes haben sich geaendert -> laufende Anfragen verwerfen
    m_lastReqRow = -1;
    m_hoverItem = -1;
    m_sel = m_anchor = -1;
    m_preview.Reset();
    m_infoOk = false;
    ApplyFilter();  // Filter anwenden und sortieren

    const std::wstring& wantPath = !m_pendingSelect.empty() ? m_pendingSelect : (m_keepScroll ? oldFocus : std::wstring());
    int want = -1;
    for (int i = 0; i < (int)m_items.size(); ++i)
        if (_wcsicmp(m_items[i].path.c_str(), wantPath.c_str()) == 0) { want = i; break; }
    if (want < 0 && m_viewer && !m_items.empty()) want = 0;
    if (!m_pendingSelect.empty()) {
        Select(want);
    } else if (want >= 0) {
        if (!m_items[want].selected) m_items[want].selected = true;
        m_anchor = want;
        Focus(want);
    }
    if (m_viewer && m_items.empty()) ExitViewer();

    if (m_keepScroll) SetGridScroll(m_pendingScroll, true);
    SetStatus(!m_pendingStatus.empty() ? m_pendingStatus : FolderStatus());
    m_pendingStatus.clear();
    m_pendingSelect.clear();
    RequestVisible();
    StartMetaPass();
    Invalidate();
}

bool App::Visible(const Item& it) const {
    if (m_gpsOnly && it.metaDone && !it.hasGps) return false;
    if (m_dupMode && it.dupGroup == 0) return false;
    return true;
}

std::wstring App::FolderStatus() const {
    std::wstring s = m_folder + L"  ·  " + std::to_wstring(m_items.size()) + L" Bilder";
    if (m_dupMode) s += L"  ·  Duplikatansicht (Esc beendet)";
    else if (m_gpsOnly) s += L"  ·  nur mit Standortdaten (" + std::to_wstring(m_hidden.size()) + L" ausgeblendet)";
    return s;
}

void App::ApplyFilter() {
    std::vector<Item> all;
    all.reserve(m_items.size() + m_hidden.size());
    for (auto& it : m_items) all.push_back(std::move(it));
    for (auto& it : m_hidden) all.push_back(std::move(it));
    m_items.clear();
    m_hidden.clear();
    for (auto& it : all) {
        if (Visible(it)) {
            m_items.push_back(std::move(it));
        } else {
            it.selected = false;
            m_hidden.push_back(std::move(it));
        }
    }
    SortItems();
    m_loader.NewGeneration();  // Indizes haben sich geaendert
    m_lastReqRow = -1;
    m_hoverItem = -1;
    m_loaded = 0;
    for (auto& it : m_items)
        if (it.thumb) ++m_loaded;
    if (m_sel < 0) {
        m_preview.Reset();
        m_infoOk = false;
    }
    SetGridScroll(m_gridScroll, true);
    RequestVisible();
    Invalidate();
}

void App::StartMetaPass() {
    if (m_meta) m_meta->cancel = true;
    std::vector<std::wstring> todo;
    for (auto& it : m_items)
        if (!it.metaDone) todo.push_back(it.path);
    for (auto& it : m_hidden)
        if (!it.metaDone) todo.push_back(it.path);
    if (todo.empty()) {
        m_meta.reset();
        return;
    }
    auto st = std::make_shared<MetaState>();
    st->todo = std::move(todo);
    m_meta = st;
    HWND hwnd = m_hwnd;
    // Pro Bild ein Decoder: auf mehreren Kernen ist das bei grossen Ordnern merklich schneller als ein Thread
    const unsigned hw = std::thread::hardware_concurrency();
    const unsigned nWorkers = std::max(1u, std::min(3u, hw > 1 ? hw - 1 : 1u));
    st->workersLeft = nWorkers;
    for (unsigned t = 0; t < nWorkers; ++t) {
        std::thread([st, hwnd] {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            size_t sinceFlush = 0;
            for (;;) {
                const size_t i = st->next.fetch_add(1);
                if (i >= st->todo.size() || st->cancel) break;
                imgops::QuickMeta qm;
                imgops::ReadQuickMeta(st->todo[i], qm);  // bei Fehlern bleiben die Standardwerte (kein Datum, kein GPS)
                {
                    std::lock_guard<std::mutex> lk(st->mutex);
                    st->results.push_back({st->todo[i], qm.taken, qm.hasGps});
                }
                if (++sinceFlush >= 32) {
                    sinceFlush = 0;
                    if (!st->pending.exchange(true)) PostMessageW(hwnd, WM_APP_META, 0, 0);
                }
            }
            if (SUCCEEDED(hr)) CoUninitialize();
            if (st->workersLeft.fetch_sub(1) == 1) {  // der letzte Worker meldet "fertig"
                st->done = true;
                PostMessageW(hwnd, WM_APP_META, 0, 0);
            }
        }).detach();
    }
}

void App::OnMeta() {
    auto st = m_meta;
    if (!st || st->cancel) return;
    const bool done = st->done;  // vor dem Abholen lesen: ist es true, liegen alle Ergebnisse schon bereit
    std::vector<MetaResult> res;
    {
        std::lock_guard<std::mutex> lk(st->mutex);
        res.swap(st->results);
    }
    st->pending = false;
    std::unordered_map<std::wstring, Item*> byPath;
    byPath.reserve(m_items.size() + m_hidden.size());
    for (auto& it : m_items) byPath[it.path] = &it;
    for (auto& it : m_hidden) byPath[it.path] = &it;
    for (auto& r : res) {
        auto f = byPath.find(r.path);
        if (f == byPath.end()) continue;
        f->second->metaDone = true;
        f->second->hasGps = r.hasGps;
        f->second->taken = r.taken;
    }
    if (!done) {
        Invalidate();
        return;
    }
    m_meta.reset();
    if (m_viewer) {
        m_resortPending = true;  // Reihenfolge nicht unter dem geoeffneten Bild aendern
    } else if (m_settings.sortBy == 3 || m_gpsOnly) {
        ApplyFilter();
        if (m_sel >= 0) EnsureVisible(m_sel);
    }
    if (!m_batchRunning && !m_dupRunning) SetStatus(FolderStatus());
    Invalidate();
}

void App::StartDupSearch() {
    if (m_batchRunning || m_dupRunning) return;
    auto st = std::make_shared<DupState>();
    for (auto& it : m_items) st->files.push_back({it.path, it.size});
    for (auto& it : m_hidden) st->files.push_back({it.path, it.size});
    if (st->files.size() < 2) {
        SetStatus(L"Zu wenige Bilder für eine Duplikatsuche.");
        return;
    }
    m_dup = st;
    m_dupRunning = true;
    SetStatus(L"Suche doppelte Bilder … (Esc bricht ab)");
    HWND hwnd = m_hwnd;
    std::thread([st, hwnd] {
        auto tick = [&] {
            const ULONGLONG now = GetTickCount64();
            if (now - st->lastPost.load() < 150) return;
            st->lastPost = now;
            if (!st->pending.exchange(true)) PostMessageW(hwnd, WM_APP_DUP, 0, 0);
        };
        std::vector<dup::Group> groups = dup::Find(st->files, st->pr, tick);
        {
            std::lock_guard<std::mutex> lk(st->mutex);
            st->groups = std::move(groups);
        }
        st->done = true;
        PostMessageW(hwnd, WM_APP_DUP, 0, 0);
    }).detach();
}

void App::OnDup() {
    auto st = m_dup;
    if (!st || !m_dupRunning) return;
    st->pending = false;
    if (!st->done) {
        const wchar_t* what = st->pr.stage == 1 ? L"Vergleiche Dateien" : L"Vergleiche Bildinhalte";
        SetStatus(std::wstring(what) + L" … " + std::to_wstring((unsigned long long)st->pr.done.load()) + L" / " +
                  std::to_wstring((unsigned long long)st->pr.total.load()) + L"  (Esc bricht ab)");
        Invalidate();
        return;
    }
    m_dupRunning = false;
    if (st->pr.cancel) {
        SetStatus(L"Duplikatsuche abgebrochen");
        Invalidate();
        return;
    }
    std::vector<dup::Group> groups;
    {
        std::lock_guard<std::mutex> lk(st->mutex);
        groups.swap(st->groups);
    }
    if (m_viewer) ExitViewer();
    ClearDupMarks();
    std::unordered_map<std::wstring, Item*> byPath;
    for (auto& it : m_items) byPath[it.path] = &it;
    for (auto& it : m_hidden) byPath[it.path] = &it;

    int gid = 0;
    size_t extras = 0;
    for (auto& g : groups) {
        std::vector<Item*> mem;
        for (size_t idx : g.members) {
            auto f = byPath.find(st->files[idx].path);
            if (f != byPath.end()) mem.push_back(f->second);
        }
        if (mem.size() < 2) continue;
        // Behalten wird die groesste Datei (bei Gleichstand die aelteste, dann nach Name); Rest nach Name
        std::sort(mem.begin(), mem.end(), [](const Item* a, const Item* b) {
            if (a->size != b->size) return a->size > b->size;
            if (a->mtime != b->mtime) return a->mtime < b->mtime;
            return StrCmpLogicalW(a->name.c_str(), b->name.c_str()) < 0;
        });
        ++gid;
        for (size_t i = 0; i < mem.size(); ++i) {
            mem[i]->dupGroup = gid;
            mem[i]->dupRank = (int)i;
            mem[i]->dupExact = g.exact;
            mem[i]->selected = false;
        }
        extras += mem.size() - 1;
    }
    if (gid == 0) {
        SetStatus(L"Keine doppelten oder ähnlichen Bilder gefunden");
        Invalidate();
        return;
    }
    for (auto& it : m_items) it.selected = false;
    m_dupMode = true;
    ApplyFilter();
    // Alle ausser dem jeweils behaltenen Bild vormarkieren: Entf schiebt sie in den Papierkorb
    int first = -1;
    for (int i = 0; i < (int)m_items.size(); ++i) {
        if (m_items[i].dupRank > 0) {
            m_items[i].selected = true;
            if (first < 0) first = i;
        }
    }
    m_sel = m_anchor = -1;
    if (first >= 0) {
        m_anchor = first;
        Focus(first);
        EnsureVisible(first);
    }
    SetStatus(std::to_wstring(gid) + (gid == 1 ? L" Gruppe, " : L" Gruppen, ") + std::to_wstring(extras) +
              (extras == 1 ? L" Duplikat vorgewählt" : L" Duplikate vorgewählt") +
              L" – Entf verschiebt sie in den Papierkorb, Esc beendet die Ansicht");
    Invalidate();
}

void App::ClearDupMarks() {
    for (auto* list : {&m_items, &m_hidden})
        for (auto& it : *list) it.dupGroup = it.dupRank = 0, it.dupExact = false;
}

void App::EndDupMode() {
    if (!m_dupMode) return;
    m_dupMode = false;
    ClearDupMarks();
    ApplyFilter();
    if (m_sel >= 0) EnsureVisible(m_sel);
    SetStatus(FolderStatus());
}

void App::SortItems() {
    const std::wstring focus = (m_sel >= 0 && m_sel < (int)m_items.size()) ? m_items[m_sel].path : L"";
    const std::wstring anchor = (m_anchor >= 0 && m_anchor < (int)m_items.size()) ? m_items[m_anchor].path : L"";
    const int by = m_settings.sortBy;
    const bool desc = m_settings.sortDesc;
    const bool dupView = m_dupMode;
    std::sort(m_items.begin(), m_items.end(), [by, desc, dupView](const Item& a, const Item& b) {
        if (dupView) {  // Duplikatansicht: Gruppen zusammenhaengend, das behaltene Bild zuerst
            if (a.dupGroup != b.dupGroup) return a.dupGroup < b.dupGroup;
            if (a.dupRank != b.dupRank) return a.dupRank < b.dupRank;
        }
        int c = 0;
        if (by == 1) c = a.mtime < b.mtime ? -1 : (a.mtime > b.mtime ? 1 : 0);
        else if (by == 2) c = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
        else if (by == 3) {  // Aufnahmedatum, ohne EXIF-Zeit gilt das Aenderungsdatum
            const ULONGLONG ta = a.taken ? a.taken : a.mtime, tb = b.taken ? b.taken : b.mtime;
            c = ta < tb ? -1 : (ta > tb ? 1 : 0);
        }
        if (c == 0) c = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
        return desc ? c > 0 : c < 0;
    });
    m_sel = m_anchor = -1;
    for (int i = 0; i < (int)m_items.size(); ++i) {
        if (!focus.empty() && m_items[i].path == focus) m_sel = i;
        if (!anchor.empty() && m_items[i].path == anchor) m_anchor = i;
    }
}

GridM App::Metrics() const {
    GridM g;
    g.pad = 14 * k;
    g.thumb = m_settings.thumbSize * k;
    g.cellW = g.thumb + 12 * k;
    g.cellH = g.thumb + 36 * k;
    g.cols = std::max(1, (int)((W(rcGrid) - g.pad) / g.cellW));
    g.rows = ((int)m_items.size() + g.cols - 1) / g.cols;
    g.content = g.pad * 2 + g.rows * g.cellH;
    return g;
}

int App::HitGrid(float x, float y) const {
    if (!In(rcGrid, x, y) || m_items.empty()) return -1;
    GridM g = Metrics();
    float fx = x - rcGrid.left - g.pad, fy = y - rcGrid.top - g.pad + m_gridScroll;
    if (fx < 0 || fy < 0) return -1;
    int c = (int)(fx / g.cellW), r = (int)(fy / g.cellH);
    if (c >= g.cols) return -1;
    if (fx - c * g.cellW > g.thumb || fy - r * g.cellH > g.thumb + 24 * k) return -1;
    int idx = r * g.cols + c;
    return idx < (int)m_items.size() ? idx : -1;
}

void App::SetGridScroll(float target, bool instant) {
    GridM g = Metrics();
    float maxScroll = std::max(0.f, g.content - H(rcGrid));
    m_gridTarget = std::clamp(target, 0.f, maxScroll);
    if (instant) m_gridScroll = m_gridTarget;
    else SetTimer(m_hwnd, TIMER_SCROLL, 15, nullptr);
}

void App::EnsureVisible(int idx) {
    GridM g = Metrics();
    float top = g.pad + (idx / g.cols) * g.cellH, bottom = top + g.cellH;
    if (top < m_gridTarget) SetGridScroll(top - g.pad);
    else if (bottom > m_gridTarget + H(rcGrid)) SetGridScroll(bottom - H(rcGrid) + g.pad);
}

void App::Focus(int idx) {
    if (idx < 0 || idx >= (int)m_items.size()) idx = -1;
    if (idx == m_sel) return;
    m_sel = idx;
    m_preview.Reset();
    m_previewFailed = false;
    m_infoOk = false;
    if (idx >= 0) {
        m_infoOk = imgops::ReadImageInfo(m_items[idx].path, m_info);
        EnsureVisible(idx);
        SetStatus(m_items[idx].path);
    }
    RequestVisible();
    Invalidate();
}

void App::Select(int idx) {
    if (idx < 0 || idx >= (int)m_items.size()) idx = -1;
    for (auto& it : m_items) it.selected = false;
    if (idx >= 0) m_items[idx].selected = true;
    m_anchor = idx;
    Focus(idx);
    Invalidate();
}

void App::SelectRange(int a, int b, bool add) {
    if (!add)
        for (auto& it : m_items) it.selected = false;
    const int lo = std::max(0, std::min(a, b)), hi = std::min((int)m_items.size() - 1, std::max(a, b));
    for (int i = lo; i <= hi; ++i) m_items[i].selected = true;
    Invalidate();
}

void App::SelectAll() {
    for (auto& it : m_items) it.selected = true;
    if (m_sel < 0 && !m_items.empty()) { m_anchor = 0; Focus(0); }
    SetStatus(std::to_wstring(m_items.size()) + L" Bilder ausgewählt");
    Invalidate();
}

int App::SelCount() const {
    int n = 0;
    for (auto& it : m_items) n += it.selected ? 1 : 0;
    return n;
}

int App::FirstSelected() const {
    for (int i = 0; i < (int)m_items.size(); ++i)
        if (m_items[i].selected) return i;
    return -1;
}

std::vector<int> App::SelectedIdx() const {
    std::vector<int> v;
    for (int i = 0; i < (int)m_items.size(); ++i)
        if (m_items[i].selected) v.push_back(i);
    if (v.empty() && m_sel >= 0 && m_sel < (int)m_items.size()) v.push_back(m_sel);
    return v;
}

UINT App::PreviewPx() const {
    if (!m_viewer) return (UINT)std::lround(720 * k);
    const int longest = std::max(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    return (UINT)std::clamp(longest * 3 / 2, 1920, 5000);
}

// Baut die Prioritaetenliste: Vorschau > sichtbar > 3 Zeilen Vorlauf (unten zuerst) > 2 Zeilen darueber.
void App::RequestVisible() {
    if (!rt) return;
    uint32_t gen = m_loader.Generation();
    std::vector<ThumbRequest> reqs;
    if (m_sel >= 0 && !m_preview && !m_previewFailed)
        reqs.push_back({gen, (size_t)m_sel, true, PreviewPx(), m_items[m_sel].path});
    if (m_viewer) {  // im Betrachter zaehlt nur das grosse Bild
        m_loader.SetWanted(gen, std::move(reqs));
        return;
    }

    GridM g = Metrics();
    int n = (int)m_items.size();
    if (n > 0) {
        int first = std::max(0, (int)std::floor(m_gridScroll / g.cellH));
        int last = std::min(g.rows - 1, (int)std::floor((m_gridScroll + H(rcGrid)) / g.cellH));
        UINT sz = (UINT)std::lround(m_settings.thumbSize * k);
        auto addRow = [&](int row) {
            for (int c = 0; c < g.cols; ++c) {
                int i = row * g.cols + c;
                if (i >= n) break;
                Item& it = m_items[i];
                if (!it.thumb && !it.failed) reqs.push_back({gen, (size_t)i, false, sz, it.path});
            }
        };
        for (int r = first; r <= last; ++r) addRow(r);
        for (int r = last + 1; r <= std::min(g.rows - 1, last + 3); ++r) addRow(r);
        for (int r = first - 1; r >= std::max(0, first - 2); --r) addRow(r);

        // Speicher begrenzen: weit entfernte Thumbnails wieder freigeben.
        if (m_loaded > 600) {
            for (int i = 0; i < n; ++i) {
                int row = i / g.cols;
                if ((row < first - 25 || row > last + 25) && m_items[i].thumb) {
                    m_items[i].thumb.Reset();
                    --m_loaded;
                }
            }
        }
    }
    m_loader.SetWanted(gen, std::move(reqs));
}

void App::ConsumeThumbs() {
    for (auto& r : m_loader.Drain()) {
        if (r.generation != m_loader.Generation()) continue;
        if (r.index >= m_items.size()) continue;
        ComPtr<ID2D1Bitmap> bmp;
        if (r.ok) {
            rt->CreateBitmap(D2D1::SizeU(r.width, r.height), r.pixels.data(), r.width * 4,
                             D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                      D2D1_ALPHA_MODE_PREMULTIPLIED)),
                             &bmp);
        }
        if (r.preview) {
            if ((int)r.index == m_sel) {
                m_preview = bmp;
                m_previewFailed = !bmp;
            }
        } else if (!m_items[r.index].thumb) {
            if (bmp) { m_items[r.index].thumb = bmp; ++m_loaded; }
            else m_items[r.index].failed = true;
        }
    }
}

void App::ReloadItem(int idx) {
    if (idx < 0 || idx >= (int)m_items.size()) return;
    if (m_items[idx].thumb) { m_items[idx].thumb.Reset(); --m_loaded; }
    m_items[idx].failed = false;
    m_preview.Reset();
    m_previewFailed = false;
    m_infoOk = imgops::ReadImageInfo(m_items[idx].path, m_info);
    if (m_infoOk) {
        m_items[idx].size = m_info.fileSize;
        m_items[idx].mtime = ((ULONGLONG)m_info.modified.dwHighDateTime << 32) | m_info.modified.dwLowDateTime;
    }
    RequestVisible();
}

// ============================================================================
// Aktionen
// ============================================================================

void App::Command(int id) {
    switch (id) {
        case BTN_OPEN: BrowseForFolder(); break;
        case BTN_FULLSCREEN:
            if (m_viewer || m_items.empty()) ToggleFullscreen();  // im Betrachter: nur Vollbild an/aus
            else StartSlideshow();                                // sonst: Diashow ab dem gewaehlten Bild
            break;
        case VB_SLIDE_PREV: SlideAdvance(-1); break;
        case VB_SLIDE_NEXT: SlideAdvance(1); break;
        case VB_SLIDE_PLAY:
            m_slidePaused = !m_slidePaused;
            LayoutViewer();
            if (!m_slidePaused) SlideRestartTimer();
            break;
        case VB_SLIDE_CLOSE: EndSlideshow(); break;
        case BTN_BATCH: ShowBatchMenu(); break;
        case BTN_SETTINGS: ShowSettings(); break;
        case BTN_ROT_L: RotateSelected(false); break;
        case BTN_ROT_R: RotateSelected(true); break;
        case BTN_CROP:
            if (!m_viewer) EnterViewer(true);
            else if (m_cropping) CancelCrop();
            else BeginCrop();
            break;
        case BTN_STRIP: StripSelected(); break;
        case BTN_EDIT:
            if (!m_viewer) EnterViewer(false);
            BeginEdit();
            break;
        case VB_EDIT_SAVE: SaveEdit(false); break;
        case VB_EDIT_SAVEAS: SaveEdit(true); break;
        case VB_EDIT_RESET:
            m_ep = imgops::EditParams{};
            RebuildEditBitmap();
            break;
        case VB_EDIT_CANCEL: EndEdit(); break;
        case VB_PREV: ViewerNav(-1); break;
        case VB_NEXT: ViewerNav(1); break;
        case VB_FIT: ViewerFit(); break;
        case VB_ACTUAL: ViewerActual(); break;
        case VB_CLOSE: ExitViewer(); break;
        case VB_DELETE: DeleteSelected(); break;
        case VB_CROP_OK: ApplyCrop(); break;
        case VB_CROP_CANCEL: CancelCrop(); break;
    }
    Invalidate();
}

void App::ToggleFullscreen() {
    DWORD style = (DWORD)GetWindowLongPtrW(m_hwnd, GWL_STYLE);
    if (!m_full) {
        MONITORINFO mi{sizeof(mi)};
        GetWindowPlacement(m_hwnd, &m_prevPlacement);
        GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(m_hwnd, &m_prevPlacement);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    m_full = !m_full;
}

void App::ShowSettings() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (m_settings.thumbSize == 120 ? MF_CHECKED : 0), MENU_SIZE_S, L"Thumbnails klein (120)");
    AppendMenuW(m, MF_STRING | (m_settings.thumbSize == 170 ? MF_CHECKED : 0), MENU_SIZE_M, L"Thumbnails mittel (170)");
    AppendMenuW(m, MF_STRING | (m_settings.thumbSize == 230 ? MF_CHECKED : 0), MENU_SIZE_L, L"Thumbnails groß (230)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (m_settings.sortBy == 0 ? MF_CHECKED : 0), MENU_SORT_NAME, L"Sortieren nach Name");
    AppendMenuW(m, MF_STRING | (m_settings.sortBy == 1 ? MF_CHECKED : 0), MENU_SORT_DATE, L"Sortieren nach Änderungsdatum");
    AppendMenuW(m, MF_STRING | (m_settings.sortBy == 2 ? MF_CHECKED : 0), MENU_SORT_SIZE, L"Sortieren nach Dateigröße");
    AppendMenuW(m, MF_STRING | (m_settings.sortBy == 3 ? MF_CHECKED : 0), MENU_SORT_TAKEN, L"Sortieren nach Aufnahmedatum (EXIF)");
    AppendMenuW(m, MF_STRING | (m_settings.sortDesc ? MF_CHECKED : 0), MENU_SORT_DESC, L"Absteigend sortieren");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (m_gpsOnly ? MF_CHECKED : 0), MENU_FILTER_GPS, L"Nur Bilder mit Standortdaten zeigen");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (m_settings.overwriteOriginals ? MF_CHECKED : 0), MENU_OVERWRITE,
                L"EXIF entfernen / Zuschneiden: Original überschreiben");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (m_settings.slideSeconds == 2 ? MF_CHECKED : 0), MENU_SLIDE_2, L"Diashow: 2 Sekunden pro Bild");
    AppendMenuW(m, MF_STRING | (m_settings.slideSeconds == 4 ? MF_CHECKED : 0), MENU_SLIDE_4, L"Diashow: 4 Sekunden pro Bild");
    AppendMenuW(m, MF_STRING | (m_settings.slideSeconds == 6 ? MF_CHECKED : 0), MENU_SLIDE_6, L"Diashow: 6 Sekunden pro Bild");
    AppendMenuW(m, MF_STRING | (m_settings.slideSeconds == 10 ? MF_CHECKED : 0), MENU_SLIDE_10, L"Diashow: 10 Sekunden pro Bild");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, MENU_ABOUT, L"Über Photo Commander");
    for (auto& b : m_btns)
        if (b.id == BTN_SETTINGS) {
            POINT pt{(LONG)b.rc.right, (LONG)b.rc.bottom};
            ClientToScreen(m_hwnd, &pt);
            int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
            int newSize = cmd == MENU_SIZE_S ? 120 : cmd == MENU_SIZE_M ? 170 : cmd == MENU_SIZE_L ? 230 : 0;
            if (newSize && newSize != m_settings.thumbSize) {
                m_settings.thumbSize = newSize;
                for (auto& it : m_items) { it.thumb.Reset(); it.failed = false; }
                m_loaded = 0;
                m_loader.NewGeneration();
                SetGridScroll(m_gridScroll, true);
                RequestVisible();
            } else if (cmd == MENU_SORT_NAME || cmd == MENU_SORT_DATE || cmd == MENU_SORT_SIZE || cmd == MENU_SORT_TAKEN ||
                       cmd == MENU_SORT_DESC) {
                if (cmd == MENU_SORT_DESC) m_settings.sortDesc = !m_settings.sortDesc;
                else m_settings.sortBy = cmd - MENU_SORT_NAME;
                SortItems();
                m_lastReqRow = -1;
                RequestVisible();
                if (m_sel >= 0) EnsureVisible(m_sel);
            } else if (cmd == MENU_FILTER_GPS) {
                m_gpsOnly = !m_gpsOnly;
                ApplyFilter();
                if (m_sel >= 0) EnsureVisible(m_sel);
                SetStatus(FolderStatus());
            } else if (cmd >= MENU_SLIDE_2 && cmd <= MENU_SLIDE_10) {
                static const int secs[4] = {2, 4, 6, 10};
                m_settings.slideSeconds = secs[cmd - MENU_SLIDE_2];
                if (m_slide && !m_slidePaused) SlideRestartTimer();
            } else if (cmd == MENU_OVERWRITE) {
                m_settings.overwriteOriginals = !m_settings.overwriteOriginals;
            } else if (cmd == MENU_ABOUT) {
                std::wstring text = L"Duncan's Photo Commander\nTeil von Duncan's Suite\n\nVersion " APP_VERSION
                                    L"\n\nEinstellungen: %LOCALAPPDATA%\\DuncansPhotoCommander\\settings.ini";
                MessageBoxW(m_hwnd, text.c_str(), L"Über", MB_OK | MB_ICONINFORMATION);
            }
            if (cmd) SaveSettingsNow();
            break;
        }
    DestroyMenu(m);
}

// Zieldatei fuer "Kopie anlegen oder Original ersetzen" (Zahnrad-Einstellung). Leer = Nutzer hat abgebrochen.
std::wstring App::ChooseOutput(const std::wstring& src, const wchar_t* suffix, const wchar_t* question, const wchar_t* title) {
    if (!m_settings.overwriteOriginals) return UniquePath(WithSuffix(src, suffix));
    return MessageBoxW(m_hwnd, question, title, MB_YESNO | MB_ICONWARNING) == IDYES ? src : std::wstring();
}

// Nach dem Schreiben: Original neu einlesen bzw. die neue Kopie in die Liste aufnehmen und auswaehlen.
void App::FinishWrite(const std::wstring& src, const std::wstring& dst, const std::wstring& doneInPlace,
                      const std::wstring& doneCopy) {
    if (_wcsicmp(dst.c_str(), src.c_str()) == 0) {
        ReloadItem(m_sel);
        SetStatus(doneInPlace);
    } else {
        m_pendingStatus = doneCopy;
        OpenFolder(m_folder, dst, true);
    }
}

void App::StripSelected() {
    if (m_sel < 0) return;
    if (!m_viewer && SelCount() > 1) {
        StartBatch(m_settings.overwriteOriginals ? BatchKind::StripOverwrite : BatchKind::StripCopy);
        return;
    }
    const std::wstring src = m_items[m_sel].path, name = m_items[m_sel].name;
    std::wstring err;
    const std::wstring dst = ChooseOutput(src, L"_clean",
                                          L"Alle Metadaten (inkl. Kamera und GPS) im ORIGINAL unwiderruflich entfernen?",
                                          L"EXIF entfernen");
    if (dst.empty()) return;
    if (!imgops::StripMetadata(src, dst, &err)) {
        SetStatus(L"EXIF entfernen fehlgeschlagen: " + err);
        return;
    }
    imgops::ImageInfo check;   // Gegenprobe: sind die Standortdaten wirklich weg?
    const bool gpsLeft = imgops::ReadImageInfo(dst, check) && check.hasGps;
    FinishWrite(src, dst,
                gpsLeft ? L"Achtung: Standortdaten sind noch vorhanden: " + name : L"Metadaten entfernt: " + name,
                gpsLeft ? L"Achtung: Standortdaten sind in der Kopie noch vorhanden: " + dst
                        : L"Bereinigte Kopie erstellt (ohne Kamera- und GPS-Daten): " + FileNameOf(dst));
}

void App::RotateSelected(bool cw) {
    if (m_sel < 0) return;
    if (!m_viewer && SelCount() > 1) {
        StartBatch(cw ? BatchKind::RotateCw : BatchKind::RotateCcw);
        return;
    }
    std::wstring err;
    if (!imgops::RotateImage(m_items[m_sel].path, cw, &err)) {
        SetStatus(L"Drehen fehlgeschlagen: " + err);
        return;
    }
    ReloadItem(m_sel);
    SetStatus(cw ? L"90° rechts gedreht" : L"90° links gedreht");
}

void App::DeleteSelected() {
    std::vector<int> idx = m_viewer ? std::vector<int>{m_sel} : SelectedIdx();
    if (idx.empty() || idx[0] < 0 || idx[0] >= (int)m_items.size()) return;
    std::wstring msg = idx.size() == 1 ? L"\u201e" + m_items[idx[0]].name + L"\u201c in den Papierkorb verschieben?"
                                       : std::to_wstring(idx.size()) + L" Bilder in den Papierkorb verschieben?";
    if (MessageBoxW(m_hwnd, msg.c_str(), L"Löschen", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    const int n = (int)m_items.size();
    std::vector<bool> del(n, false);
    std::wstring from;  // doppelt nullterminierte Liste fuer SHFileOperation
    for (int i : idx) {
        del[i] = true;
        from.append(m_items[i].path);
        from.push_back(L'\0');
    }
    std::wstring next;  // danach anzuzeigendes Bild: das naechste nicht geloeschte, sonst das vorherige
    for (int i = idx.back() + 1; i < n && next.empty(); ++i)
        if (!del[i]) next = m_items[i].path;
    for (int i = idx.front() - 1; i >= 0 && next.empty(); --i)
        if (!del[i]) next = m_items[i].path;

    SHFILEOPSTRUCTW op{};
    op.hwnd = m_hwnd;
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    const int rc = SHFileOperationW(&op);
    if (rc != 0 || op.fAnyOperationsAborted) SetStatus(L"Löschen fehlgeschlagen oder abgebrochen");
    else m_pendingStatus = std::to_wstring(idx.size()) + (idx.size() == 1 ? L" Bild" : L" Bilder") +
                           L" in den Papierkorb verschoben";
    if (m_dupMode) {  // die Gruppen stimmen nach dem Loeschen nicht mehr: zurueck zur normalen Ansicht
        m_dupMode = false;
        ClearDupMarks();
    }
    OpenFolder(m_folder, next, true);
}

void App::ShowBatchMenu() {
    if (m_batchRunning) {
        if (MessageBoxW(m_hwnd, L"Die Stapelverarbeitung läuft noch. Abbrechen?", L"Stapelverarbeitung",
                        MB_YESNO | MB_ICONQUESTION) == IDYES) {
            m_batchCancel = true;
            SetStatus(L"Stapelverarbeitung wird abgebrochen …");
        }
        return;
    }
    if (m_items.empty()) {
        SetStatus(L"Keine Bilder im Ordner.");
        return;
    }
    const int sc = SelCount();
    const std::wstring scope = sc > 1 ? L"Auswahl (" + std::to_wstring(sc) + L" Bilder)"
                                      : L"alle " + std::to_wstring(m_items.size()) + L" Bilder im Ordner";
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, (L"Gilt für: " + scope).c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, MENU_B_STRIP_COPY, L"EXIF/GPS entfernen – Kopien im Ordner \"bereinigt\"");
    AppendMenuW(m, MF_STRING, MENU_B_STRIP_OVERWRITE, L"EXIF/GPS entfernen – Originale überschreiben");
    AppendMenuW(m, MF_STRING, MENU_B_RESIZE, L"Fürs Teilen verkleinern (2048 px, ohne Metadaten)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, MENU_B_ROT_CW, L"Alle 90° nach rechts drehen");
    AppendMenuW(m, MF_STRING, MENU_B_ROT_CCW, L"Alle 90° nach links drehen");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (m_dupRunning ? MF_GRAYED : 0), MENU_B_DUPES,
                L"Doppelte und ähnliche Bilder im Ordner suchen");
    if (m_dupMode) AppendMenuW(m, MF_STRING, MENU_B_DUPES_END, L"Duplikatansicht beenden");
    POINT pt{0, 0};
    for (auto& b : m_btns)
        if (b.id == BTN_BATCH) pt = {(LONG)b.rc.left, (LONG)b.rc.bottom};
    ClientToScreen(m_hwnd, &pt);
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(m);
    switch (cmd) {
        case MENU_B_STRIP_COPY: StartBatch(BatchKind::StripCopy); break;
        case MENU_B_STRIP_OVERWRITE: StartBatch(BatchKind::StripOverwrite); break;
        case MENU_B_RESIZE: StartBatch(BatchKind::Resize); break;
        case MENU_B_ROT_CW: StartBatch(BatchKind::RotateCw); break;
        case MENU_B_ROT_CCW: StartBatch(BatchKind::RotateCcw); break;
        case MENU_B_DUPES: StartDupSearch(); break;
        case MENU_B_DUPES_END: EndDupMode(); break;
    }
}

void App::StartBatch(BatchKind kind) {
    if (m_batchRunning || m_items.empty()) return;
    std::vector<std::wstring> files;
    if (SelCount() > 1) {
        for (auto& it : m_items)
            if (it.selected) files.push_back(it.path);
    } else {
        for (auto& it : m_items) files.push_back(it.path);
    }
    const std::wstring scope = (SelCount() > 1 ? L"den " + std::to_wstring(files.size()) + L" ausgewählten Bildern"
                                               : L"allen " + std::to_wstring(files.size()) + L" Bildern im Ordner");
    std::wstring question, outName;
    switch (kind) {
        case BatchKind::StripCopy:
            outName = L"bereinigt";
            question = L"Von " + scope + L" Kamera- und GPS-Daten entfernen?\n\nDie bereinigten Kopien werden im Unterordner "
                       L"\"bereinigt\" abgelegt, die Originale bleiben unverändert.";
            break;
        case BatchKind::StripOverwrite:
            question = L"Von " + scope + L" Kamera- und GPS-Daten im ORIGINAL unwiderruflich entfernen?";
            break;
        case BatchKind::Resize:
            outName = L"verkleinert";
            question = L"Die längste Kante von " + scope + L" auf höchstens 2048 Pixel verkleinern (JPEG-Qualität 85, ohne "
                       L"Metadaten)?\n\nDie Kopien werden im Unterordner \"verkleinert\" abgelegt, die Originale bleiben unverändert.";
            break;
        case BatchKind::RotateCw:
        case BatchKind::RotateCcw:
            question = std::wstring(L"Bilder in ") + scope + (kind == BatchKind::RotateCw ? L" um 90° nach rechts drehen?"
                                                                                           : L" um 90° nach links drehen?");
            break;
    }
    if (MessageBoxW(m_hwnd, question.c_str(), L"Stapelverarbeitung",
                    MB_YESNO | (kind == BatchKind::StripOverwrite ? MB_ICONWARNING : MB_ICONQUESTION)) != IDYES)
        return;
    if (m_batch.joinable()) m_batch.join();

    std::wstring outDir;
    if (!outName.empty()) {
        outDir = m_folder + (m_folder.back() == L'\\' ? L"" : L"\\") + outName;
        CreateDirectoryW(outDir.c_str(), nullptr);
    }
    m_batchKind = kind;
    m_bDone = 0; m_bTotal = files.size(); m_bOk = 0; m_bFail = 0; m_bSkip = 0; m_bGps = 0;
    { std::lock_guard<std::mutex> lk(m_bErrMutex); m_bErr.clear(); }
    m_batchRunning = true;
    m_batchCancel = false;
    HWND hwnd = m_hwnd;
    m_batch = std::thread([this, hwnd, files, outDir, kind] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (size_t i = 0; i < files.size() && !m_batchCancel; ++i) {
            const std::wstring& src = files[i];
            const std::wstring dst = outDir.empty() ? src : outDir + L"\\" + FileNameOf(src);
            std::wstring err;
            bool ok = false, skipped = false;
            switch (kind) {
                case BatchKind::StripCopy:
                case BatchKind::StripOverwrite: {
                    if (!imgops::CanStripMetadata(src)) { skipped = true; break; }
                    imgops::ImageInfo before, after;
                    if (imgops::ReadImageInfo(src, before) && before.hasGps) ++m_bGps;
                    ok = imgops::StripMetadata(src, dst, &err);
                    if (ok && imgops::ReadImageInfo(dst, after) && after.hasGps) {  // Gegenprobe
                        ok = false;
                        err = L"Standortdaten nach dem Bereinigen noch vorhanden";
                    }
                    break;
                }
                case BatchKind::Resize:
                    if (!imgops::CanReencode(src)) skipped = true;
                    else ok = imgops::ResizeImage(src, dst, kShareEdge, &err);
                    break;
                case BatchKind::RotateCw:
                case BatchKind::RotateCcw:
                    if (!imgops::CanReencode(src)) skipped = true;
                    else ok = imgops::RotateImage(src, kind == BatchKind::RotateCw, &err);
                    break;
            }
            if (skipped) {
                ++m_bSkip;
            } else if (ok) {
                ++m_bOk;
            } else {
                ++m_bFail;
                std::lock_guard<std::mutex> lk(m_bErrMutex);
                if (m_bErr.empty()) m_bErr = FileNameOf(src) + L": " + err;
            }
            m_bDone = i + 1;
            PostMessageW(hwnd, WM_APP_BATCH, 0, 0);
        }
        CoUninitialize();
        PostMessageW(hwnd, WM_APP_BATCHDONE, 0, 0);
    });
    SetStatus(L"Stapelverarbeitung läuft … (Esc bricht ab)");
}

void App::FinishBatch() {
    if (m_batch.joinable()) m_batch.join();
    m_batchRunning = false;
    const bool strip = m_batchKind == BatchKind::StripCopy || m_batchKind == BatchKind::StripOverwrite;
    const wchar_t* verb = strip ? L" bereinigt" : (m_batchKind == BatchKind::Resize ? L" verkleinert" : L" gedreht");
    std::wstring msg = std::to_wstring((unsigned long long)m_bOk.load()) + verb;
    if (strip && m_bGps > 0) msg += L" (" + std::to_wstring((unsigned long long)m_bGps.load()) + L" davon mit Standortdaten)";
    if (m_bSkip > 0) msg += L", " + std::to_wstring((unsigned long long)m_bSkip.load()) + L" übersprungen (Format nicht unterstützt)";
    if (m_bFail > 0) {
        std::lock_guard<std::mutex> lk(m_bErrMutex);
        msg += L", " + std::to_wstring((unsigned long long)m_bFail.load()) + L" fehlgeschlagen (" + m_bErr + L")";
    }
    if (m_batchCancel) msg = L"Abgebrochen – " + msg;
    else msg = L"Fertig – " + msg;
    if (m_batchKind == BatchKind::StripCopy) msg += L" – Ordner \"bereinigt\"";
    if (m_batchKind == BatchKind::Resize) msg += L" – Ordner \"verkleinert\"";
    const bool inPlace = m_batchKind == BatchKind::StripOverwrite || m_batchKind == BatchKind::RotateCw ||
                         m_batchKind == BatchKind::RotateCcw;
    if (inPlace) {
        m_pendingStatus = msg;
        OpenFolder(m_folder, L"", true);  // geaenderte Dateien neu einlesen, Vorschauen neu laden
    } else {
        SetStatus(msg);
    }
    Invalidate();
}


// ============================================================================
// Bildbetrachter
// ============================================================================

void App::EnterViewer(bool crop) {
    if (m_sel < 0 || m_sel >= (int)m_items.size()) return;
    m_viewer = true;
    m_cropping = m_cropDragging = m_panning = false;
    m_vZoom = 1.f;
    m_vPanX = m_vPanY = 0;
    m_preview.Reset();  // in Bildschirmaufloesung neu laden
    m_previewFailed = false;
    LayoutViewer();
    if (crop) BeginCrop();
    RequestVisible();
    Invalidate();
}

void App::ExitViewer() {
    if (!m_viewer) return;
    if (m_editing) EndEdit();
    m_viewer = false;
    m_cropping = m_cropDragging = m_panning = false;
    if (GetCapture() == m_hwnd) ReleaseCapture();
    if (m_fullByViewer && m_full) ToggleFullscreen();
    m_fullByViewer = false;
    m_preview.Reset();  // das Detail-Panel braucht nur die kleine Vorschau
    m_previewFailed = false;
    m_lastReqRow = -1;
    if (m_resortPending) {
        m_resortPending = false;
        ApplyFilter();
        if (m_sel >= 0) EnsureVisible(m_sel);
        SetStatus(FolderStatus());
    }
    RequestVisible();
    Invalidate();
}

void App::LayoutViewer() {
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    const float Wd = (float)cr.right, Ht = (float)cr.bottom, bar = 60 * k;
    if (m_slide) {  // Diashow: das Bild nutzt den ganzen Bildschirm, nur die kleine Steuerleiste schwebt unten
        rcViewer = {0, 0, Wd, Ht};
        rcViewBar = {0, Ht, Wd, Ht};
        rcEditPanel = {};
        const float bw = 52 * k, bh = 44 * k, gap = 10 * k, total = 4 * bw + 3 * gap;
        const float x0 = (Wd - total) / 2, y0 = Ht - 80 * k;
        rcSlideBar = {x0 - 16 * k, y0 - 8 * k, x0 + total + 16 * k, y0 + bh + 8 * k};
        struct SDef { int id; const wchar_t* label; };
        const SDef sd[4] = {{VB_SLIDE_PREV, L"⏮"}, {VB_SLIDE_PLAY, m_slidePaused ? L"▶" : L"❚❚"},
                            {VB_SLIDE_NEXT, L"⏭"}, {VB_SLIDE_CLOSE, L"✕"}};
        m_vbtns.clear();
        for (int i = 0; i < 4; ++i) {
            Btn b;
            b.id = sd[i].id;
            b.label = sd[i].label;
            b.icon = true;
            b.accent = i == 1;
            b.rc = {x0 + i * (bw + gap), y0, x0 + i * (bw + gap) + bw, y0 + bh};
            m_vbtns.push_back(b);
        }
        return;
    }
    const float panelW = m_editing ? std::min(300 * k, Wd * 0.4f) : 0.f;
    rcViewer = {0, 0, Wd - panelW, std::max(0.f, Ht - bar)};
    rcEditPanel = m_editing ? D2D1_RECT_F{Wd - panelW, 0, Wd, std::max(0.f, Ht - bar)} : D2D1_RECT_F{};
    rcViewBar = {0, std::max(0.f, Ht - bar), Wd, Ht};

    struct Def { int id; const wchar_t* label; bool accent, danger; };
    std::vector<Def> defs;
    if (m_editing) {
        defs = {{VB_EDIT_SAVE, L"Speichern", true, false},
                {VB_EDIT_SAVEAS, L"Speichern unter …", false, false},
                {VB_EDIT_RESET, L"Zurücksetzen", false, false},
                {VB_EDIT_CANCEL, L"Abbrechen", false, false}};
    } else if (m_cropping) {
        defs = {{VB_CROP_OK, L"Zuschneiden speichern", true, false}, {VB_CROP_CANCEL, L"Abbrechen", false, false}};
    } else {
        defs = {{VB_PREV, L"‹ Zurück", false, false},   {VB_NEXT, L"Weiter ›", false, false},
                {BTN_ROT_L, L"↺", false, false},        {BTN_ROT_R, L"↻", false, false},
                {VB_FIT, L"Einpassen", false, false},   {VB_ACTUAL, L"100 %", false, false},
                {BTN_CROP, L"Zuschneiden", false, false}, {BTN_EDIT, L"Bearbeiten", false, false},
                {BTN_STRIP, L"EXIF entfernen", false, true},
                {VB_DELETE, L"Löschen", false, true},   {BTN_FULLSCREEN, L"Vollbild", false, false},
                {VB_CLOSE, L"Schließen ✕", true, false}};
    }
    const float bh = 36 * k, by = rcViewBar.top + (bar - bh) / 2, gap = 6 * k;
    std::vector<float> widths;
    float total = 0;
    for (auto& d : defs) {
        const float w = std::max(40 * k, (float)wcslen(d.label) * 7.5f * k + 22 * k);
        widths.push_back(w);
        total += w + gap;
    }
    total -= gap;
    float x = std::max(8 * k, (Wd - total) / 2);
    m_vbtns.clear();
    for (size_t i = 0; i < defs.size(); ++i) {
        Btn b;
        b.id = defs[i].id;
        b.label = defs[i].label;
        b.accent = defs[i].accent;
        b.danger = defs[i].danger;
        b.needsSelection = true;
        b.rc = {x, by, x + widths[i], by + bh};
        x += widths[i] + gap;
        m_vbtns.push_back(b);
    }
}

ID2D1Bitmap* App::ViewerBitmap() const {
    if (m_preview) return m_preview.Get();
    if (m_sel >= 0 && m_sel < (int)m_items.size() && m_items[m_sel].thumb) return m_items[m_sel].thumb.Get();
    return nullptr;
}

float App::ViewerFitScale(const D2D1_SIZE_F& sz) const {
    const float m = m_slide ? 0.f : 12 * k;
    return std::max(0.01f, std::min((W(rcViewer) - 2 * m) / sz.width, (H(rcViewer) - 2 * m) / sz.height));
}

D2D1_RECT_F App::ViewerImageRect() const {
    ID2D1Bitmap* b = ViewerBitmap();
    if (!b) return {0, 0, 0, 0};
    const D2D1_SIZE_F sz = b->GetSize();
    const float fit = ViewerFitScale(sz) * m_vZoom, w = sz.width * fit, h = sz.height * fit;
    const float cx = (rcViewer.left + rcViewer.right) / 2 + m_vPanX, cy = (rcViewer.top + rcViewer.bottom) / 2 + m_vPanY;
    return {cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2};
}

bool App::OrientedSize(double& w, double& h) const {
    if (!m_infoOk || !m_info.width || !m_info.height) return false;
    const bool swap = m_info.orientation >= 5;
    w = swap ? m_info.height : m_info.width;
    h = swap ? m_info.width : m_info.height;
    return true;
}

void App::ClampPan() {
    ID2D1Bitmap* b = ViewerBitmap();
    if (!b) { m_vPanX = m_vPanY = 0; return; }
    const D2D1_SIZE_F sz = b->GetSize();
    const float fit = ViewerFitScale(sz) * m_vZoom, w = sz.width * fit, h = sz.height * fit;
    const float mx = w > W(rcViewer) ? (w - W(rcViewer)) / 2 : 0, my = h > H(rcViewer) ? (h - H(rcViewer)) / 2 : 0;
    m_vPanX = std::clamp(m_vPanX, -mx, mx);
    m_vPanY = std::clamp(m_vPanY, -my, my);
}

void App::ViewerFit() {
    m_vZoom = 1.f;
    m_vPanX = m_vPanY = 0;
    Invalidate();
}

void App::ViewerZoomAt(float factor, float px, float py) {
    const float nz = std::clamp(m_vZoom * factor, 0.1f, 48.f);
    if (nz == m_vZoom) return;
    const float ratio = nz / m_vZoom;
    const float c0x = (rcViewer.left + rcViewer.right) / 2, c0y = (rcViewer.top + rcViewer.bottom) / 2;
    const float cx = c0x + m_vPanX, cy = c0y + m_vPanY;  // aktuelle Bildmitte; der Punkt unter dem Mauszeiger bleibt stehen
    m_vPanX = (px - (px - cx) * ratio) - c0x;
    m_vPanY = (py - (py - cy) * ratio) - c0y;
    m_vZoom = nz;
    ClampPan();
    Invalidate();
}

// 100 %: ein Bildpixel entspricht einem Bildschirmpixel (bezogen auf die Originalgroesse)
void App::ViewerActual(float px, float py) {
    ID2D1Bitmap* b = ViewerBitmap();
    if (!b) return;
    const D2D1_SIZE_F sz = b->GetSize();
    double ow = 0, oh = 0;
    const double targetW = OrientedSize(ow, oh) ? ow : sz.width;
    const float target = (float)(targetW / (sz.width * ViewerFitScale(sz)));
    if (px < 0) { px = (rcViewer.left + rcViewer.right) / 2; py = (rcViewer.top + rcViewer.bottom) / 2; }
    ViewerZoomAt(target / m_vZoom, px, py);
}

void App::ViewerNav(int delta) {
    const int n = (int)m_items.size();
    if (n == 0 || m_sel < 0) return;
    const int t = std::clamp(m_sel + delta, 0, n - 1);
    if (t == m_sel) return;
    if (m_editing) {
        SetStatus(L"Erst speichern oder abbrechen, dann zum nächsten Bild.");
        return;
    }
    if (m_cropping) CancelCrop();
    Select(t);
    m_vZoom = 1.f;
    m_vPanX = m_vPanY = 0;
    LayoutViewer();
    Invalidate();
}

void App::BeginCrop() {
    if (!m_viewer || m_sel < 0 || !imgops::CanReencode(m_items[m_sel].path)) return;
    m_cropping = true;
    m_cropHas = m_cropDragging = false;
    m_crop[0] = m_crop[1] = 0;
    m_crop[2] = m_crop[3] = 1;
    LayoutViewer();
    Invalidate();
}

void App::CancelCrop() {
    m_cropping = m_cropHas = m_cropDragging = false;
    if (GetCapture() == m_hwnd) ReleaseCapture();
    LayoutViewer();
    Invalidate();
}

void App::ApplyCrop() {
    if (!m_cropping || !m_cropHas || m_sel < 0) return;
    const std::wstring src = m_items[m_sel].path, name = m_items[m_sel].name;
    std::wstring err;
    const std::wstring dst = ChooseOutput(src, L"_crop",
                                          L"Das ORIGINAL durch den Ausschnitt ersetzen? Das lässt sich nicht rückgängig machen.",
                                          L"Zuschneiden");
    if (dst.empty()) return;
    if (!imgops::CropImage(src, dst, m_crop[0], m_crop[1], m_crop[2], m_crop[3], &err)) {
        SetStatus(L"Zuschneiden fehlgeschlagen: " + err);
        return;
    }
    CancelCrop();
    FinishWrite(src, dst, L"Zugeschnitten: " + name, L"Zugeschnittene Kopie erstellt: " + FileNameOf(dst));
}

void App::OnViewerKey(WPARAM vk) {
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    const float cx = (rcViewer.left + rcViewer.right) / 2, cy = (rcViewer.top + rcViewer.bottom) / 2;
    if (m_slide) {  // Diashow: Leertaste/P Pause, Pfeile blaettern, Esc beendet
        switch (vk) {
            case VK_ESCAPE: EndSlideshow(); break;
            case VK_SPACE:
            case 'P':
                m_slidePaused = !m_slidePaused;
                m_slideLastMove = GetTickCount64();  // Leiste kurz einblenden
                m_slideCursorHidden = false;
                LayoutViewer();
                if (!m_slidePaused) SlideRestartTimer();
                break;
            case VK_RIGHT: case VK_NEXT: SlideAdvance(1); break;
            case VK_LEFT: case VK_PRIOR: SlideAdvance(-1); break;
        }
        Invalidate();
        return;
    }
    if (m_editing) {  // Bearbeiten-Modus: nur Speichern/Abbrechen (Blaettern, Drehen, Loeschen waeren hier verwirrend)
        if (vk == VK_ESCAPE) EndEdit();
        else if (vk == VK_RETURN) SaveEdit(false);
        Invalidate();
        return;
    }
    switch (vk) {
        case 'E': Command(BTN_EDIT); break;
        case VK_F5: StartSlideshow(); break;
        case VK_ESCAPE:
            if (m_cropping) CancelCrop();
            else ExitViewer();
            break;
        case VK_RETURN:
            if (m_cropping) ApplyCrop();
            else ExitViewer();
            break;
        case VK_LEFT: case VK_PRIOR: case VK_BACK: ViewerNav(-1); break;
        case VK_RIGHT: case VK_NEXT: case VK_SPACE: ViewerNav(1); break;
        case VK_HOME: ViewerNav(-(int)m_items.size()); break;
        case VK_END: ViewerNav((int)m_items.size()); break;
        case '0': case VK_NUMPAD0: case 'F': ViewerFit(); break;
        case '1': case VK_NUMPAD1: ViewerActual(); break;
        case VK_OEM_PLUS: case VK_ADD: ViewerZoomAt(1.25f, cx, cy); break;
        case VK_OEM_MINUS: case VK_SUBTRACT: ViewerZoomAt(0.8f, cx, cy); break;
        case 'R': if (m_sel >= 0 && imgops::CanReencode(m_items[m_sel].path)) RotateSelected(!shift); break;
        case 'C': Command(BTN_CROP); break;
        case VK_DELETE: DeleteSelected(); break;
    }
    Invalidate();
}

void App::DrawViewer() {
    const float Wd = rcViewBar.right, Ht = rcViewBar.bottom;
    if (m_slide) {
        DrawSlide();
        return;
    }
    Fill({0, 0, Wd, Ht}, 0x0C0D10);
    rt->PushAxisAlignedClip(rcViewer, D2D1_ANTIALIAS_MODE_ALIASED);

    ID2D1Bitmap* bmp = ViewerBitmap();
    const D2D1_RECT_F ir = ViewerImageRect();
    if (m_editing) {
        DrawEditPreview();
    } else if (bmp) {
        const float scale = W(ir) / bmp->GetSize().width;
        rt->DrawBitmap(bmp, ir, 1.f,
                       scale > 2.f ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        Txt(m_previewFailed ? L"Keine Vorschau" : L"Lädt…", rcViewer, fCenter.Get(), Theme::TextDim);
    }

    double ow = 0, oh = 0;
    const bool haveDims = OrientedSize(ow, oh);
    if (bmp && m_cropping && m_cropHas) {
        const D2D1_RECT_F c{ir.left + (float)m_crop[0] * W(ir), ir.top + (float)m_crop[1] * H(ir),
                            ir.left + (float)m_crop[2] * W(ir), ir.top + (float)m_crop[3] * H(ir)};
        Fill({ir.left, ir.top, ir.right, c.top}, 0x000000, 0.6f);
        Fill({ir.left, c.bottom, ir.right, ir.bottom}, 0x000000, 0.6f);
        Fill({ir.left, c.top, c.left, c.bottom}, 0x000000, 0.6f);
        Fill({c.right, c.top, ir.right, c.bottom}, 0x000000, 0.6f);
        SetCol(0xFFFFFF, 0.3f);
        for (int i = 1; i <= 2; ++i) {
            rt->DrawLine({c.left + W(c) * i / 3, c.top}, {c.left + W(c) * i / 3, c.bottom}, brush.Get(), 1.f);
            rt->DrawLine({c.left, c.top + H(c) * i / 3}, {c.right, c.top + H(c) * i / 3}, brush.Get(), 1.f);
        }
        StrokeRR(c, 0, Theme::Accent, 2 * k);
        if (haveDims) {
            wchar_t lab[48];
            swprintf_s(lab, L"%d × %d px", (int)std::lround((m_crop[2] - m_crop[0]) * ow),
                       (int)std::lround((m_crop[3] - m_crop[1]) * oh));
            const D2D1_RECT_F pill{c.left + 6 * k, c.top + 6 * k, c.left + 6 * k + 120 * k, c.top + 6 * k + 22 * k};
            FillRR(pill, 4 * k, 0x000000, 0.65f);
            Txt(lab, pill, fCenter.Get(), Theme::Text);
        }
    }

    // Kopfzeile
    const D2D1_RECT_F top{0, 0, Wd, 36 * k};
    Fill(top, 0x000000, 0.5f);
    if (m_editing) {
        Txt(L"Bearbeiten   ·   Enter = speichern   ·   Esc = abbrechen", {16 * k, 0, Wd - 16 * k, 36 * k}, fNormal.Get(),
            Theme::Text);
    } else if (m_cropping) {
        Txt(L"Zuschneiden: Ausschnitt mit gedrückter Maustaste aufziehen   ·   Enter = speichern   ·   Esc = abbrechen",
            {16 * k, 0, Wd - 16 * k, 36 * k}, fNormal.Get(), Theme::Text);
    } else if (m_sel >= 0 && m_sel < (int)m_items.size()) {
        wchar_t idx[48];
        swprintf_s(idx, L"%d / %d", m_sel + 1, (int)m_items.size());
        Txt(m_items[m_sel].name + L"   ·   " + idx, {16 * k, 0, Wd * 0.6f, 36 * k}, fBold.Get(), Theme::Text);
        wchar_t info[96];
        const int pct = (bmp && haveDims && ow > 0) ? (int)std::lround(W(ir) / ow * 100) : 0;
        if (haveDims) swprintf_s(info, L"%d × %d   ·   %d %%%s", (int)ow, (int)oh, pct, !m_preview ? L"   ·   lädt …" : L"");
        else swprintf_s(info, L"%d %%", pct);
        Txt(info, {Wd * 0.6f, 0, Wd - 16 * k, 36 * k}, fRight.Get(), Theme::TextDim);
    }
    if (!m_status.empty())
        Txt(m_status, {16 * k, rcViewer.bottom - 30 * k, Wd - 16 * k, rcViewer.bottom - 6 * k}, fSmall.Get(), Theme::TextDim);
    rt->PopAxisAlignedClip();

    if (m_editing) DrawEditPanel();
    Fill(rcViewBar, Theme::Topbar);
    Fill({0, rcViewBar.top, Wd, rcViewBar.top + 1}, Theme::Border);
    for (auto& b : m_vbtns) DrawButton(b);
}

// ============================================================================
// Diashow
// ============================================================================

float App::SlideOverlayAlpha() const {
    if (!m_slide) return 0.f;
    const ULONGLONG dt = GetTickCount64() - m_slideLastMove;
    if (dt < 2000) return 1.f;
    if (dt >= 2400) return 0.f;
    return 1.f - (float)(dt - 2000) / 400.f;  // 0,4 s ausblenden
}

void App::SlideRestartTimer() {
    SetTimer(m_hwnd, TIMER_SLIDE, (UINT)m_settings.slideSeconds * 1000, nullptr);  // setzt die laufende Zeit zurueck
}

void App::StartSlideshow() {
    if (m_slide || m_items.empty()) return;
    if (m_editing) {
        SetStatus(L"Erst das Bearbeiten beenden, dann die Diashow starten.");
        return;
    }
    if (m_sel < 0) Select(0);
    if (m_sel < 0) return;
    if (m_cropping) CancelCrop();
    m_slideFromViewer = m_viewer;
    m_slideToggledFull = false;
    m_slidePaused = false;
    m_slide = true;
    if (!m_viewer) EnterViewer(false);
    if (!m_full) {
        ToggleFullscreen();
        if (m_slideFromViewer) m_slideToggledFull = true;
        else m_fullByViewer = true;  // ExitViewer verlaesst den Vollbildmodus wieder
    }
    m_vZoom = 1.f;
    m_vPanX = m_vPanY = 0;
    POINT pt{};
    GetCursorPos(&pt);
    ScreenToClient(m_hwnd, &pt);
    m_slideMouse = pt;
    m_slideLastMove = GetTickCount64();  // Leiste zeigt sich anfangs kurz
    m_slideCursorHidden = false;
    LayoutViewer();
    SlideRestartTimer();
    SetTimer(m_hwnd, TIMER_FADE, 40, nullptr);
    Invalidate();
}

void App::EndSlideshow() {
    if (!m_slide) return;
    m_slide = false;
    KillTimer(m_hwnd, TIMER_SLIDE);
    KillTimer(m_hwnd, TIMER_FADE);
    m_slideCursorHidden = false;
    m_uiAlpha = 1.f;
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    if (m_slideFromViewer) {  // zurueck in den normalen Betrachter
        if (m_slideToggledFull && m_full) ToggleFullscreen();
        LayoutViewer();
        Invalidate();
    } else {
        ExitViewer();  // zurueck zur Normalansicht (und Vollbild aus)
    }
}

void App::SlideAdvance(int delta) {
    const int n = (int)m_items.size();
    if (!m_slide || n < 2 || m_sel < 0) return;
    Select(((m_sel + delta) % n + n) % n);
    m_vZoom = 1.f;
    m_vPanX = m_vPanY = 0;
    LayoutViewer();
    SlideRestartTimer();
    Invalidate();
}

void App::DrawSlide() {
    const float Wd = rcViewBar.right, Ht = rcViewBar.bottom;
    Fill({0, 0, Wd, Ht}, 0x000000);
    if (ID2D1Bitmap* bmp = ViewerBitmap()) {
        rt->DrawBitmap(bmp, ViewerImageRect(), 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        Txt(m_previewFailed ? L"Keine Vorschau" : L"Lädt…", rcViewer, fCenter.Get(), Theme::TextDim);
    }
    if (m_slidePaused) {  // Hinweis bleibt sichtbar, solange pausiert ist
        const D2D1_RECT_F pill{Wd - 110 * k, 16 * k, Wd - 16 * k, 42 * k};
        FillRR(pill, 6 * k, 0x000000, 0.55f);
        Txt(L"❚❚  Pause", pill, fCenter.Get(), Theme::Text, 0.9f);
    }
    const float a = SlideOverlayAlpha();
    if (a <= 0.01f) return;
    FillRR(rcSlideBar, 12 * k, 0x000000, 0.6f * a);
    if (m_sel >= 0 && m_sel < (int)m_items.size()) {
        wchar_t idx[32];
        swprintf_s(idx, L"   ·   %d / %d", m_sel + 1, (int)m_items.size());
        Txt(m_items[m_sel].name + idx, {rcSlideBar.left - 80 * k, rcSlideBar.top - 30 * k, rcSlideBar.right + 80 * k, rcSlideBar.top - 6 * k},
            fCenter.Get(), Theme::Text, a);
    }
    m_uiAlpha = a;
    for (auto& b : m_vbtns) DrawButton(b);
    m_uiAlpha = 1.f;
}

// ============================================================================
// Bildkorrektur
// ============================================================================

namespace {
struct SliderDef { const wchar_t* label; double lo, hi; };
const SliderDef kSliders[4] = {{L"Helligkeit", -100, 100}, {L"Kontrast", -100, 100}, {L"Sättigung", -100, 100},
                               {L"Horizont ausrichten", -45, 45}};
constexpr int kEditMaxEdge = 2000;  // Vorschau-Aufloesung; gespeichert wird immer aus dem Original

std::wstring ExtOf(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.'), cut = path.find_last_of(L'\\');
    if (dot == std::wstring::npos || (cut != std::wstring::npos && dot < cut)) return L"";
    std::wstring e = path.substr(dot + 1);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

// "Speichern unter": Dateiname frei waehlbar, das Format bleibt das des Originals
std::wstring PickSavePath(HWND owner, const std::wstring& src, const std::wstring& suggestion) {
    ComPtr<IFileSaveDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return L"";
    const std::wstring ext = ExtOf(src), spec = L"*." + ext;
    COMDLG_FILTERSPEC fs[1] = {{L"Gleiches Format wie das Original", spec.c_str()}};
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetFileTypes(1, fs);
    dlg->SetDefaultExtension(ext.c_str());
    dlg->SetFileName(suggestion.c_str());
    ComPtr<IShellItem> folder;
    if (SUCCEEDED(SHCreateItemFromParsingName(ParentOf(src).c_str(), nullptr, IID_PPV_ARGS(&folder)))) dlg->SetFolder(folder.Get());
    if (FAILED(dlg->Show(owner))) return L"";
    ComPtr<IShellItem> res;
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(dlg->GetResult(&res)) && SUCCEEDED(res->GetDisplayName(SIGDN_FILESYSPATH, &p))) out = p;
    CoTaskMemFree(p);
    return out;
}
}  // namespace

void App::BeginEdit() {
    if (!m_viewer || m_editing || m_sel < 0 || m_sel >= (int)m_items.size()) return;
    if (!imgops::CanReencode(m_items[m_sel].path)) {
        SetStatus(L"Dieses Format kann nicht bearbeitet werden.");
        return;
    }
    if (m_cropping) CancelCrop();
    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::vector<BYTE> px;
    UINT w = 0, h = 0;
    const bool ok = imgops::LoadPixels(m_items[m_sel].path, kEditMaxEdge, px, w, h);
    SetCursor(old);
    if (!ok) {
        SetStatus(L"Das Bild konnte nicht zum Bearbeiten geladen werden.");
        return;
    }
    m_editHasAlpha = false;
    for (size_t i = 3; i < px.size(); i += 4)
        if (px[i] != 255) { m_editHasAlpha = true; break; }
    m_editSrc = std::move(px);
    m_editW = w;
    m_editH = h;
    m_ep = imgops::EditParams{};
    m_dragSlider = -1;
    m_editing = true;
    m_vZoom = 1.f;
    m_vPanX = m_vPanY = 0;
    LayoutViewer();
    RebuildEditBitmap();
    SetStatus(L"Regler ziehen · Doppelklick auf einen Regler setzt ihn zurück");
    Invalidate();
}

void App::EndEdit() {
    if (!m_editing) return;
    m_editing = false;
    m_dragSlider = -1;
    if (GetCapture() == m_hwnd) ReleaseCapture();
    m_editSrc.clear();
    m_editSrc.shrink_to_fit();
    m_editBmp.Reset();
    LayoutViewer();
    Invalidate();
}

void App::RebuildEditBitmap() {
    if (!m_editing || !rt || m_editSrc.empty()) return;
    std::vector<BYTE> tmp = m_editSrc;
    imgops::ApplyColor(tmp.data(), (size_t)m_editW * m_editH, m_ep);
    if (m_editHasAlpha) {  // Direct2D erwartet vormultiplizierte Farben
        for (size_t i = 0; i < tmp.size(); i += 4) {
            const unsigned a = tmp[i + 3];
            tmp[i] = (BYTE)(tmp[i] * a / 255);
            tmp[i + 1] = (BYTE)(tmp[i + 1] * a / 255);
            tmp[i + 2] = (BYTE)(tmp[i + 2] * a / 255);
        }
    }
    m_editBmp.Reset();
    rt->CreateBitmap(D2D1::SizeU(m_editW, m_editH), tmp.data(), m_editW * 4,
                     D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                     &m_editBmp);
    Invalidate();
}

D2D1_RECT_F App::EditSliderBlock(int i) const {
    const float top = rcEditPanel.top + 56 * k + i * 74 * k;
    return {rcEditPanel.left + 16 * k, top, rcEditPanel.right - 16 * k, top + 62 * k};
}

D2D1_RECT_F App::EditSliderTrack(int i) const {
    const D2D1_RECT_F b = EditSliderBlock(i);
    const float cy = b.top + 42 * k;
    return {b.left + 8 * k, cy - 2 * k, b.right - 8 * k, cy + 2 * k};
}

int App::EditSliderAt(float x, float y) const {
    if (!m_editing) return -1;
    for (int i = 0; i < 4; ++i)
        if (In(EditSliderBlock(i), x, y)) return i;
    return -1;
}

double App::EditSliderValue(int i) const {
    switch (i) {
        case 0: return m_ep.brightness;
        case 1: return m_ep.contrast;
        case 2: return m_ep.saturation;
        default: return m_ep.angle;
    }
}

void App::SetEditSlider(int i, double v) {
    const SliderDef& d = kSliders[i];
    v = std::clamp(v, d.lo, d.hi);
    if (i < 3) {
        const int iv = (int)std::lround(v);
        int& ref = i == 0 ? m_ep.brightness : (i == 1 ? m_ep.contrast : m_ep.saturation);
        if (ref == iv) return;
        ref = iv;
        RebuildEditBitmap();
    } else {
        v = std::round(v * 10.0) / 10.0;
        if (m_ep.angle == v) return;
        m_ep.angle = v;  // nur eine Drehmatrix beim Zeichnen: kein Neuberechnen der Pixel
    }
    Invalidate();
}

void App::SetEditSliderFromX(int i, float x) {
    const D2D1_RECT_F t = EditSliderTrack(i);
    const SliderDef& d = kSliders[i];
    const double f = std::clamp((x - t.left) / (t.right - t.left), 0.f, 1.f);
    double v = d.lo + f * (d.hi - d.lo);
    if (std::fabs(v) < (i < 3 ? 2.0 : 0.3)) v = 0;  // rastet in der Mitte ein
    SetEditSlider(i, v);
}

void App::DrawEditPreview() {
    if (!m_editBmp) RebuildEditBitmap();
    if (!m_editBmp) {
        Txt(L"Lädt…", rcViewer, fCenter.Get(), Theme::TextDim);
        return;
    }
    // Die Vorschau wird nur gedreht gezeichnet (Matrix) und auf den randlosen Ausschnitt begrenzt.
    // Beim Speichern rechnet imgops::ApplyEdits dieselbe Geometrie auf dem Original nach.
    const float w = (float)m_editW, h = (float)m_editH;
    const float s = (float)imgops::StraightenScale(w, h, m_ep.angle);
    const float ow = w * s, oh = h * s, margin = 16 * k;
    const float z = std::max(0.01f, std::min((W(rcViewer) - 2 * margin) / ow, (H(rcViewer) - 2 * margin) / oh));
    const float cx = (rcViewer.left + rcViewer.right) / 2, cy = (rcViewer.top + rcViewer.bottom) / 2;
    const D2D1_RECT_F out{cx - ow * z / 2, cy - oh * z / 2, cx + ow * z / 2, cy + oh * z / 2};
    rt->PushAxisAlignedClip(out, D2D1_ANTIALIAS_MODE_ALIASED);
    rt->SetTransform(D2D1::Matrix3x2F::Translation(-w / 2, -h / 2) * D2D1::Matrix3x2F::Scale(z, z) *
                     D2D1::Matrix3x2F::Rotation((float)m_ep.angle) * D2D1::Matrix3x2F::Translation(cx, cy));
    rt->DrawBitmap(m_editBmp.Get(), D2D1::RectF(0, 0, w, h), 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    if (m_dragSlider == 3) {  // Hilfslinien beim Ausrichten
        SetCol(0xFFFFFF, 0.35f);
        for (int i = 1; i <= 8; ++i) {
            rt->DrawLine({out.left + W(out) * i / 9, out.top}, {out.left + W(out) * i / 9, out.bottom}, brush.Get(), 1.f);
            rt->DrawLine({out.left, out.top + H(out) * i / 9}, {out.right, out.top + H(out) * i / 9}, brush.Get(), 1.f);
        }
    }
    rt->PopAxisAlignedClip();
}

void App::DrawEditPanel() {
    Fill(rcEditPanel, Theme::Panel);
    Fill({rcEditPanel.left, rcEditPanel.top, rcEditPanel.left + 1, rcEditPanel.bottom}, Theme::Border);
    rt->PushAxisAlignedClip(rcEditPanel, D2D1_ANTIALIAS_MODE_ALIASED);
    const float x = rcEditPanel.left + 16 * k, wd = W(rcEditPanel) - 32 * k;
    Txt(L"BILD BEARBEITEN", {x, rcEditPanel.top + 14 * k, x + wd, rcEditPanel.top + 36 * k}, fSmall.Get(), Theme::TextDim);
    for (int i = 0; i < 4; ++i) {
        const D2D1_RECT_F b = EditSliderBlock(i), t = EditSliderTrack(i);
        const double v = EditSliderValue(i);
        const bool changed = std::fabs(v) > 0.001;
        wchar_t val[24];
        if (!changed) wcscpy_s(val, L"0");
        else if (i < 3) swprintf_s(val, L"%+d", (int)std::lround(v));
        else swprintf_s(val, L"%+.1f°", v);
        Txt(kSliders[i].label, {b.left, b.top + 4 * k, b.right - 70 * k, b.top + 26 * k}, fNormal.Get(), Theme::Text);
        Txt(val, {b.right - 70 * k, b.top + 4 * k, b.right, b.top + 26 * k}, fRight.Get(),
            changed ? Theme::Accent : Theme::TextDim);
        FillRR(t, 2 * k, Theme::Border);
        const double f = (v - kSliders[i].lo) / (kSliders[i].hi - kSliders[i].lo);
        const float kx = t.left + (float)f * (t.right - t.left), mid = (t.left + t.right) / 2;
        if (changed) FillRR({std::min(kx, mid), t.top, std::max(kx, mid), t.bottom}, 2 * k, Theme::Accent);
        Fill({mid - 0.5f * k, t.top - 4 * k, mid + 0.5f * k, t.bottom + 4 * k}, Theme::TextDim, 0.7f);
        const float cy = (t.top + t.bottom) / 2, r = (m_dragSlider == i ? 9.f : 8.f) * k;
        SetCol(0xFFFFFF);
        rt->FillEllipse(D2D1::Ellipse({kx, cy}, r, r), brush.Get());
        SetCol(Theme::Accent);
        rt->DrawEllipse(D2D1::Ellipse({kx, cy}, r, r), brush.Get(), 2 * k);
    }
    float y = EditSliderBlock(3).bottom + 14 * k;
    const double sc = imgops::StraightenScale(1, 1, m_ep.angle);
    if (std::fabs(m_ep.angle) > 0.005) {
        wchar_t cut[96];
        swprintf_s(cut, L"Randlos zugeschnitten: %d %% des Bildes bleiben.", (int)std::lround(sc * sc * 100));
        Txt(cut, {x, y, x + wd, y + 36 * k}, fSmall.Get(), Theme::TextDim);
        y += 40 * k;
    }
    Txt(L"Doppelklick auf einen Regler setzt ihn zurück.", {x, y, x + wd, y + 22 * k}, fSmall.Get(), Theme::TextDim);
    rt->PopAxisAlignedClip();
}

void App::SaveEdit(bool saveAs) {
    if (!m_editing || m_sel < 0 || m_sel >= (int)m_items.size()) return;
    if (m_ep.IsIdentity()) {
        SetStatus(L"Keine Änderungen zum Speichern.");
        return;
    }
    const std::wstring src = m_items[m_sel].path, name = m_items[m_sel].name;
    std::wstring dst, err;
    if (saveAs) {
        dst = PickSavePath(m_hwnd, src, FileNameOf(WithSuffix(src, L"_edit")));
        if (dst.empty()) return;
        if (ExtOf(dst) != ExtOf(src)) {
            SetStatus(L"Das Format muss das des Originals bleiben (." + ExtOf(src) + L").");
            return;
        }
    } else {
        dst = ChooseOutput(src, L"_edit",
                           L"Das ORIGINAL durch die bearbeitete Fassung ersetzen? Das lässt sich nicht rückgängig machen.",
                           L"Bearbeiten");
        if (dst.empty()) return;
    }
    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    const bool ok = imgops::EditImage(src, dst, m_ep, &err);
    SetCursor(old);
    if (!ok) {
        SetStatus(L"Speichern fehlgeschlagen: " + err);
        return;
    }
    EndEdit();
    if (_wcsicmp(dst.c_str(), src.c_str()) == 0 || _wcsicmp(ParentOf(dst).c_str(), m_folder.c_str()) == 0)
        FinishWrite(src, dst, L"Korrekturen gespeichert: " + name, L"Bearbeitete Kopie gespeichert: " + FileNameOf(dst));
    else
        SetStatus(L"Bearbeitete Kopie gespeichert: " + dst);  // liegt ausserhalb des offenen Ordners
}

void App::OnDropFiles(HDROP drop) {
    wchar_t buf[2048]{};
    if (DragQueryFileW(drop, 0, buf, 2048)) {
        const std::wstring path = buf;
        const DWORD a = GetFileAttributesW(path.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) OpenFolder(path);
        else if (a != INVALID_FILE_ATTRIBUTES && imgops::IsSupportedImage(path)) OpenFolder(ParentOf(path), path);
    }
    DragFinish(drop);
}

// ============================================================================
// Nachrichten
// ============================================================================

LRESULT CALLBACK App::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App* self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->m_hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT App::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            if (rt) rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
            Layout();
            if (m_viewer) ClampPan();
            SetGridScroll(m_gridTarget, true);
            RequestVisible();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(m_hwnd, &ps);
            if (m_hwnd && rcTop.right == 0) Layout();
            Paint();
            EndPaint(m_hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize = {(LONG)(960 * k), (LONG)(600 * k)};
            return 0;
        }
        case WM_DPICHANGED: {
            k = HIWORD(wp) / 96.f;
            auto* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            CreateFormats();
            DiscardDevice();
            Layout();
            Invalidate();
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && m_slide && SlideOverlayAlpha() <= 0.f) {
                SetCursor(nullptr);  // Diashow: Zeiger verschwindet mit der Leiste
                return TRUE;
            }
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursor(nullptr, m_cursor));
                return TRUE;
            }
            break;
        case WM_DROPFILES: OnDropFiles(reinterpret_cast<HDROP>(wp)); return 0;
        case WM_APP_THUMBS: Invalidate(); return 0;
        case WM_APP_SCAN: OnScanDone(); return 0;
        case WM_APP_TREE: OnTreeLoaded(); return 0;
        case WM_APP_META: OnMeta(); return 0;
        case WM_APP_DUP: OnDup(); return 0;
        case WM_APP_BATCH: {
            wchar_t b[128];
            swprintf_s(b, L"Stapelverarbeitung: %llu / %llu   (Esc bricht ab)", (unsigned long long)m_bDone.load(),
                       (unsigned long long)m_bTotal.load());
            SetStatus(b);
            Invalidate();
            return 0;
        }
        case WM_APP_BATCHDONE: FinishBatch(); return 0;
        case WM_TIMER:
            if (wp == TIMER_SCROLL) {
                float d = m_gridTarget - m_gridScroll;
                if (std::fabs(d) < 0.5f) { m_gridScroll = m_gridTarget; KillTimer(m_hwnd, TIMER_SCROLL); }
                else m_gridScroll += d * 0.3f;
                GridM g = Metrics();
                int row = (int)(m_gridScroll / g.cellH);
                if (row != m_lastReqRow) { m_lastReqRow = row; RequestVisible(); }
                Invalidate();
            } else if (wp == TIMER_SLIDE) {
                if (m_slide && !m_slidePaused) SlideAdvance(1);
            } else if (wp == TIMER_FADE) {
                if (m_slide) {
                    const float a = SlideOverlayAlpha();
                    if (a > 0.f) Invalidate();
                    else if (!m_slideCursorHidden) {
                        m_slideCursorHidden = true;
                        SetCursor(nullptr);
                        Invalidate();
                    }
                }
            }
            return 0;
        case WM_MOUSEMOVE: {
            float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
            if (m_slide && ((LONG)x != m_slideMouse.x || (LONG)y != m_slideMouse.y)) {  // echte Bewegung: Leiste zeigen
                m_slideMouse = {(LONG)x, (LONG)y};
                m_slideLastMove = GetTickCount64();
                m_slideCursorHidden = false;
                Invalidate();
            }
            if (!m_tracking) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, m_hwnd, 0};
                TrackMouseEvent(&t);
                m_tracking = true;
            }
            if (m_viewer && m_dragSlider >= 0) {
                SetEditSliderFromX(m_dragSlider, x);
                return 0;
            }
            if (m_viewer && m_cropDragging) {
                const D2D1_RECT_F ir = ViewerImageRect();
                if (W(ir) > 1 && H(ir) > 1) {
                    const double fx = std::clamp((x - ir.left) / W(ir), 0.f, 1.f), fy = std::clamp((y - ir.top) / H(ir), 0.f, 1.f);
                    m_crop[0] = std::min(m_cropAnchor[0], fx);
                    m_crop[2] = std::max(m_cropAnchor[0], fx);
                    m_crop[1] = std::min(m_cropAnchor[1], fy);
                    m_crop[3] = std::max(m_cropAnchor[1], fy);
                    m_cropHas = (m_crop[2] - m_crop[0]) > 0.005 && (m_crop[3] - m_crop[1]) > 0.005;
                    Invalidate();
                }
                return 0;
            }
            if (m_viewer && m_panning) {
                m_vPanX = m_panStartX + (x - m_dragStart.x);
                m_vPanY = m_panStartY + (y - m_dragStart.y);
                ClampPan();
                Invalidate();
                return 0;
            }
            int hb = 0;
            for (auto& b : ActiveBtns())
                if (In(b.rc, x, y) && BtnEnabled(b)) hb = b.id;
            int hi = -1, hr = -1;
            if (!m_viewer) {
                hi = HitGrid(x, y);
                if (In(rcTree, x, y) && y > rcTree.top + 30 * k) {
                    int r = (int)((y - rcTree.top - 30 * k + m_treeScroll) / (kRowH * k));
                    if (r >= 0 && r < (int)m_rows.size()) hr = r;
                }
            }
            LPCWSTR cur = IDC_ARROW;
            if (hb || hi >= 0 || hr >= 0) cur = IDC_HAND;
            else if (m_viewer && EditSliderAt(x, y) >= 0) cur = IDC_HAND;
            else if (m_viewer && m_cropping && In(ViewerImageRect(), x, y)) cur = IDC_CROSS;
            else if (m_viewer && !m_editing && m_vZoom > 1.001f && In(rcViewer, x, y)) cur = IDC_SIZEALL;
            if (cur != m_cursor) { m_cursor = cur; SetCursor(LoadCursor(nullptr, cur)); }
            if (hb != m_hoverBtn || hi != m_hoverItem || hr != m_hoverRow) {
                m_hoverBtn = hb; m_hoverItem = hi; m_hoverRow = hr;
                Invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            m_tracking = false;
            m_hoverBtn = 0; m_hoverItem = m_hoverRow = -1;
            Invalidate();
            return 0;
        case WM_LBUTTONDOWN: {
            float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
            SetFocus(m_hwnd);
            for (auto& b : ActiveBtns())
                if (In(b.rc, x, y) && BtnEnabled(b)) { Command(b.id); return 0; }
            if (m_viewer) {
                const int sl = EditSliderAt(x, y);
                if (sl >= 0) {
                    m_dragSlider = sl;
                    SetCapture(m_hwnd);
                    SetEditSliderFromX(sl, x);
                    Invalidate();
                    return 0;
                }
                const D2D1_RECT_F ir = ViewerImageRect();
                if (m_cropping && In(ir, x, y) && W(ir) > 1 && H(ir) > 1) {
                    m_cropAnchor[0] = std::clamp((x - ir.left) / W(ir), 0.f, 1.f);
                    m_cropAnchor[1] = std::clamp((y - ir.top) / H(ir), 0.f, 1.f);
                    m_crop[0] = m_crop[2] = m_cropAnchor[0];
                    m_crop[1] = m_crop[3] = m_cropAnchor[1];
                    m_cropHas = false;
                    m_cropDragging = true;
                    SetCapture(m_hwnd);
                } else if (!m_cropping && !m_editing && !m_slide && In(rcViewer, x, y)) {
                    m_panning = true;
                    m_dragStart = {(LONG)x, (LONG)y};
                    m_panStartX = m_vPanX;
                    m_panStartY = m_vPanY;
                    SetCapture(m_hwnd);
                }
                Invalidate();
                return 0;
            }
            if (In(rcTree, x, y) && y > rcTree.top + 30 * k) {
                int r = (int)((y - rcTree.top - 30 * k + m_treeScroll) / (kRowH * k));
                if (r >= 0 && r < (int)m_rows.size()) {
                    TreeNode* n = m_rows[r];
                    float ax = rcTree.left + 12 * k + n->depth * 16 * k;
                    if (x < ax + 18 * k) ToggleNode(n);
                    else { m_selNode = n; OpenFolder(n->path); }
                    Invalidate();
                }
                return 0;
            }
            if (In(rcGrid, x, y)) {
                const int idx = HitGrid(x, y);
                const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
                if (idx < 0) {
                    if (!ctrl && !shift) Select(-1);
                } else if (shift && m_anchor >= 0) {
                    SelectRange(m_anchor, idx, ctrl);
                    Focus(idx);
                } else if (ctrl) {
                    m_items[idx].selected = !m_items[idx].selected;
                    m_anchor = idx;
                    Focus(m_items[idx].selected ? idx : FirstSelected());
                } else {
                    Select(idx);
                }
                Invalidate();
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (m_cropDragging || m_panning || m_dragSlider >= 0) {
                m_cropDragging = m_panning = false;
                m_dragSlider = -1;
                if (GetCapture() == m_hwnd) ReleaseCapture();
                Invalidate();
            }
            return 0;
        case WM_LBUTTONDBLCLK: {
            float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
            if (m_viewer) {
                const int sl = EditSliderAt(x, y);
                if (sl >= 0) {  // Doppelklick: diesen Regler zuruecksetzen
                    m_dragSlider = -1;
                    SetEditSlider(sl, 0);
                    Invalidate();
                    return 0;
                }
                if (!m_cropping && !m_editing && !m_slide && In(rcViewer, x, y)) {
                    if (std::fabs(m_vZoom - 1.f) < 0.01f) ViewerActual(x, y);
                    else ViewerFit();
                }
            } else {
                const int idx = HitGrid(x, y);
                if (idx >= 0) {
                    if (m_sel != idx) Select(idx);
                    EnterViewer(false);
                }
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(m_hwnd, &pt);
            const float notches = GET_WHEEL_DELTA_WPARAM(wp) / 120.f;
            if (m_viewer) {
                if (!m_editing && !m_slide && In(rcViewer, (float)pt.x, (float)pt.y)) ViewerZoomAt(std::pow(1.2f, notches), (float)pt.x, (float)pt.y);
                return 0;
            }
            float delta = -notches;
            if (In(rcTree, (float)pt.x, (float)pt.y)) {
                float maxS = std::max(0.f, m_rows.size() * kRowH * k - (H(rcTree) - 30 * k));
                m_treeScroll = std::clamp(m_treeScroll + delta * 3 * kRowH * k, 0.f, maxS);
                Invalidate();
            } else {
                SetGridScroll(m_gridTarget + delta * 90 * k);
            }
            return 0;
        }
        case WM_KEYDOWN: {
            if (wp == VK_F11) { ToggleFullscreen(); return 0; }
            if (m_viewer) { OnViewerKey(wp); return 0; }
            const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
            if (wp == VK_ESCAPE) {
                if (m_batchRunning) { m_batchCancel = true; SetStatus(L"Stapelverarbeitung wird abgebrochen …"); }
                else if (m_dupRunning && m_dup) { m_dup->pr.cancel = true; SetStatus(L"Duplikatsuche wird abgebrochen …"); }
                else if (m_dupMode) EndDupMode();
                else if (m_full) ToggleFullscreen();
                return 0;
            }
            if (wp == VK_F5 || (wp == 'F' && !ctrl)) { StartSlideshow(); return 0; }
            int n = (int)m_items.size();
            if (n == 0) return 0;
            if (ctrl && wp == 'A') { SelectAll(); return 0; }
            if (wp == VK_RETURN) { if (m_sel >= 0) EnterViewer(false); return 0; }
            if (wp == VK_DELETE) { DeleteSelected(); return 0; }
            int cols = Metrics().cols, s = m_sel < 0 ? 0 : m_sel, t = s;
            switch (wp) {
                case VK_RIGHT: t = std::min(n - 1, s + 1); break;
                case VK_LEFT: t = std::max(0, s - 1); break;
                case VK_DOWN: t = std::min(n - 1, s + cols); break;
                case VK_UP: t = std::max(0, s - cols); break;
                case VK_HOME: t = 0; break;
                case VK_END: t = n - 1; break;
                default: return 0;
            }
            if (shift) {
                if (m_anchor < 0) m_anchor = s;
                SelectRange(m_anchor, t, false);
                Focus(t);
            } else {
                Select(t);
            }
            return 0;
        }
        case WM_DESTROY:
            KillTimer(m_hwnd, TIMER_SCROLL);
            SaveSettingsNow();
            m_batchCancel = true;
            if (m_meta) m_meta->cancel = true;
            if (m_dup) m_dup->pr.cancel = true;
            if (m_batch.joinable()) m_batch.join();
            m_loader.Stop();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Dunkle Kontextmenues (undokumentiert: uxtheme-Ordinal 135 = SetPreferredAppMode, 136 = FlushMenuThemes)
    if (HMODULE ux = LoadLibraryW(L"uxtheme.dll")) {
        using SetMode = int(WINAPI*)(int);
        using Flush = void(WINAPI*)();
        if (auto f = reinterpret_cast<SetMode>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))))) f(2 /*ForceDark*/);
        if (auto f = reinterpret_cast<Flush>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))))) f();
    }

    // Erstes Argument: Ordner oder Bilddatei, die beim Start geoeffnet wird
    std::wstring startArg;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        if (argc > 1) startArg = argv[1];
        LocalFree(argv);
    }

    int code = 0;
    {
        App app;
        if (app.Create(hi, show, startArg)) {
            MSG m;
            while (GetMessageW(&m, nullptr, 0, 0) > 0) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            code = (int)m.wParam;
        } else {
            code = 1;
        }
    }
    CoUninitialize();
    return code;
}
