// Duncan's Photo Commander - Grundgeruest (Win32 + Direct2D/DirectWrite + WIC)
//
// Aufbau: Topbar | Ordnerbaum | Thumbnail-Grid | Detail-Panel | Statusleiste.
// Alles wird selbst in Direct2D gezeichnet (kein Common Controls), damit das dunkle
// Flat-Design der Suite pixelgenau umgesetzt werden kann.
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <dxgiformat.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ImageOps.h"
#include "Theme.h"
#include "ThumbnailLoader.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT WM_APP_THUMBS = WM_APP + 1;
constexpr UINT WM_APP_BATCH = WM_APP + 2;      // wParam = fertig, lParam = gesamt
constexpr UINT WM_APP_BATCHDONE = WM_APP + 3;  // wParam = ok, lParam = fehlgeschlagen
constexpr UINT_PTR TIMER_SCROLL = 1;

enum ButtonId {
    BTN_OPEN = 1, BTN_FULLSCREEN, BTN_BATCH, BTN_SETTINGS,
    BTN_ROT_L, BTN_ROT_R, BTN_CROP, BTN_STRIP
};
enum MenuId {
    MENU_SIZE_S = 100, MENU_SIZE_M, MENU_SIZE_L, MENU_OVERWRITE = 110, MENU_ABOUT = 120
};

struct Settings {
    int thumbSize = 170;            // DIP, laengste Kante
    bool overwriteOriginals = false;  // EXIF-Cleaner: Original ersetzen statt "_clean"-Kopie
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
    TreeNode* parent = nullptr;
    std::vector<std::unique_ptr<TreeNode>> children;
};

struct Item {
    std::wstring path, name;
    ComPtr<ID2D1Bitmap> thumb;
    bool failed = false;
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

class App {
public:
    bool Create(HINSTANCE hi, int show);
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
    void DrawButton(const Btn& b);
    void DrawScrollbar(const D2D1_RECT_F& area, float scroll, float content);
    void DrawTopbar();
    void DrawTree();
    void DrawGrid();
    void DrawDetails();
    void DrawStatus();

    // --- Ordnerbaum
    void InitTreeRoots();
    void EnsureChildren(TreeNode* n);
    void Flatten();
    void ToggleNode(TreeNode* n);

    // --- Ordner / Grid
    void OpenFolder(const std::wstring& path);
    void BrowseForFolder();
    GridM Metrics() const;
    int HitGrid(float x, float y) const;
    void Select(int idx);
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
    void RotateSelected(bool cw);
    void StartBatch();
    void SetStatus(const std::wstring& s) { m_status = s; }
    void Invalidate() { InvalidateRect(m_hwnd, nullptr, FALSE); }

    HWND m_hwnd = nullptr;
    float k = 1.f;  // DPI-Faktor (Layout in Pixeln = Design * k)

    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dw;
    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> fNormal, fSmall, fBold, fTitle, fIcon, fCenter;

    D2D1_RECT_F rcTop{}, rcTree{}, rcGrid{}, rcDetail{}, rcStatus{};
    D2D1_RECT_F rcPreview{}, rcActionsHead{};
    float m_infoTop = 0;
    std::vector<Btn> m_btns;
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
    int m_sel = -1, m_hoverItem = -1;
    float m_gridScroll = 0, m_gridTarget = 0;
    int m_lastReqRow = -1;
    size_t m_loaded = 0;
    ThumbnailLoader m_loader;

    // Detail
    imgops::ImageInfo m_info;
    bool m_infoOk = false;
    ComPtr<ID2D1Bitmap> m_preview;
    bool m_previewFailed = false;

    std::wstring m_status = L"Bereit";
    bool m_full = false;
    WINDOWPLACEMENT m_prevPlacement{sizeof(WINDOWPLACEMENT)};
    bool m_tracking = false;

    std::thread m_batch;
    std::atomic<bool> m_batchRunning{false};
    std::atomic<bool> m_batchCancel{false};
};

// ============================================================================
// Setup
// ============================================================================

bool App::Create(HINSTANCE hi, int show) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"DuncansPhotoCommander";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
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

    // Startordner: Bilder
    std::wstring pics = KnownFolder(FOLDERID_Pictures);
    if (!pics.empty()) OpenFolder(pics);
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
    m_preview.Reset();
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
        {BTN_CROP, L"Zuschneiden", false}, {BTN_STRIP, L"EXIF entfernen", true}};
    for (int i = 0; i < 4; ++i) {
        Btn b;
        b.id = acts[i].id;
        b.label = acts[i].t;
        b.danger = acts[i].danger;
        b.needsSelection = true;
        float bx = dx + (i % 2) * (bw + 8 * k), byy = y + (i / 2) * (ah + 8 * k);
        b.rc = {bx, byy, bx + bw, byy + ah};
        m_btns.push_back(b);
    }
    m_infoTop = y + 2 * (ah + 8 * k) + 10 * k;
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
    DrawGrid();
    DrawTree();
    DrawDetails();
    DrawTopbar();
    DrawStatus();
    HRESULT hr = rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
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

