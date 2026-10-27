// Duncan's QuickConvert: lokaler Medien- und Dateikonverter (Win32 + GDI, WIC fuer Bilder, Media Foundation fuer Audio/Video).
// Dateien per Drag-and-Drop oder Dialog sammeln, Zielformat und Qualitaet waehlen, Stapel laeuft parallel in Worker-Threads.
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "Convert.h"

#ifndef APP_VERSION
#define APP_VERSION L"0.2.0"
#endif

namespace {

// ---- Farben (Catppuccin Mocha auf #1E1E2E) ----
constexpr COLORREF kBg = RGB(0x1E, 0x1E, 0x2E);
constexpr COLORREF kCtrl = RGB(0x2D, 0x2D, 0x3D);
constexpr COLORREF kRow = RGB(0x31, 0x32, 0x44);
constexpr COLORREF kText = RGB(0xCD, 0xD6, 0xF4);
constexpr COLORREF kDim = RGB(0xA6, 0xAD, 0xC8);
constexpr COLORREF kOverlay = RGB(0x6C, 0x70, 0x86);
constexpr COLORREF kAccent = RGB(0x89, 0xB4, 0xFA);
constexpr COLORREF kGreen = RGB(0xA6, 0xE3, 0xA1);
constexpr COLORREF kRed = RGB(0xF3, 0x8B, 0xA8);
constexpr COLORREF kOnAccent = RGB(0x1E, 0x1E, 0x2E);

enum : int {
    IDC_ADD = 100, IDC_OUT = 101, IDC_CLEAR = 102, IDC_GO = 103, IDC_OPEN = 104,
    IDC_IMG0 = 110,   // 110..114: PNG, JPG, WEBP, BMP, HEIC
    IDC_AUD0 = 120,   // 120..123: MP3, M4A, WAV, MP4
    IDM_OUT_PICK = 200, IDM_OUT_SAME = 201, IDM_OUT_SHOW = 202,
    IDM_OUT_RECENT0 = 210,   // 210..214: zuletzt benutzte Zielordner
};
constexpr int kAudCount = 4, kMaxRecent = 5;
constexpr UINT WM_APP_UPDATE = WM_APP + 1;   // ein Eintrag hat sich geaendert
constexpr UINT WM_APP_DONE = WM_APP + 2;     // alle Worker sind fertig

constexpr conv::ImageFmt kImgFmts[] = {conv::ImageFmt::Png, conv::ImageFmt::Jpeg, conv::ImageFmt::Webp, conv::ImageFmt::Bmp, conv::ImageFmt::Heic};
constexpr const wchar_t* kImgNames[] = {L"PNG", L"JPG", L"WEBP", L"BMP", L"HEIC"};
constexpr conv::AudioFmt kAudFmts[kAudCount] = {conv::AudioFmt::Mp3, conv::AudioFmt::M4a, conv::AudioFmt::Wav, conv::AudioFmt::Mp4};
constexpr const wchar_t* kAudNames[kAudCount] = {L"MP3", L"M4A", L"WAV", L"MP4"};

const wchar_t* AudName(conv::AudioFmt f) {
    for (int i = 0; i < kAudCount; ++i)
        if (kAudFmts[i] == f) return kAudNames[i];
    return L"";
}

enum class State { Waiting, Running, Done, Failed };

struct Item {
    std::wstring path;
    conv::Kind kind = conv::Kind::Unknown;
    State state = State::Waiting;
    int pct = 0;
    std::wstring text;      // Ergebnisdatei (Done) oder Fehlertext (Failed)
    std::wstring outPath;   // vollstaendiger Pfad der Ergebnisdatei (Done)
};

struct App {
    HWND hwnd = nullptr;
    UINT dpi = 96;
    HFONT fTitle = nullptr, fBody = nullptr, fBold = nullptr, fSmall = nullptr;

    std::vector<Item> items;
    std::mutex mu;   // schuetzt Felder der Items, solange Worker laufen

    int imgSel = 1;      // JPG
    int audSel = 0;      // MP3
    int quality = 85;
    std::wstring outDir; // leer: neben der Originaldatei
    std::vector<std::wstring> recent;   // zuletzt benutzte Zielordner (neueste zuerst)
    bool openFolder = false;            // nach dem Stapel den Zielordner im Explorer oeffnen

    bool running = false;
    std::atomic<bool> cancel{false};
    std::vector<std::thread> workers;
    std::atomic<int> workersLeft{0};
    std::atomic<size_t> nextJob{0};
    std::set<std::wstring> reserved;   // in diesem Lauf vergebene Zielpfade (klein geschrieben)
    std::wstring note;

    RECT zone{}, slider{};
    int scroll = 0;
    bool dragSlider = false;
    HWND btnAdd = nullptr, btnOut = nullptr, btnClear = nullptr, btnGo = nullptr, btnOpen = nullptr;
    HWND chipImg[5]{}, chipAud[kAudCount]{};

    int S(int v) const { return MulDiv(v, static_cast<int>(dpi), 96); }
} g;

// ---------------------------------------------------------------------------------------------------------------------
// Hilfen
// ---------------------------------------------------------------------------------------------------------------------

std::wstring IniPath() {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)) || !p) return L"";
    std::wstring dir = std::wstring(p) + L"\\DuncansQuickConvert";
    CoTaskMemFree(p);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

