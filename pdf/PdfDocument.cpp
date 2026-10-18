#include "PdfDocument.h"
#include "PdfiumApi.h"

#include <algorithm>

namespace {
    PdfiumApi g_api;
    bool g_tried = false;
    std::wstring g_error;

    bool ReadFileBytes(const std::wstring& path, std::vector<char>& out) {
        HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz{};
        bool ok = GetFileSizeEx(f, &sz) && sz.QuadPart > 0 && sz.QuadPart < 0x7FFFFFFF;
        if (ok) {
            out.resize(static_cast<size_t>(sz.QuadPart));
            size_t done = 0;
            while (ok && done < out.size()) {
                DWORD got = 0;
                ok = ReadFile(f, out.data() + done, static_cast<DWORD>(out.size() - done), &got, nullptr) && got > 0;
                done += got;
            }
        }
        CloseHandle(f);
        return ok;
    }

    struct Writer : FPDF_FILEWRITE { HANDLE file; bool ok; };
    int WriteBlock(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
        auto* w = static_cast<Writer*>(self);
        DWORD written = 0;
        if (!WriteFile(w->file, data, size, &written, nullptr) || written != size) w->ok = false;
        return w->ok ? 1 : 0;
    }

    template <typename T> bool Load(const char* name, T& fn) {
        fn = reinterpret_cast<T>(GetProcAddress(g_api.module, name));
        return fn != nullptr;
    }
}