void App::DrawButton(const Btn& b) {
    bool hover = m_hoverBtn == b.id;
    bool enabled = !b.needsSelection || m_sel >= 0;
    unsigned bg = b.accent ? Theme::Accent : (hover && enabled ? Theme::CardHover : Theme::Card);
    float a = enabled ? 1.f : 0.45f;
    FillRR(b.rc, 6 * k, bg, a);
    if (b.accent && hover) FillRR(b.rc, 6 * k, 0xFFFFFF, 0.12f);
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
        if (!n->empty) Txt(n->expanded ? L"▾" : L"▸", {x, y, x + 16 * k, y + rh}, fCenter.Get(), Theme::TextDim);
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
        Txt(m_folder.empty() ? L"Ordner öffnen, um Bilder anzuzeigen" : L"Keine Bilder in diesem Ordner", rcGrid,
            fCenter.Get(), Theme::TextDim);
        rt->PopAxisAlignedClip();
        return;
    }
    GridM g = Metrics();
    int firstRow = std::max(0, (int)std::floor(m_gridScroll / g.cellH));
    int lastRow = std::min(g.rows - 1, (int)std::floor((m_gridScroll + H(rcGrid)) / g.cellH));
    for (int r = firstRow; r <= lastRow; ++r) {
        for (int c = 0; c < g.cols; ++c) {
            int idx = r * g.cols + c;
            if (idx >= (int)m_items.size()) break;
            float x = rcGrid.left + g.pad + c * g.cellW, y = rcGrid.top + g.pad + r * g.cellH - m_gridScroll;
            D2D1_RECT_F card{x, y, x + g.thumb, y + g.thumb};
            Item& it = m_items[idx];
            bool sel = idx == m_sel, hov = idx == m_hoverItem;
            FillRR(card, 8 * k, hov || sel ? Theme::CardHover : Theme::Card);
            if (it.thumb) DrawFit(it.thumb.Get(), Inset(card, 6 * k));
            else if (it.failed) Txt(L"⚠", card, fCenter.Get(), Theme::Warn);
            else FillRR(Inset(card, g.thumb * 0.35f), 6 * k, Theme::Border, 0.7f);  // Platzhalter
            if (sel) StrokeRR(card, 8 * k, Theme::Accent, 2 * k);
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
        row(L"Name", m_items[m_sel].name);
        wchar_t res[64];
        swprintf_s(res, L"%u × %u  (%.1f MP)", m_info.width, m_info.height, m_info.width * (double)m_info.height / 1e6);
        row(L"Auflösung", res);
        row(L"Dateigröße", FormatSize(m_info.fileSize));
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
    Txt(m_status, {12 * k, rcStatus.top, rcStatus.right * 0.6f, rcStatus.bottom}, fSmall.Get(), Theme::TextDim);
    wchar_t buf[96];
    swprintf_s(buf, L"%zu Bilder   ·   %zu Thumbnails geladen", m_items.size(), m_loaded);
    D2D1_RECT_F r{rcStatus.right * 0.6f, rcStatus.top, rcStatus.right - 12 * k, rcStatus.bottom};
    ComPtr<IDWriteTextFormat> right;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                         DWRITE_FONT_STRETCH_NORMAL, 11 * k, L"de-de", &right);
    right->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    right->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    Txt(buf, r, right.Get(), Theme::TextDim);
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
            std::wstring d = std::wstring(1, L'A' + i) + L":\\";
            addRoot(d, d);
        }
    Flatten();
}