void LoadSettings() {
    const std::wstring ini = IniPath();
    if (ini.empty()) return;
    auto str = [&](const wchar_t* sec, const wchar_t* key) {
        wchar_t buf[1024]{};
        GetPrivateProfileStringW(sec, key, L"", buf, 1024, ini.c_str());
        return std::wstring(buf);
    };
    g.imgSel = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Allgemein", L"BildFormat", g.imgSel, ini.c_str())), 0, 4);
    g.audSel = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Allgemein", L"AudioFormat", g.audSel, ini.c_str())), 0, kAudCount - 1);
    g.quality = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Allgemein", L"Qualitaet", g.quality, ini.c_str())), 0, 100);
    g.openFolder = GetPrivateProfileIntW(L"Allgemein", L"OrdnerOeffnen", 0, ini.c_str()) != 0;
    g.outDir = str(L"Allgemein", L"Zielordner");
    if (!g.outDir.empty() && !(GetFileAttributesW(g.outDir.c_str()) & FILE_ATTRIBUTE_DIRECTORY)) g.outDir.clear();   // Ordner gibt es nicht mehr
    g.recent.clear();
    for (int i = 0; i < kMaxRecent; ++i) {
        const std::wstring d = str(L"Zuletzt", (L"Ordner" + std::to_wstring(i)).c_str());
        if (!d.empty()) g.recent.push_back(d);
    }
}

void SaveSettings() {
    const std::wstring ini = IniPath();
    if (ini.empty()) return;
    auto put = [&](const wchar_t* sec, const wchar_t* key, const std::wstring& v) { WritePrivateProfileStringW(sec, key, v.c_str(), ini.c_str()); };
    put(L"Allgemein", L"BildFormat", std::to_wstring(g.imgSel));
    put(L"Allgemein", L"AudioFormat", std::to_wstring(g.audSel));
    put(L"Allgemein", L"Qualitaet", std::to_wstring(g.quality));
    put(L"Allgemein", L"OrdnerOeffnen", g.openFolder ? L"1" : L"0");
    put(L"Allgemein", L"Zielordner", g.outDir);
    for (int i = 0; i < kMaxRecent; ++i) put(L"Zuletzt", (L"Ordner" + std::to_wstring(i)).c_str(), i < static_cast<int>(g.recent.size()) ? g.recent[i] : L"");
}

std::wstring Lower(std::wstring s) { for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c)); return s; }

std::wstring FileName(const std::wstring& p) {
    const size_t i = p.find_last_of(L"\\/");
    return i == std::wstring::npos ? p : p.substr(i + 1);
}
std::wstring DirOf(const std::wstring& p) {
    const size_t i = p.find_last_of(L"\\/");
    return i == std::wstring::npos ? L"." : p.substr(0, i);
}
std::wstring StemOf(const std::wstring& name) {
    const size_t i = name.find_last_of(L'.');
    return i == std::wstring::npos || i == 0 ? name : name.substr(0, i);
}

bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// Zielpfad, der weder existiert noch schon in diesem Lauf vergeben ist und nie die Quelle selbst trifft. Aufruf unter g.mu.
std::wstring UniqueTarget(const std::wstring& dir, const std::wstring& stem, const std::wstring& ext, const std::wstring& src) {
    for (int n = 0;; ++n) {
        std::wstring name = stem;
        if (n == 1) name += L" (konvertiert)";
        else if (n > 1) name += L" (konvertiert " + std::to_wstring(n) + L")";
        const std::wstring cand = dir + L"\\" + name + L"." + ext;
        const std::wstring key = Lower(cand);
        if (key != Lower(src) && !Exists(cand) && !g.reserved.count(key)) {
            g.reserved.insert(key);
            return cand;
        }
    }
}

void AddPath(const std::wstring& path, int& added, int& skipped, int depth = 0) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        if (depth > 4) return;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            AddPath(path + L"\\" + n, added, skipped, depth + 1);
        } while (FindNextFileW(h, &fd) && g.items.size() < 5000);
        FindClose(h);
        return;
    }
    const conv::Kind k = conv::KindOfFile(path);
    if (k == conv::Kind::Unknown) { ++skipped; return; }
    const std::wstring key = Lower(path);
    for (const Item& it : g.items) if (Lower(it.path) == key) return;
    Item it;
    it.path = path;
    it.kind = k;
    g.items.push_back(std::move(it));
    ++added;
}

void AddPaths(const std::vector<std::wstring>& paths) {
    if (g.running) { g.note = L"Während der Konvertierung können keine Dateien hinzugefügt werden"; return; }
    int added = 0, skipped = 0;
    for (const auto& p : paths) AddPath(p, added, skipped);
    g.note = std::to_wstring(added) + (added == 1 ? L" Datei hinzugefügt" : L" Dateien hinzugefügt");
    if (skipped) g.note += L", " + std::to_wstring(skipped) + L" nicht unterstützt";
}