bool PdfDocument::InitLibrary() {
    if (g_tried) return g_api.module != nullptr;
    g_tried = true;

    // pdfium.dll nur aus dem Ordner der EXE laden (kein Fallback auf Arbeitsordner/PATH: DLL-Hijacking)
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir.resize(dir.find_last_of(L'\\') == std::wstring::npos ? 0 : dir.find_last_of(L'\\'));
    g_api.module = LoadLibraryExW((dir + L"\\pdfium.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_api.module) {
        g_error = L"pdfium.dll fehlt – bitte neben DuncansPdf.exe legen (pdf\\third_party\\get_pdfium.ps1).";
        return false;
    }
    bool ok = Load("FPDF_InitLibraryWithConfig", g_api.InitLibraryWithConfig)
        && Load("FPDF_DestroyLibrary", g_api.DestroyLibrary)
        && Load("FPDF_GetLastError", g_api.GetLastError)
        && Load("FPDF_LoadMemDocument", g_api.LoadMemDocument)
        && Load("FPDF_CloseDocument", g_api.CloseDocument)
        && Load("FPDF_GetPageCount", g_api.GetPageCount)
        && Load("FPDF_LoadPage", g_api.LoadPage)
        && Load("FPDF_ClosePage", g_api.ClosePage)
        && Load("FPDF_GetPageWidthF", g_api.GetPageWidthF)
        && Load("FPDF_GetPageHeightF", g_api.GetPageHeightF)
        && Load("FPDFBitmap_CreateEx", g_api.BitmapCreateEx)
        && Load("FPDFBitmap_FillRect", g_api.BitmapFillRect)
        && Load("FPDFBitmap_Destroy", g_api.BitmapDestroy)
        && Load("FPDF_RenderPageBitmap", g_api.RenderPageBitmap)
        && Load("FPDFPage_GetRotation", g_api.PageGetRotation)
        && Load("FPDFPage_SetRotation", g_api.PageSetRotation)
        && Load("FPDF_ImportPages", g_api.ImportPages)
        && Load("FPDF_SaveAsCopy", g_api.SaveAsCopy)
        && Load("FPDFPage_Delete", g_api.PageDelete)
        && Load("FPDF_PageToDevice", g_api.PageToDevice)
        && Load("FPDFText_LoadPage", g_api.TextLoadPage)
        && Load("FPDFText_ClosePage", g_api.TextClosePage)
        && Load("FPDFText_FindStart", g_api.TextFindStart)
        && Load("FPDFText_FindNext", g_api.TextFindNext)
        && Load("FPDFText_GetSchResultIndex", g_api.TextGetSchResultIndex)
        && Load("FPDFText_GetSchCount", g_api.TextGetSchCount)
        && Load("FPDFText_FindClose", g_api.TextFindClose)
        && Load("FPDFText_CountRects", g_api.TextCountRects)
        && Load("FPDFText_GetRect", g_api.TextGetRect);
    if (!ok) {
        g_error = L"pdfium.dll ist nicht kompatibel (Funktionen fehlen). Bitte eine aktuelle 64-Bit-Version verwenden.";
        FreeLibrary(g_api.module);
        g_api.module = nullptr;
        return false;
    }
    FPDF_LIBRARY_CONFIG cfg{};
    cfg.version = 2;
    g_api.InitLibraryWithConfig(&cfg);
    return true;
}

const std::wstring& PdfDocument::LastError() { return g_error; }

bool PdfDocument::Open(const std::wstring& path, const std::string& password) {
    if (!InitLibrary()) return false;
    std::vector<char> data;
    if (!ReadFileBytes(path, data)) {
        g_error = L"Die Datei konnte nicht gelesen werden.";
        return false;
    }
    void* doc = g_api.LoadMemDocument(data.data(), static_cast<int>(data.size()),
                                      password.empty() ? nullptr : password.c_str());
    if (!doc) {
        unsigned long err = g_api.GetLastError();
        g_error = err == 4 ? L"Die PDF ist passwortgeschützt (Passwortabfrage folgt in einer späteren Version)."
                           : L"Die Datei ist keine gültige PDF oder beschädigt.";
        return false;
    }
    Close();
    m_doc = doc;
    m_data = std::move(data);
    m_path = path;
    m_rot.assign(PageCount(), 0);
    m_modified = false;
    return true;
}

void PdfDocument::Close() {
    if (m_doc) g_api.CloseDocument(m_doc);
    m_doc = nullptr;
    m_data.clear();
    m_path.clear();
    m_rot.clear();
    m_modified = false;
}

int PdfDocument::PageCount() const { return m_doc ? g_api.GetPageCount(m_doc) : 0; }

int PdfDocument::Rotation(int index) const {
    return index >= 0 && index < static_cast<int>(m_rot.size()) ? m_rot[index] : 0;
}

void PdfDocument::SetRotation(int index, int quarterTurns) {
    if (index < 0 || index >= static_cast<int>(m_rot.size())) return;
    m_rot[index] = ((quarterTurns % 4) + 4) % 4;
    m_modified = true;
}

bool PdfDocument::PageSize(int index, double& w, double& h) const {
    if (!m_doc) return false;
    void* page = g_api.LoadPage(m_doc, index);
    if (!page) return false;
    w = g_api.GetPageWidthF(page);
    h = g_api.GetPageHeightF(page);
    g_api.ClosePage(page);
    if (Rotation(index) & 1) std::swap(w, h);
    return w > 0 && h > 0;
}

HBITMAP PdfDocument::RenderPage(int index, int w, int h) const {
    if (!m_doc || w <= 0 || h <= 0) return nullptr;
    void* page = g_api.LoadPage(m_doc, index);
    if (!page) return nullptr;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;                 // top-down, passend zu PDFium
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        void* pb = g_api.BitmapCreateEx(w, h, FPDFBitmap_BGRx, bits, w * 4);
        if (pb) {
            g_api.BitmapFillRect(pb, 0, 0, w, h, 0xFFFFFFFF);
            g_api.RenderPageBitmap(pb, page, 0, 0, w, h, Rotation(index), FPDF_ANNOT | FPDF_LCD_TEXT);
            g_api.BitmapDestroy(pb);
        }
    }
    g_api.ClosePage(page);
    return bmp;
}

bool PdfDocument::Append(const std::wstring& otherPath) {
    if (!m_doc) return false;
    std::vector<char> data;
    if (!ReadFileBytes(otherPath, data)) { g_error = L"Die Datei konnte nicht gelesen werden."; return false; }
    void* src = g_api.LoadMemDocument(data.data(), static_cast<int>(data.size()), nullptr);
    if (!src) { g_error = L"Die Datei ist keine gültige (unverschlüsselte) PDF."; return false; }
    bool ok = g_api.ImportPages(m_doc, src, nullptr, PageCount()) != 0;
    g_api.CloseDocument(src);
    if (!ok) { g_error = L"Die Seiten konnten nicht angehängt werden."; return false; }
    m_rot.resize(PageCount(), 0);
    m_modified = true;
    return true;
}