void App::EnsureChildren(TreeNode* n) {
    if (n->loaded) return;
    n->loaded = true;
    WIN32_FIND_DATAW fd;
    std::wstring base = n->path;
    if (!base.empty() && base.back() != L'\\') base += L'\\';
    HANDLE h = FindFirstFileExW((base + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            auto c = std::make_unique<TreeNode>();
            c->name = fd.cFileName;
            c->path = base + fd.cFileName;
            c->depth = n->depth + 1;
            c->parent = n;
            n->children.push_back(std::move(c));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(n->children.begin(), n->children.end(),
              [](auto& a, auto& b) { return StrCmpLogicalW(a->name.c_str(), b->name.c_str()) < 0; });
    n->empty = n->children.empty();
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
    if (!n->expanded) EnsureChildren(n);
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

void App::OpenFolder(const std::wstring& path) {
    m_folder = path;
    m_items.clear();
    m_loaded = 0;
    m_sel = m_hoverItem = -1;
    m_preview.Reset();
    m_infoOk = false;
    m_gridScroll = m_gridTarget = 0;
    m_lastReqRow = -1;
    m_loader.NewGeneration();

    WIN32_FIND_DATAW fd;
    std::wstring base = path;
    if (!base.empty() && base.back() != L'\\') base += L'\\';
    HANDLE h = FindFirstFileExW((base + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (!imgops::IsSupportedImage(fd.cFileName)) continue;
            Item it;
            it.name = fd.cFileName;
            it.path = base + fd.cFileName;
            m_items.push_back(std::move(it));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(m_items.begin(), m_items.end(),
              [](const Item& a, const Item& b) { return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0; });

    SetStatus(path);
    SetWindowTextW(m_hwnd, (L"Duncan's Photo Commander – " + path).c_str());
    RequestVisible();
    Invalidate();
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

void App::Select(int idx) {
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

// Baut die Prioritaetenliste: Vorschau > sichtbar > 3 Zeilen Vorlauf (unten zuerst) > 2 Zeilen darueber.
void App::RequestVisible() {
    if (!rt) return;
    uint32_t gen = m_loader.Generation();
    std::vector<ThumbRequest> reqs;
    if (m_sel >= 0 && !m_preview && !m_previewFailed)
        reqs.push_back({gen, (size_t)m_sel, true, (UINT)std::lround(720 * k), m_items[m_sel].path});

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
    RequestVisible();
}

// ============================================================================
// Aktionen
// ============================================================================

void App::Command(int id) {
    switch (id) {
        case BTN_OPEN: BrowseForFolder(); break;
        case BTN_FULLSCREEN: ToggleFullscreen(); break;
        case BTN_BATCH: StartBatch(); break;
        case BTN_SETTINGS: ShowSettings(); break;
        case BTN_ROT_L: RotateSelected(false); break;
        case BTN_ROT_R: RotateSelected(true); break;
        case BTN_CROP: SetStatus(L"Zuschneiden: Editor folgt in Phase 2 (Platzhalter)."); break;
        case BTN_STRIP: StripSelected(); break;
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
    AppendMenuW(m, MF_STRING | (m_settings.overwriteOriginals ? MF_CHECKED : 0), MENU_OVERWRITE,
                L"EXIF entfernen: Original überschreiben");
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
            } else if (cmd == MENU_OVERWRITE) {
                m_settings.overwriteOriginals = !m_settings.overwriteOriginals;
            } else if (cmd == MENU_ABOUT) {
                MessageBoxW(m_hwnd, L"Duncan's Photo Commander\nTeil von Duncan's Suite\n\nGrundgerüst v0.1", L"Über",
                            MB_OK | MB_ICONINFORMATION);
            }
            break;
        }
    DestroyMenu(m);
}

void App::StripSelected() {
    if (m_sel < 0) return;
    const std::wstring src = m_items[m_sel].path;
    std::wstring dst = src, err;
    if (!m_settings.overwriteOriginals) {
        size_t dot = src.find_last_of(L'.');
        dst = src.substr(0, dot) + L"_clean" + src.substr(dot);
    } else if (MessageBoxW(m_hwnd, L"Alle Metadaten (inkl. Kamera und GPS) im ORIGINAL unwiderruflich entfernen?",
                           L"EXIF entfernen", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }
    if (!imgops::StripMetadata(src, dst, &err)) {
        SetStatus(L"EXIF entfernen fehlgeschlagen: " + err);
        return;
    }
    if (dst == src) {
        ReloadItem(m_sel);
        SetStatus(L"Metadaten entfernt: " + m_items[m_sel].name);
    } else {
        int keep = m_sel;
        std::wstring folder = m_folder;
        OpenFolder(folder);  // Kopie in die Liste aufnehmen
        for (int i = 0; i < (int)m_items.size(); ++i)
            if (m_items[i].path == dst) keep = i;
        Select(keep);
        SetStatus(L"Bereinigte Kopie erstellt: " + dst);
    }
}

void App::RotateSelected(bool cw) {
    if (m_sel < 0) return;
    std::wstring err;
    if (!imgops::RotateImage(m_items[m_sel].path, cw, &err)) {
        SetStatus(L"Drehen fehlgeschlagen: " + err);
        return;
    }
    ReloadItem(m_sel);
    SetStatus(cw ? L"90° rechts gedreht" : L"90° links gedreht");
}

void App::StartBatch() {
    if (m_batchRunning || m_items.empty()) return;
    if (MessageBoxW(m_hwnd,
                    L"Von allen Bildern im Ordner Kamera- und GPS-Daten entfernen?\n\n"
                    L"Die bereinigten Kopien werden im Unterordner \"bereinigt\" abgelegt, die Originale bleiben unverändert.",
                    L"Stapelverarbeitung", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    if (m_batch.joinable()) m_batch.join();
    std::vector<std::wstring> files;
    for (auto& it : m_items) files.push_back(it.path);
    std::wstring outDir = m_folder + (m_folder.back() == L'\\' ? L"" : L"\\") + L"bereinigt";
    CreateDirectoryW(outDir.c_str(), nullptr);
    m_batchRunning = true;
    m_batchCancel = false;
    HWND hwnd = m_hwnd;
    m_batch = std::thread([this, hwnd, files, outDir] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        WPARAM ok = 0, fail = 0;
        for (size_t i = 0; i < files.size() && !m_batchCancel; ++i) {
            std::wstring name = files[i].substr(files[i].find_last_of(L'\\') + 1);
            if (imgops::StripMetadata(files[i], outDir + L"\\" + name)) ++ok;
            else ++fail;
            PostMessageW(hwnd, WM_APP_BATCH, i + 1, (LPARAM)files.size());
        }
        CoUninitialize();
        m_batchRunning = false;
        PostMessageW(hwnd, WM_APP_BATCHDONE, ok, (LPARAM)fail);
    });
    SetStatus(L"Stapelverarbeitung läuft…");
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
        case WM_APP_THUMBS: Invalidate(); return 0;
        case WM_APP_BATCH: {
            wchar_t b[96];
            swprintf_s(b, L"Stapelverarbeitung: %zu / %zu", (size_t)wp, (size_t)lp);
            SetStatus(b);
            Invalidate();
            return 0;
        }
        case WM_APP_BATCHDONE: {
            wchar_t b[160];
            swprintf_s(b, L"Stapelverarbeitung fertig: %zu bereinigt, %zu übersprungen/fehlgeschlagen (Ordner \"bereinigt\")",
                       (size_t)wp, (size_t)lp);
            SetStatus(b);
            Invalidate();
            return 0;
        }
        case WM_TIMER:
            if (wp == TIMER_SCROLL) {
                float d = m_gridTarget - m_gridScroll;
                if (std::fabs(d) < 0.5f) { m_gridScroll = m_gridTarget; KillTimer(m_hwnd, TIMER_SCROLL); }
                else m_gridScroll += d * 0.3f;
                GridM g = Metrics();
                int row = (int)(m_gridScroll / g.cellH);
                if (row != m_lastReqRow) { m_lastReqRow = row; RequestVisible(); }
                Invalidate();
            }
            return 0;
        case WM_MOUSEMOVE: {
            float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
            if (!m_tracking) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, m_hwnd, 0};
                TrackMouseEvent(&t);
                m_tracking = true;
            }
            int hb = 0;
            for (auto& b : m_btns)
                if (In(b.rc, x, y) && (!b.needsSelection || m_sel >= 0)) hb = b.id;
            int hi = HitGrid(x, y), hr = -1;
            if (In(rcTree, x, y) && y > rcTree.top + 30 * k) {
                int r = (int)((y - rcTree.top - 30 * k + m_treeScroll) / (kRowH * k));
                if (r >= 0 && r < (int)m_rows.size()) hr = r;
            }
            if (hb != m_hoverBtn || hi != m_hoverItem || hr != m_hoverRow) {
                m_hoverBtn = hb; m_hoverItem = hi; m_hoverRow = hr;
                SetCursor(LoadCursor(nullptr, (hb || hi >= 0 || hr >= 0) ? IDC_HAND : IDC_ARROW));
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
            for (auto& b : m_btns)
                if (In(b.rc, x, y) && (!b.needsSelection || m_sel >= 0)) { Command(b.id); return 0; }
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
            int idx = HitGrid(x, y);
            if (In(rcGrid, x, y)) { SetFocus(m_hwnd); Select(idx); }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(m_hwnd, &pt);
            float delta = -GET_WHEEL_DELTA_WPARAM(wp) / 120.f;
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
            int n = (int)m_items.size();
            if (wp == VK_F11) { ToggleFullscreen(); return 0; }
            if (wp == VK_ESCAPE && m_full) { ToggleFullscreen(); return 0; }
            if (n == 0) return 0;
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
            Select(t);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(m_hwnd, TIMER_SCROLL);
            m_batchCancel = true;
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
        if (auto f = reinterpret_cast<SetMode>(GetProcAddress(ux, MAKEINTRESOURCEA(135)))) f(2 /*ForceDark*/);
        if (auto f = reinterpret_cast<Flush>(GetProcAddress(ux, MAKEINTRESOURCEA(136)))) f();
    }

    int code = 0;
    {
        App app;
        if (app.Create(hi, show)) {
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