std::wstring TargetLabel(const Item& it) {
    return it.kind == conv::Kind::Image ? kImgNames[g.imgSel] : AudName(conv::EffectiveFmt(it.path, kAudFmts[g.audSel]));
}

// Zielordner setzen und an die Spitze der Zuletzt-Liste stellen
void UseOutDir(const std::wstring& dir) {
    g.outDir = dir;
    if (dir.empty()) return;
    g.recent.erase(std::remove_if(g.recent.begin(), g.recent.end(), [&](const std::wstring& r) { return Lower(r) == Lower(dir); }), g.recent.end());
    g.recent.insert(g.recent.begin(), dir);
    if (g.recent.size() > static_cast<size_t>(kMaxRecent)) g.recent.resize(kMaxRecent);
}

std::wstring QualityDetail() {
    std::wstring s = L"Bilder (JPG, WEBP, HEIC): " + std::to_wstring(g.quality) + L" %   ·   ";
    const int kb = conv::BitrateKbps(kAudFmts[g.audSel], g.quality);
    if (kAudFmts[g.audSel] == conv::AudioFmt::Mp4) s += L"MP4: H.264 nach Auflösung, Ton " + std::to_wstring(kb) + L" kbit/s";
    else s += kb ? std::wstring(kAudNames[g.audSel]) + L": " + std::to_wstring(kb) + L" kbit/s" : L"WAV: verlustfrei";
    return s;
}

// ---------------------------------------------------------------------------------------------------------------------
// Stapelverarbeitung
// ---------------------------------------------------------------------------------------------------------------------

struct Settings {
    conv::ImageFmt img;
    conv::AudioFmt aud;
    int quality;
    std::wstring outDir;
};

void Worker(HWND hwnd, std::shared_ptr<Settings> s) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        const size_t i = g.nextJob.fetch_add(1);
        std::wstring src, target;
        conv::Kind kind;
        {
            std::lock_guard<std::mutex> lk(g.mu);
            if (i >= g.items.size()) break;
            Item& it = g.items[i];
            if (g.cancel.load()) { it.state = State::Failed; it.text = L"Abgebrochen"; PostMessageW(hwnd, WM_APP_UPDATE, i, 0); continue; }
            src = it.path;
            kind = it.kind;
            const std::wstring ext = kind == conv::Kind::Image ? conv::ImageExt(s->img) : conv::AudioExt(conv::EffectiveFmt(src, s->aud));
            target = UniqueTarget(s->outDir.empty() ? DirOf(src) : s->outDir, StemOf(FileName(src)), ext, src);
            it.state = State::Running;
            it.pct = 0;
            it.text.clear();
        }
        PostMessageW(hwnd, WM_APP_UPDATE, i, 0);

        std::wstring err;
        bool ok = false;
        if (kind == conv::Kind::Image) {
            ok = conv::ConvertImage(src, target, s->img, s->quality, &err);
        } else {
            ok = conv::ConvertMedia(src, target, s->aud, s->quality, g.cancel, [hwnd, i](int pct) {
                {
                    std::lock_guard<std::mutex> lk(g.mu);
                    g.items[i].pct = pct;
                }
                PostMessageW(hwnd, WM_APP_UPDATE, i, 0);
            }, &err);
        }
        {
            std::lock_guard<std::mutex> lk(g.mu);
            Item& it = g.items[i];
            it.state = ok ? State::Done : State::Failed;
            it.pct = ok ? 100 : it.pct;
            it.text = ok ? FileName(target) : err;
            it.outPath = ok ? target : std::wstring();
        }
        PostMessageW(hwnd, WM_APP_UPDATE, i, 0);
    }
    CoUninitialize();
    if (g.workersLeft.fetch_sub(1) == 1) PostMessageW(hwnd, WM_APP_DONE, 0, 0);
}

void JoinWorkers() {
    for (auto& t : g.workers) if (t.joinable()) t.join();
    g.workers.clear();
}

void SetRunning(bool run);

void StartBatch() {
    if (g.running || g.items.empty()) return;
    JoinWorkers();
    {
        std::lock_guard<std::mutex> lk(g.mu);
        for (Item& it : g.items) { it.state = State::Waiting; it.pct = 0; it.text.clear(); }
        g.reserved.clear();
    }
    auto s = std::make_shared<Settings>(Settings{kImgFmts[g.imgSel], kAudFmts[g.audSel], g.quality, g.outDir});
    g.cancel = false;
    g.nextJob = 0;
    // Bilder sind CPU-bound, Medien-Encoder laufen teils selbst mehrfaedig: Threads begrenzen
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const size_t n = std::min<size_t>({g.items.size(), static_cast<size_t>(std::min(hw, 8u))});
    g.workersLeft = static_cast<int>(n);
    SetRunning(true);
    g.note = L"Konvertiere …";
    for (size_t k = 0; k < n; ++k) g.workers.emplace_back(Worker, g.hwnd, s);
}

// ---------------------------------------------------------------------------------------------------------------------
// Dialoge
// ---------------------------------------------------------------------------------------------------------------------