bool PdfDocument::DeletePages(std::vector<int> indices) {
    if (!m_doc) return false;
    const int n = PageCount();
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    indices.erase(std::remove_if(indices.begin(), indices.end(), [n](int i) { return i < 0 || i >= n; }), indices.end());
    if (indices.empty()) { g_error = L"Es ist keine Seite ausgewählt."; return false; }
    if (static_cast<int>(indices.size()) >= n) { g_error = L"Die letzte Seite kann nicht gelöscht werden."; return false; }
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {   // von hinten, damit die Indizes stimmen
        g_api.PageDelete(m_doc, *it);
        if (*it < static_cast<int>(m_rot.size())) m_rot.erase(m_rot.begin() + *it);
    }
    m_rot.resize(PageCount(), 0);
    m_modified = true;
    return true;
}

std::vector<PdfDocument::TextHit> PdfDocument::Search(const std::wstring& needle, size_t maxHits) const {
    std::vector<TextHit> hits;
    if (!m_doc || needle.empty()) return hits;
    const int n = PageCount();
    for (int i = 0; i < n && hits.size() < maxHits; ++i) {
        void* page = g_api.LoadPage(m_doc, i);
        if (!page) continue;
        if (void* tp = g_api.TextLoadPage(page)) {
            // wchar_t ist unter Windows UTF-16, wie es PDFium erwartet
            if (void* find = g_api.TextFindStart(tp, reinterpret_cast<const unsigned short*>(needle.c_str()), 0, 0)) {
                while (hits.size() < maxHits && g_api.TextFindNext(find)) {
                    TextHit h;
                    h.page = i;
                    h.start = g_api.TextGetSchResultIndex(find);
                    h.count = g_api.TextGetSchCount(find);
                    if (h.count > 0) hits.push_back(h);
                }
                g_api.TextFindClose(find);
            }
            g_api.TextClosePage(tp);
        }
        g_api.ClosePage(page);
    }
    return hits;
}

void PdfDocument::HitRects(const TextHit& hit, int w, int h, std::vector<RECT>& out) const {
    out.clear();
    if (!m_doc || w <= 0 || h <= 0) return;
    void* page = g_api.LoadPage(m_doc, hit.page);
    if (!page) return;
    if (void* tp = g_api.TextLoadPage(page)) {
        const int rects = g_api.TextCountRects(tp, hit.start, hit.count);
        for (int r = 0; r < rects; ++r) {
            double l = 0, t = 0, rt = 0, b = 0;
            if (!g_api.TextGetRect(tp, r, &l, &t, &rt, &b)) continue;
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            // Seitenkoordinaten (Punkt, y nach oben) in Pixel der gerenderten Seite; beruecksichtigt Seiten- und Zusatzdrehung
            g_api.PageToDevice(page, 0, 0, w, h, Rotation(hit.page), l, t, &x1, &y1);
            g_api.PageToDevice(page, 0, 0, w, h, Rotation(hit.page), rt, b, &x2, &y2);
            out.push_back(RECT{ std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2) });
        }
        g_api.TextClosePage(tp);
    }
    g_api.ClosePage(page);
}

bool PdfDocument::SaveCopy(const std::wstring& path) {
    if (!m_doc) return false;
    // Drehungen in die Seiten schreiben; danach gilt die Drehung als im Dokument enthalten.
    for (size_t i = 0; i < m_rot.size(); ++i) {
        if (!m_rot[i]) continue;
        void* page = g_api.LoadPage(m_doc, static_cast<int>(i));
        if (!page) continue;
        g_api.PageSetRotation(page, (g_api.PageGetRotation(page) + m_rot[i]) % 4);
        g_api.ClosePage(page);
        m_rot[i] = 0;
    }
    Writer w{};
    w.version = 1;
    w.WriteBlock = &WriteBlock;
    w.ok = true;
    // Erst in eine Temp-Datei schreiben und danach ersetzen: bei einem Fehler bleibt eine vorhandene Zieldatei unversehrt
    const std::wstring tmp = path + L".tmp";
    w.file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (w.file == INVALID_HANDLE_VALUE) { g_error = L"Die Zieldatei kann nicht geschrieben werden."; return false; }
    bool ok = g_api.SaveAsCopy(m_doc, &w, 0) != 0 && w.ok;
    CloseHandle(w.file);
    if (ok) ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) { DeleteFileW(tmp.c_str()); g_error = L"Speichern fehlgeschlagen."; return false; }
    m_modified = false;
    return true;
}