void PickFiles() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opt = 0;
    dlg->GetOptions(&opt);
    dlg->SetOptions(opt | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM);
    const COMDLG_FILTERSPEC filters[] = {
        {L"Alle unterstützten Dateien", L"*.png;*.jpg;*.jpeg;*.jfif;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.heic;*.heif;*.jxr;*.ico;"
                                        L"*.mp3;*.wav;*.m4a;*.aac;*.wma;*.flac;*.mp4;*.m4v;*.mov;*.wmv;*.avi;*.mkv;*.webm;*.mpg;*.mpeg;*.3gp"},
        {L"Alle Dateien", L"*.*"}};
    dlg->SetFileTypes(2, filters);
    dlg->SetTitle(L"Dateien zum Konvertieren wählen");
    if (SUCCEEDED(dlg->Show(g.hwnd))) {
        IShellItemArray* arr = nullptr;
        if (SUCCEEDED(dlg->GetResults(&arr))) {
            DWORD count = 0;
            arr->GetCount(&count);
            std::vector<std::wstring> paths;
            for (DWORD i = 0; i < count; ++i) {
                IShellItem* it = nullptr;
                if (SUCCEEDED(arr->GetItemAt(i, &it))) {
                    PWSTR p = nullptr;
                    if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { paths.emplace_back(p); CoTaskMemFree(p); }
                    it->Release();
                }
            }
            arr->Release();
            AddPaths(paths);
        }
    }
    dlg->Release();
}

void PickFolder() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opt = 0;
    dlg->GetOptions(&opt);
    dlg->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(L"Zielordner wählen");
    if (SUCCEEDED(dlg->Show(g.hwnd))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(dlg->GetResult(&it))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { UseOutDir(p); CoTaskMemFree(p); }
            it->Release();
        }
    }
    dlg->Release();
}

// ---------------------------------------------------------------------------------------------------------------------
// Layout und Zeichnen
// ---------------------------------------------------------------------------------------------------------------------

void MakeFonts() {
    for (HFONT* f : {&g.fTitle, &g.fBody, &g.fBold, &g.fSmall}) if (*f) DeleteObject(*f);
    auto mk = [](int pt, int weight) {
        return CreateFontW(-g.S(pt), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    g.fTitle = mk(22, FW_SEMIBOLD);
    g.fBody = mk(14, FW_NORMAL);
    g.fBold = mk(14, FW_SEMIBOLD);
    g.fSmall = mk(12, FW_NORMAL);
}

constexpr int kRowH = 54, kRowGap = 8;

int ContentHeight() { return static_cast<int>(g.items.size()) * g.S(kRowH + kRowGap); }
int MaxScroll() { return std::max(0, ContentHeight() - (static_cast<int>(g.zone.bottom - g.zone.top) - g.S(16))); }

void Relayout() {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    const int m = g.S(24), w = rc.right, h = rc.bottom;
    g.zone = {m, g.S(84), w - m, h - g.S(236)};
    const int y0 = g.zone.bottom + g.S(16);
    const int colW = g.S(190), colX = w - m - colW;

    int x = m + g.S(120);
    for (int i = 0; i < 5; ++i, x += g.S(72)) MoveWindow(g.chipImg[i], x, y0, g.S(64), g.S(30), TRUE);
    x = m + g.S(120);
    for (int i = 0; i < kAudCount; ++i, x += g.S(72)) MoveWindow(g.chipAud[i], x, y0 + g.S(40), g.S(64), g.S(30), TRUE);
    MoveWindow(g.btnOpen, m, y0 + g.S(146), g.S(400), g.S(26), TRUE);

    const int sx0 = m + g.S(120), sx1 = std::max(sx0 + g.S(80), std::min(sx0 + g.S(300), colX - g.S(24)));
    g.slider = {sx0, y0 + g.S(88), sx1, y0 + g.S(108)};

    MoveWindow(g.btnGo, colX, y0, colW, g.S(46), TRUE);
    MoveWindow(g.btnAdd, colX, y0 + g.S(56), (colW - g.S(8)) / 2, g.S(30), TRUE);
    MoveWindow(g.btnClear, colX + (colW + g.S(8)) / 2, y0 + g.S(56), (colW - g.S(8)) / 2, g.S(30), TRUE);
    MoveWindow(g.btnOut, colX, y0 + g.S(94), colW, g.S(30), TRUE);
    g.scroll = std::min(g.scroll, MaxScroll());
}

void FillRound(HDC dc, const RECT& r, int radius, COLORREF fill) {
    HBRUSH b = CreateSolidBrush(fill);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, radius * 2, radius * 2);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(b);
}

void Text(HDC dc, HFONT f, COLORREF c, RECT r, const std::wstring& s, UINT fmt = DT_LEFT) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s.c_str(), -1, &r, fmt | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}

RECT RemoveBox(const RECT& row) {
    const int s = g.S(22);
    return {row.right - g.S(14) - s, row.top + (row.bottom - row.top - s) / 2, row.right - g.S(14), row.top + (row.bottom - row.top - s) / 2 + s};
}

void PaintDropZone(HDC dc) {
    const RECT z = g.zone;
    FillRound(dc, z, g.S(14), kCtrl);

    if (g.items.empty()) {
        Text(dc, g.fTitle, kText, {z.left, z.top, z.right, (z.top + z.bottom) / 2}, L"Dateien hierher ziehen", DT_CENTER | DT_BOTTOM);
        Text(dc, g.fBody, kDim, {z.left, (z.top + z.bottom) / 2 + g.S(6), z.right, z.bottom},
             L"Bilder, Audio und Video – oder „Hinzufügen“ wählen", DT_CENTER | DT_TOP);
    } else {
        const int save = SaveDC(dc);
        IntersectClipRect(dc, z.left + g.S(2), z.top + g.S(6), z.right - g.S(2), z.bottom - g.S(6));
        std::lock_guard<std::mutex> lk(g.mu);
        int y = z.top + g.S(8) - g.scroll;
        for (const Item& it : g.items) {
            const RECT row = {z.left + g.S(10), y, z.right - g.S(10 + (MaxScroll() > 0 ? 8 : 0)), y + g.S(kRowH)};
            y += g.S(kRowH + kRowGap);
            if (row.bottom < z.top || row.top > z.bottom) continue;
            FillRound(dc, row, g.S(10), kRow);

            const RECT rb = RemoveBox(row);
            const int pad = g.S(14);
            const int badgeW = g.S(70);
            const std::wstring target = TargetLabel(it);
            Text(dc, g.fBold, kText, {row.left + pad, row.top + g.S(6), rb.left - badgeW - pad, row.top + g.S(30)}, FileName(it.path));

            std::wstring status;
            COLORREF sc = kDim;
            switch (it.state) {
                case State::Waiting: status = (it.kind == conv::Kind::Image ? L"Bild" : L"Audio/Video") + std::wstring(L" · wartet"); break;
                case State::Running: status = it.kind == conv::Kind::Media ? std::to_wstring(it.pct) + L" %" : L"Wird konvertiert …"; sc = kAccent; break;
                case State::Done: status = L"Fertig → " + it.text; sc = kGreen; break;
                case State::Failed: status = it.text; sc = kRed; break;
            }
            Text(dc, g.fSmall, sc, {row.left + pad, row.top + g.S(28), rb.left - badgeW - pad, row.bottom - g.S(8)}, status);

            RECT badge = {rb.left - badgeW, row.top + g.S(13), rb.left - g.S(8), row.top + g.S(13) + g.S(26)};
            FillRound(dc, badge, g.S(8), kCtrl);
            Text(dc, g.fSmall, kAccent, badge, L"→ " + target, DT_CENTER);

            if (!g.running) Text(dc, g.fBold, kOverlay, rb, L"✕", DT_CENTER);

            if (it.state == State::Running || it.state == State::Done) {
                const int bw = row.right - row.left - 2 * g.S(10);
                RECT bar = {row.left + g.S(10), row.bottom - g.S(6), row.left + g.S(10) + bw, row.bottom - g.S(3)};
                FillRound(dc, bar, g.S(1), kCtrl);
                bar.right = bar.left + bw * (it.state == State::Done ? 100 : it.pct) / 100;
                if (bar.right > bar.left) FillRound(dc, bar, g.S(1), it.state == State::Done ? kGreen : kAccent);
            }
        }
        RestoreDC(dc, save);

        const int ms = MaxScroll();
        if (ms > 0) {   // schmaler Rollbalken
            const int trackH = z.bottom - z.top - g.S(24);
            const int thumbH = std::max(g.S(30), trackH * trackH / (trackH + ms));
            const int ty = z.top + g.S(12) + (trackH - thumbH) * g.scroll / ms;
            FillRound(dc, {z.right - g.S(10), ty, z.right - g.S(6), ty + thumbH}, g.S(2), kOverlay);
        }
    }

    // gestrichelter Rahmen
    LOGBRUSH lb = {BS_SOLID, g.items.empty() ? kOverlay : RGB(0x45, 0x47, 0x5A), 0};
    DWORD dash[2] = {static_cast<DWORD>(g.S(8)), static_cast<DWORD>(g.S(6))};
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_USERSTYLE | PS_ENDCAP_FLAT, std::max(2, g.S(2)), &lb, 2, dash);
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, z.left, z.top, z.right, z.bottom, g.S(28), g.S(28));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
}

void PaintSlider(HDC dc) {
    const RECT s = g.slider;
    const int cy = (s.top + s.bottom) / 2, th = g.S(6), r = g.S(9);
    const int tx0 = s.left + r, tx1 = s.right - r;
    const int tx = tx0 + (tx1 - tx0) * g.quality / 100;
    FillRound(dc, {tx0 - r / 2, cy - th / 2, tx1 + r / 2, cy + th / 2}, th / 2, kCtrl);
    FillRound(dc, {tx0 - r / 2, cy - th / 2, tx, cy + th / 2}, th / 2, kAccent);
    HBRUSH b = CreateSolidBrush(kText);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, tx - r, cy - r, tx + r + 1, cy + r + 1);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(b);
}

void Paint(HDC dc, const RECT& rc) {
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    const int m = g.S(24);

    Text(dc, g.fTitle, kText, {m, g.S(14), rc.right - m, g.S(48)}, L"Duncan's QuickConvert");
    Text(dc, g.fSmall, kDim, {m, g.S(48), rc.right - m, g.S(72)},
         L"Konvertiert Bilder, Audio und Video lokal auf deinem PC – nichts wird hochgeladen");

    PaintDropZone(dc);

    const int y0 = g.zone.bottom + g.S(16);
    Text(dc, g.fBody, kDim, {m, y0, m + g.S(112), y0 + g.S(30)}, L"Bilder → ");
    Text(dc, g.fBody, kDim, {m, y0 + g.S(40), m + g.S(112), y0 + g.S(70)}, L"Audio/Video →");
    Text(dc, g.fBody, kDim, {m, g.slider.top - g.S(2), m + g.S(112), g.slider.bottom + g.S(2)}, L"Qualität");
    PaintSlider(dc);
    Text(dc, g.fBold, kText, {g.slider.right + g.S(12), g.slider.top - g.S(2), g.slider.right + g.S(70), g.slider.bottom + g.S(2)},
         std::to_wstring(g.quality) + L" %");
    Text(dc, g.fSmall, kDim, {m + g.S(120), g.slider.bottom + g.S(8), g.slider.right + g.S(260), g.slider.bottom + g.S(30)}, QualityDetail());

    int done = 0, failed = 0;
    {
        std::lock_guard<std::mutex> lk(g.mu);
        for (const Item& it : g.items) { done += it.state == State::Done; failed += it.state == State::Failed; }
    }
    std::wstring status = g.note;
    if (g.running) status = L"Konvertiere … " + std::to_wstring(done + failed) + L" von " + std::to_wstring(g.items.size());
    Text(dc, g.fSmall, failed && !g.running ? kRed : kDim, {m, rc.bottom - g.S(44), rc.right - g.S(120), rc.bottom - g.S(18)}, status);
    Text(dc, g.fSmall, kOverlay, {rc.right - g.S(120) - m, rc.bottom - g.S(44), rc.right - m, rc.bottom - g.S(18)},
         std::wstring(L"v") + APP_VERSION, DT_RIGHT);
}

void SetRunning(bool run) {
    g.running = run;
    SetWindowTextW(g.btnGo, run ? L"Abbrechen" : L"Konvertieren");
    for (HWND h : {g.btnAdd, g.btnClear, g.btnOut}) EnableWindow(h, !run);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

bool IsChipSelected(int id) {
    if (id >= IDC_IMG0 && id < IDC_IMG0 + 5) return g.imgSel == id - IDC_IMG0;
    if (id >= IDC_AUD0 && id < IDC_AUD0 + kAudCount) return g.audSel == id - IDC_AUD0;
    return false;
}

void DrawButton(const DRAWITEMSTRUCT* d) {
    const int id = static_cast<int>(d->CtlID);
    const bool chip = id >= IDC_IMG0;
    const bool primary = id == IDC_GO;
    const bool disabled = (d->itemState & ODS_DISABLED) != 0;
    const bool pressed = (d->itemState & ODS_SELECTED) != 0;
    const bool selected = chip && IsChipSelected(id);

    if (id == IDC_OPEN) {   // Checkbox: Kaestchen + Beschriftung
        HBRUSH bgb = CreateSolidBrush(kBg);
        FillRect(d->hDC, &d->rcItem, bgb);
        DeleteObject(bgb);
        const int bx = g.S(18), top = d->rcItem.top + (d->rcItem.bottom - d->rcItem.top - bx) / 2;
        const RECT box = {d->rcItem.left, top, d->rcItem.left + bx, top + bx};
        FillRound(d->hDC, box, g.S(5), g.openFolder ? kAccent : kCtrl);
        if (g.openFolder) {   // Haken
            HPEN pen = CreatePen(PS_SOLID, std::max(2, g.S(2)), kOnAccent);
            HGDIOBJ op = SelectObject(d->hDC, pen);
            MoveToEx(d->hDC, box.left + g.S(4), top + bx / 2, nullptr);
            LineTo(d->hDC, box.left + bx * 2 / 5, box.bottom - g.S(5));
            LineTo(d->hDC, box.right - g.S(4), box.top + g.S(5));
            SelectObject(d->hDC, op);
            DeleteObject(pen);
        }
        SetBkMode(d->hDC, TRANSPARENT);
        Text(d->hDC, g.fBody, disabled ? kOverlay : kText, {box.right + g.S(10), d->rcItem.top, d->rcItem.right, d->rcItem.bottom},
             L"Ordner nach Konvertierung im Explorer öffnen");
        return;
    }

    COLORREF fill = kCtrl, fg = kText;
    if (primary) { fill = g.running ? kRed : kAccent; fg = kOnAccent; }
    else if (selected) { fill = kAccent; fg = kOnAccent; }
    if (pressed && !selected) fill = primary ? (g.running ? RGB(0xD0, 0x70, 0x8C) : RGB(0x74, 0x9E, 0xE0)) : RGB(0x3A, 0x3B, 0x50);
    if (disabled) { fill = kCtrl; fg = kOverlay; }

    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(d->hDC, &d->rcItem, bg);
    DeleteObject(bg);
    FillRound(d->hDC, d->rcItem, g.S(8), fill);
    wchar_t buf[64];
    GetWindowTextW(d->hwndItem, buf, 64);
    SetBkMode(d->hDC, TRANSPARENT);
    std::wstring label = buf;
    if (id == IDC_OUT) {
        label = (g.outDir.empty() ? L"Ausgabe: neben Original" : L"Ausgabe: " + FileName(g.outDir)) + L"  ▾";
    }
    Text(d->hDC, primary || selected ? g.fBold : g.fBody, fg, d->rcItem, label, DT_CENTER);
}

// ---------------------------------------------------------------------------------------------------------------------
// Fenster
// ---------------------------------------------------------------------------------------------------------------------

int SliderToValue(int x) {
    const int r = g.S(9), x0 = g.slider.left + r, x1 = g.slider.right - r;
    return std::clamp((x - x0) * 100 / std::max(1, x1 - x0), 0, 100);
}

void ShowOutMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (g.outDir.empty() ? MF_CHECKED : 0), IDM_OUT_SAME, L"Neben der Originaldatei speichern");
    if (!g.recent.empty()) {
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        for (size_t i = 0; i < g.recent.size(); ++i) {
            std::wstring label;   // "&" im Pfad wuerde im Menue als Tastenkuerzel gelten
            for (wchar_t c : g.recent[i]) { if (c == L'&') label += L'&'; label += c; }
            AppendMenuW(m, MF_STRING | (Lower(g.recent[i]) == Lower(g.outDir) ? MF_CHECKED : 0), IDM_OUT_RECENT0 + static_cast<UINT>(i), label.c_str());
        }
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_OUT_PICK, L"Zielordner wählen …");
    AppendMenuW(m, MF_STRING | (g.outDir.empty() ? MF_GRAYED : 0), IDM_OUT_SHOW, L"Zielordner im Explorer öffnen");
    RECT r;
    GetWindowRect(g.btnOut, &r);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_BOTTOMALIGN, r.left, r.top, 0, g.hwnd, nullptr);
    DestroyMenu(m);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.hwnd = hwnd;
        g.dpi = GetDpiForWindow(hwnd);
        LoadSettings();
        MakeFonts();
        auto btn = [&](const wchar_t* text, int id) {
            return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        };
        for (int i = 0; i < 5; ++i) g.chipImg[i] = btn(kImgNames[i], IDC_IMG0 + i);
        for (int i = 0; i < kAudCount; ++i) g.chipAud[i] = btn(kAudNames[i], IDC_AUD0 + i);
        g.btnGo = btn(L"Konvertieren", IDC_GO);
        g.btnAdd = btn(L"Hinzufügen", IDC_ADD);
        g.btnClear = btn(L"Leeren", IDC_CLEAR);
        g.btnOut = btn(L"Ausgabe", IDC_OUT);
        g.btnOpen = btn(L"Ordner öffnen", IDC_OPEN);
        DragAcceptFiles(hwnd, TRUE);
        // Drag-and-Drop auch erlauben, wenn der Explorer mit anderen Rechten laeuft
        ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
        ChangeWindowMessageFilterEx(hwnd, 0x0049 /*WM_COPYGLOBALDATA*/, MSGFLT_ALLOW, nullptr);
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
        const COLORREF cap = kBg;
        DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof(cap));
        Relayout();
        g.note = L"Bereit";
        return 0;
    }
    case WM_SIZE:
        if (g.btnGo) Relayout();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize = {g.S(760), g.S(600)};
        return 0;
    }
    case WM_DPICHANGED: {
        g.dpi = HIWORD(wp);
        MakeFonts();
        const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Relayout();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {   // doppelt gepuffert; Kind-Buttons zeichnen sich selbst (WS_CLIPCHILDREN ist nicht noetig, da sie darueberliegen)
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        Paint(mem, rc);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        return TRUE;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wp);
        const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> paths;
        for (UINT i = 0; i < n; ++i) {
            const UINT len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring p(len + 1, L'\0');
            DragQueryFileW(drop, i, &p[0], len + 1);
            p.resize(len);
            paths.push_back(std::move(p));
        }
        DragFinish(drop);
        AddPaths(paths);
        SetForegroundWindow(hwnd);
        Relayout();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (id >= IDC_IMG0 && id < IDC_IMG0 + 5) { g.imgSel = id - IDC_IMG0; }
        else if (id >= IDC_AUD0 && id < IDC_AUD0 + kAudCount) { g.audSel = id - IDC_AUD0; }
        else if (id >= IDM_OUT_RECENT0 && id < IDM_OUT_RECENT0 + kMaxRecent) {
            if (const size_t i = static_cast<size_t>(id - IDM_OUT_RECENT0); i < g.recent.size()) {
                const std::wstring dir = g.recent[i];
                if (GetFileAttributesW(dir.c_str()) & FILE_ATTRIBUTE_DIRECTORY) UseOutDir(dir);
                else { g.recent.erase(g.recent.begin() + static_cast<std::ptrdiff_t>(i)); g.note = L"Der Ordner existiert nicht mehr"; }
            }
        }
        else switch (id) {
            case IDC_ADD: PickFiles(); Relayout(); break;
            case IDC_CLEAR: g.items.clear(); g.scroll = 0; g.note = L"Liste geleert"; break;
            case IDC_OUT: ShowOutMenu(); break;
            case IDM_OUT_PICK: PickFolder(); break;
            case IDM_OUT_SAME: g.outDir.clear(); break;
            case IDM_OUT_SHOW: ShellExecuteW(hwnd, L"open", g.outDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
            case IDC_OPEN: g.openFolder = !g.openFolder; break;
            case IDC_GO:
                if (g.running) { g.cancel = true; g.note = L"Wird abgebrochen …"; }
                else if (g.items.empty()) { g.note = L"Zuerst Dateien hinzufügen"; }
                else StartBatch();
                break;
            default: return DefWindowProcW(hwnd, msg, wp, lp);
        }
        SaveSettings();
        for (HWND c : g.chipImg) InvalidateRect(c, nullptr, FALSE);
        for (HWND c : g.chipAud) InvalidateRect(c, nullptr, FALSE);
        InvalidateRect(g.btnOut, nullptr, FALSE);
        InvalidateRect(g.btnOpen, nullptr, FALSE);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_APP_UPDATE:
        InvalidateRect(hwnd, &g.zone, FALSE);
        {
            RECT st = {0, g.zone.bottom, 4000, 4000};
            InvalidateRect(hwnd, &st, FALSE);
        }
        return 0;
    case WM_APP_DONE: {
        JoinWorkers();
        int ok = 0, fail = 0;
        for (const Item& it : g.items) { ok += it.state == State::Done; fail += it.state == State::Failed; }
        g.note = g.cancel ? L"Abgebrochen – " + std::to_wstring(ok) + L" fertig"
                          : L"Fertig: " + std::to_wstring(ok) + (ok == 1 ? L" Datei" : L" Dateien") + L" konvertiert" +
                                (fail ? L", " + std::to_wstring(fail) + L" mit Fehler" : L"");
        SetRunning(false);
        if (g.openFolder && ok > 0 && !g.cancel) {
            std::set<std::wstring> seen;   // je Ordner einmal, die erste Datei wird markiert; hoechstens 3 Fenster
            for (const Item& it : g.items) {
                if (it.state != State::Done || it.outPath.empty() || seen.size() >= 3) continue;
                if (!seen.insert(Lower(DirOf(it.outPath))).second) continue;
                ShellExecuteW(hwnd, L"open", L"explorer.exe", (L"/select,\"" + it.outPath + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
            }
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT hit = g.slider;
        InflateRect(&hit, g.S(8), g.S(8));
        if (PtInRect(&hit, pt)) {
            g.dragSlider = true;
            SetCapture(hwnd);
            g.quality = SliderToValue(pt.x);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (!g.running && PtInRect(&g.zone, pt)) {   // ✕ entfernt einen Eintrag
            int y = g.zone.top + g.S(8) - g.scroll;
            for (size_t i = 0; i < g.items.size(); ++i, y += g.S(kRowH + kRowGap)) {
                const RECT row = {g.zone.left + g.S(10), y, g.zone.right - g.S(10 + (MaxScroll() > 0 ? 8 : 0)), y + g.S(kRowH)};
                RECT rb = RemoveBox(row);
                InflateRect(&rb, g.S(6), g.S(6));
                if (PtInRect(&rb, pt)) {
                    g.items.erase(g.items.begin() + static_cast<std::ptrdiff_t>(i));
                    g.scroll = std::min(g.scroll, MaxScroll());
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                }
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g.dragSlider) {
            g.quality = SliderToValue(GET_X_LPARAM(lp));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g.dragSlider) { g.dragSlider = false; ReleaseCapture(); SaveSettings(); }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        if (PtInRect(&g.zone, pt)) {
            g.scroll = std::clamp(g.scroll - GET_WHEEL_DELTA_WPARAM(wp) * g.S(kRowH + kRowGap) / WHEEL_DELTA, 0, MaxScroll());
            InvalidateRect(hwnd, &g.zone, FALSE);
        }
        return 0;
    }
    case WM_CLOSE:
        if (g.running) {
            g.cancel = true;
            JoinWorkers();   // Abbruch greift beim naechsten Audio-Block bzw. nach dem laufenden Bild
            g.running = false;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        JoinWorkers();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) return 1;
    conv::MediaStartup();

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"DuncansQuickConvert";
    RegisterClassExW(&wc);

    const UINT dpi = GetDpiForSystem();
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Duncan's QuickConvert", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                MulDiv(900, static_cast<int>(dpi), 96), MulDiv(680, static_cast<int>(dpi), 96), nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    // Dateien aus der Kommandozeile (Explorer "Senden an" / "Oeffnen mit")
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        std::vector<std::wstring> paths;
        for (int i = 1; i < argc; ++i) paths.emplace_back(argv[i]);
        LocalFree(argv);
        if (!paths.empty()) AddPaths(paths);
    }

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    conv::MediaShutdown();
    CoUninitialize();
    return static_cast<int>(m.wParam);
}
