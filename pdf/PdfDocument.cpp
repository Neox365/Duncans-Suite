#include "PdfDocument.h"
#include "PdfiumApi.h"

#include <algorithm>
#include <cstring>
#include <map>

using std::min;
using std::max;
#include <objbase.h>
#include <propidl.h>
#include <gdiplus.h>

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
        && Load("FPDFText_GetRect", g_api.TextGetRect)
        && Load("FPDFText_GetCharIndexAtPos", g_api.TextGetCharIndexAtPos)
        && Load("FPDF_DeviceToPage", g_api.DeviceToPage)
        && Load("FPDF_CreateNewDocument", g_api.CreateNewDocument)
        && Load("FPDF_MovePages", g_api.MovePages)
        && Load("FPDFPage_CreateAnnot", g_api.PageCreateAnnot)
        && Load("FPDFPage_CloseAnnot", g_api.PageCloseAnnot)
        && Load("FPDFAnnot_SetColor", g_api.AnnotSetColor)
        && Load("FPDFAnnot_AppendAttachmentPoints", g_api.AnnotAppendAttachmentPoints)
        && Load("FPDFAnnot_SetRect", g_api.AnnotSetRect)
        && Load("FPDFAnnot_SetStringValue", g_api.AnnotSetStringValue)
        && Load("FPDFPage_CountObjects", g_api.PageCountObjects)
        && Load("FPDFPage_GetObject", g_api.PageGetObject)
        && Load("FPDFPageObj_GetType", g_api.PageObjGetType)
        && Load("FPDFImageObj_GetBitmap", g_api.ImageObjGetBitmap)
        && Load("FPDFImageObj_GetImageMetadata", g_api.ImageObjGetMetadata)
        && Load("FPDFImageObj_GetImageDataRaw", g_api.ImageObjGetImageDataRaw)
        && Load("FPDFImageObj_LoadJpegFileInline", g_api.ImageObjLoadJpegFileInline)
        && Load("FPDFPage_GenerateContent", g_api.PageGenerateContent)
        && Load("FPDFBitmap_GetWidth", g_api.BitmapGetWidth)
        && Load("FPDFBitmap_GetHeight", g_api.BitmapGetHeight)
        && Load("FPDFBitmap_GetStride", g_api.BitmapGetStride)
        && Load("FPDFBitmap_GetBuffer", g_api.BitmapGetBuffer)
        && Load("FPDFBitmap_GetFormat", g_api.BitmapGetFormat)
        && Load("FPDFPage_SetCropBox", g_api.PageSetCropBox)
        && Load("FPDFPage_GetMediaBox", g_api.PageGetMediaBox)
        && Load("FPDFDOC_InitFormFillEnvironment", g_api.FormInit)
        && Load("FPDFDOC_ExitFormFillEnvironment", g_api.FormExit)
        && Load("FPDF_GetFormType", g_api.GetFormType)
        && Load("FORM_OnAfterLoadPage", g_api.FormAfterLoadPage)
        && Load("FORM_OnBeforeClosePage", g_api.FormBeforeClosePage)
        && Load("FPDF_FFLDraw", g_api.FFLDraw)
        && Load("FORM_OnLButtonDown", g_api.FormLButtonDown)
        && Load("FORM_OnLButtonUp", g_api.FormLButtonUp)
        && Load("FORM_OnMouseMove", g_api.FormMouseMove)
        && Load("FORM_OnKeyDown", g_api.FormKeyDown)
        && Load("FORM_OnKeyUp", g_api.FormKeyUp)
        && Load("FORM_OnChar", g_api.FormChar)
        && Load("FORM_ForceToKillFocus", g_api.FormKillFocus)
        && Load("FPDF_SetFormFieldHighlightColor", g_api.FormHighlightColor)
        && Load("FPDF_SetFormFieldHighlightAlpha", g_api.FormHighlightAlpha)
        && Load("FPDFPage_HasFormFieldAtPoint", g_api.FormFieldAtPoint)
        && Load("FORM_GetSelectedText", g_api.FormGetSelectedText)
        && Load("FORM_ReplaceSelection", g_api.FormReplaceSelection)
        && Load("FORM_SelectAllText", g_api.FormSelectAllText);
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
    CloseForm();
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

HBITMAP PdfDocument::RenderImpl(void* page, int index, int w, int h, bool withForm) const {
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
            if (withForm && m_form) g_api.FFLDraw(m_form, pb, page, 0, 0, w, h, Rotation(index), FPDF_ANNOT | FPDF_LCD_TEXT);
            g_api.BitmapDestroy(pb);
        }
    }
    return bmp;
}

HBITMAP PdfDocument::RenderPage(int index, int w, int h) const {
    if (!m_doc || w <= 0 || h <= 0) return nullptr;
    void* page = g_api.LoadPage(m_doc, index);
    if (!page) return nullptr;
    HBITMAP bmp = RenderImpl(page, index, w, h, false);
    g_api.ClosePage(page);
    return bmp;
}

bool PdfDocument::Append(const std::wstring& otherPath) {
    if (!m_doc) return false;
    CloseForm();
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
    CloseForm();
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

void PdfDocument::BakeRotations() {
    // Drehungen in die Seiten schreiben; danach gilt die Drehung als im Dokument enthalten.
    for (size_t i = 0; i < m_rot.size(); ++i) {
        if (!m_rot[i]) continue;
        void* page = g_api.LoadPage(m_doc, static_cast<int>(i));
        if (!page) continue;
        g_api.PageSetRotation(page, (g_api.PageGetRotation(page) + m_rot[i]) % 4);
        g_api.ClosePage(page);
        m_rot[i] = 0;
    }
}

bool PdfDocument::MovePages(const std::vector<int>& indices, int insertBefore, std::vector<int>* newOrder) {
    if (!m_doc || indices.empty()) return false;
    CloseForm();
    const int n = PageCount();
    std::vector<int> idx = indices;
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
    if (idx.front() < 0 || idx.back() >= n) return false;
    insertBefore = std::clamp(insertBefore, 0, n);
    // Zielposition im Dokument OHNE die verschobenen Seiten
    int dest = insertBefore;
    for (int i : idx) if (i < insertBefore) --dest;

    // Neue Reihenfolge berechnen (fuer Drehungen und die Rueckmeldung an die Oberflaeche)
    std::vector<int> rest, order;
    for (int i = 0; i < n; ++i) if (!std::binary_search(idx.begin(), idx.end(), i)) rest.push_back(i);
    order = rest;
    order.insert(order.begin() + dest, idx.begin(), idx.end());   // order[neu] = alt
    bool identity = true;
    for (int i = 0; i < n; ++i) if (order[i] != i) identity = false;
    if (identity) return true;

    if (!g_api.MovePages(m_doc, idx.data(), static_cast<unsigned long>(idx.size()), dest)) {
        g_error = L"Die Seiten konnten nicht verschoben werden.";
        return false;
    }
    std::vector<int> rot(n, 0);
    for (int neu = 0; neu < n; ++neu) rot[neu] = order[neu] < static_cast<int>(m_rot.size()) ? m_rot[order[neu]] : 0;
    m_rot = rot;
    if (newOrder) {
        newOrder->assign(n, 0);
        for (int neu = 0; neu < n; ++neu) (*newOrder)[order[neu]] = neu;   // alt -> neu
    }
    m_modified = true;
    return true;
}

int PdfDocument::CharAt(int index, int w, int h, int dx, int dy) const {
    if (!m_doc || w <= 0 || h <= 0) return -1;
    void* page = g_api.LoadPage(m_doc, index);
    if (!page) return -1;
    int result = -1;
    double px = 0, py = 0;
    if (g_api.DeviceToPage(page, 0, 0, w, h, Rotation(index), dx, dy, &px, &py)) {
        if (void* tp = g_api.TextLoadPage(page)) {
            const int c = g_api.TextGetCharIndexAtPos(tp, px, py, 12.0, 12.0);   // Toleranz in Punkt
            result = c >= 0 ? c : -1;
            g_api.TextClosePage(tp);
        }
    }
    g_api.ClosePage(page);
    return result;
}

bool PdfDocument::AddHighlight(int index, int startChar, int charCount) {
    if (!m_doc || charCount <= 0 || startChar < 0) return false;
    void* page = g_api.LoadPage(m_doc, index);
    if (!page) return false;
    bool ok = false;
    if (void* tp = g_api.TextLoadPage(page)) {
        const int rects = g_api.TextCountRects(tp, startChar, charCount);
        if (rects > 0) {
            if (void* annot = g_api.PageCreateAnnot(page, FPDF_ANNOT_HIGHLIGHT)) {
                FS_RECTF all{ 1e9f, -1e9f, -1e9f, 1e9f };                 // Umschliessendes Rechteck (top > bottom)
                int added = 0;
                for (int r = 0; r < rects; ++r) {
                    double l = 0, t = 0, rt = 0, b = 0;
                    if (!g_api.TextGetRect(tp, r, &l, &t, &rt, &b)) continue;
                    FS_QUADPOINTSF q{ float(l), float(t), float(rt), float(t), float(l), float(b), float(rt), float(b) };
                    if (g_api.AnnotAppendAttachmentPoints(annot, &q)) ++added;
                    all.left = std::min(all.left, float(l));
                    all.right = std::max(all.right, float(rt));
                    all.top = std::max(all.top, float(t));
                    all.bottom = std::min(all.bottom, float(b));
                }
                if (added > 0) {
                    g_api.AnnotSetRect(annot, &all);
                    g_api.AnnotSetColor(annot, 0 /* Color */, 255, 235, 0, 200);
                    ok = true;
                }
                g_api.PageCloseAnnot(annot);
            }
        }
        g_api.TextClosePage(tp);
    }
    g_api.ClosePage(page);
    if (ok) m_modified = true; else g_error = L"Hier ist kein Text zum Markieren.";
    return ok;
}

bool PdfDocument::AddNote(int index, int w, int h, int dx, int dy, const std::wstring& text) {
    if (!m_doc || text.empty()) return false;
    void* page = g_api.LoadPage(m_doc, index);
    if (!page) return false;
    bool ok = false;
    double px = 0, py = 0;
    if (g_api.DeviceToPage(page, 0, 0, w, h, Rotation(index), dx, dy, &px, &py)) {
        if (void* annot = g_api.PageCreateAnnot(page, FPDF_ANNOT_TEXT)) {
            FS_RECTF r{ float(px), float(py), float(px + 20), float(py - 20) };   // Symbol 20 x 20 Punkt unterhalb des Klicks
            g_api.AnnotSetRect(annot, &r);
            g_api.AnnotSetColor(annot, 0, 255, 214, 0, 255);
            ok = g_api.AnnotSetStringValue(annot, "Contents", reinterpret_cast<const unsigned short*>(text.c_str())) != 0;
            g_api.PageCloseAnnot(annot);
        }
    }
    g_api.ClosePage(page);
    if (ok) m_modified = true; else g_error = L"Die Notiz konnte nicht angelegt werden.";
    return ok;
}

namespace {
    // Schreiber in einen Speicherpuffer (fuer das Verkleinern ohne Temp-Datei)
    struct MemWriter : FPDF_FILEWRITE { std::vector<char>* buf; };
    int MemWriteBlock(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
        auto* w = static_cast<MemWriter*>(self);
        const char* p = static_cast<const char*>(data);
        w->buf->insert(w->buf->end(), p, p + size);
        return 1;
    }

    struct MemFile { const std::vector<char>* data; };
    int MemGetBlock(void* param, unsigned long pos, unsigned char* out, unsigned long size) {
        const auto* f = static_cast<MemFile*>(param);
        if (pos + size > f->data->size()) return 0;
        memcpy(out, f->data->data() + pos, size);
        return 1;
    }

    // Bild verkleinern und als JPEG kodieren (GDI+). Leer = nicht moeglich/nicht sinnvoll.
    std::vector<char> EncodeJpeg(const void* pixels, int w, int h, int stride, int format, double scale, int quality) {
        using namespace Gdiplus;
        std::vector<char> out;
        PixelFormat pf = format == FPDFBitmap_BGRx ? PixelFormat32bppRGB : PixelFormat24bppRGB;
        std::vector<unsigned char> expanded;
        const void* src = pixels;
        int srcStride = stride;
        if (format == FPDFBitmap_Gray) {                       // Graustufen in 24-Bit-RGB aufweiten
            expanded.resize(static_cast<size_t>(w) * 3 * h);
            const unsigned char* s = static_cast<const unsigned char*>(pixels);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const unsigned char v = s[static_cast<size_t>(y) * stride + x];
                    unsigned char* d = &expanded[(static_cast<size_t>(y) * w + x) * 3];
                    d[0] = d[1] = d[2] = v;
                }
            src = expanded.data();
            srcStride = w * 3;
        }
        Bitmap source(w, h, srcStride, pf, const_cast<BYTE*>(static_cast<const BYTE*>(src)));
        const int nw = std::max(1, static_cast<int>(w * scale)), nh = std::max(1, static_cast<int>(h * scale));
        Bitmap target(nw, nh, PixelFormat24bppRGB);
        {
            Graphics g(&target);
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.SetPixelOffsetMode(PixelOffsetModeHalf);
            g.DrawImage(&source, Rect(0, 0, nw, nh), 0, 0, w, h, UnitPixel);
        }
        CLSID jpeg{};
        {   // JPEG-Encoder suchen
            UINT n = 0, size = 0;
            GetImageEncodersSize(&n, &size);
            if (!size) return out;
            std::vector<char> raw(size);
            auto* info = reinterpret_cast<ImageCodecInfo*>(raw.data());
            GetImageEncoders(n, size, info);
            bool found = false;
            for (UINT i = 0; i < n; ++i) if (wcscmp(info[i].MimeType, L"image/jpeg") == 0) { jpeg = info[i].Clsid; found = true; break; }
            if (!found) return out;
        }
        EncoderParameters ep{};
        ep.Count = 1;
        ep.Parameter[0].Guid = EncoderQuality;
        ep.Parameter[0].Type = EncoderParameterValueTypeLong;
        ep.Parameter[0].NumberOfValues = 1;
        ULONG q = static_cast<ULONG>(quality);
        ep.Parameter[0].Value = &q;
        IStream* stream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return out;
        if (target.Save(stream, &jpeg, &ep) == Ok) {
            HGLOBAL hg = nullptr;
            GetHGlobalFromStream(stream, &hg);
            const SIZE_T size = GlobalSize(hg);
            if (void* p = GlobalLock(hg)) {
                STATSTG st{};
                stream->Stat(&st, STATFLAG_NONAME);
                out.assign(static_cast<char*>(p), static_cast<char*>(p) + std::min<unsigned long long>(st.cbSize.QuadPart, size));
                GlobalUnlock(hg);
            }
        }
        stream->Release();
        return out;
    }
}

bool PdfDocument::SaveCompressed(const std::wstring& path, int maxDpi, int jpegQuality, CompressStats& stats) {
    if (!m_doc) return false;
    CloseForm();
    stats = CompressStats{};
    BakeRotations();

    // 1) aktuellen Stand in den Speicher schreiben
    std::vector<char> snapshot;
    MemWriter mw{};
    mw.version = 1;
    mw.WriteBlock = &MemWriteBlock;
    mw.buf = &snapshot;
    if (!g_api.SaveAsCopy(m_doc, &mw, 0) || snapshot.empty()) { g_error = L"Der Dokumentstand konnte nicht gelesen werden."; return false; }
    stats.before = snapshot.size();

    // 2) Seiten in ein frisches Dokument uebernehmen: nur erreichbare Objekte kommen mit
    void* src = g_api.LoadMemDocument(snapshot.data(), static_cast<int>(snapshot.size()), nullptr);
    void* out = g_api.CreateNewDocument();
    if (!src || !out || !g_api.ImportPages(out, src, nullptr, 0)) {
        if (src) g_api.CloseDocument(src);
        if (out) g_api.CloseDocument(out);
        g_error = L"Die Seiten konnten nicht übernommen werden.";
        return false;
    }
    g_api.CloseDocument(src);

    // 3) Bilder herunterrechnen und neu kodieren
    const int pages = g_api.GetPageCount(out);
    for (int p = 0; p < pages; ++p) {
        void* page = g_api.LoadPage(out, p);
        if (!page) continue;
        bool changed = false;
        const int objs = g_api.PageCountObjects(page);
        for (int o = 0; o < objs; ++o) {
            void* obj = g_api.PageGetObject(page, o);
            if (!obj || g_api.PageObjGetType(obj) != FPDF_PAGEOBJ_IMAGE) continue;
            ++stats.images;
            FPDF_IMAGEOBJ_METADATA meta{};
            if (!g_api.ImageObjGetMetadata(obj, page, &meta) || meta.bits_per_pixel < 8 || meta.width < 64 || meta.height < 64) continue;
            const unsigned long rawSize = g_api.ImageObjGetImageDataRaw(obj, nullptr, 0);
            const double dpi = std::max(meta.horizontal_dpi, meta.vertical_dpi);
            const double scale = (dpi > maxDpi && dpi > 0) ? maxDpi / dpi : 1.0;
            FPDF_BITMAP bmp = g_api.ImageObjGetBitmap(obj);
            if (!bmp) continue;
            const int fmt = g_api.BitmapGetFormat(bmp);
            if (fmt == FPDFBitmap_BGRA) { g_api.BitmapDestroy(bmp); continue; }       // Transparenz/Maske: unveraendert lassen
            std::vector<char> jpg = EncodeJpeg(g_api.BitmapGetBuffer(bmp), g_api.BitmapGetWidth(bmp), g_api.BitmapGetHeight(bmp),
                                               g_api.BitmapGetStride(bmp), fmt, scale, jpegQuality);
            g_api.BitmapDestroy(bmp);
            if (jpg.empty() || (rawSize > 0 && jpg.size() >= rawSize * 0.9)) continue;   // nur uebernehmen, wenn deutlich kleiner
            MemFile mf{ &jpg };
            FPDF_FILEACCESS fa{};
            fa.m_FileLen = static_cast<unsigned long>(jpg.size());
            fa.m_GetBlock = &MemGetBlock;
            fa.m_Param = &mf;
            if (g_api.ImageObjLoadJpegFileInline(&page, 1, obj, &fa)) { ++stats.recompressed; changed = true; }
        }
        if (changed) g_api.PageGenerateContent(page);
        g_api.ClosePage(page);
    }

    // 4) Die ersetzten Bilder lassen ihre alten Datenstroeme als verwaiste Objekte zurueck. Darum noch einmal
    //    in ein frisches Dokument uebernehmen: so wandert nur, was die Seiten wirklich brauchen, in die Datei.
    {
        std::vector<char> middle;
        MemWriter mw2{};
        mw2.version = 1;
        mw2.WriteBlock = &MemWriteBlock;
        mw2.buf = &middle;
        const bool wrote = g_api.SaveAsCopy(out, &mw2, FPDF_NO_INCREMENTAL) != 0 && !middle.empty();
        g_api.CloseDocument(out);
        out = nullptr;
        void* mid = wrote ? g_api.LoadMemDocument(middle.data(), static_cast<int>(middle.size()), nullptr) : nullptr;
        void* fin = mid ? g_api.CreateNewDocument() : nullptr;
        if (!fin || !g_api.ImportPages(fin, mid, nullptr, 0)) {
            if (mid) g_api.CloseDocument(mid);
            if (fin) g_api.CloseDocument(fin);
            g_error = L"Die Seiten konnten nicht übernommen werden.";
            return false;
        }
        g_api.CloseDocument(mid);
        out = fin;
    }

    // 5) speichern (vollstaendig neu geschrieben, ohne unbenutzte Objekte)
    Writer w{};
    w.version = 1;
    w.WriteBlock = &WriteBlock;
    w.ok = true;
    const std::wstring tmp = path + L".tmp";
    w.file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (w.file == INVALID_HANDLE_VALUE) { g_api.CloseDocument(out); g_error = L"Die Zieldatei kann nicht geschrieben werden."; return false; }
    bool ok = g_api.SaveAsCopy(out, &w, FPDF_NO_INCREMENTAL) != 0 && w.ok;
    LARGE_INTEGER sz{};
    GetFileSizeEx(w.file, &sz);
    CloseHandle(w.file);
    g_api.CloseDocument(out);
    if (ok) ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) { DeleteFileW(tmp.c_str()); g_error = L"Speichern fehlgeschlagen."; return false; }
    stats.after = static_cast<unsigned long long>(sz.QuadPart);
    return true;
}

bool PdfDocument::SaveCopy(const std::wstring& path) {
    if (!m_doc) return false;
    CloseForm();
    BakeRotations();
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

// ---------- Zuschneiden ----------
bool PdfDocument::CropPages(const std::vector<int>& indices, double fx0, double fy0, double fx1, double fy1) {
    if (!m_doc || indices.empty()) return false;
    if (fx0 > fx1) std::swap(fx0, fx1);
    if (fy0 > fy1) std::swap(fy0, fy1);
    fx0 = std::clamp(fx0, 0.0, 1.0); fx1 = std::clamp(fx1, 0.0, 1.0);
    fy0 = std::clamp(fy0, 0.0, 1.0); fy1 = std::clamp(fy1, 0.0, 1.0);
    if (fx1 - fx0 < 0.02 || fy1 - fy0 < 0.02) { g_error = L"Der Ausschnitt ist zu klein."; return false; }
    CloseForm();
    int done = 0;
    for (int index : indices) {
        double w = 0, h = 0;
        if (!PageSize(index, w, h)) continue;                       // angezeigte Groesse in Punkt (inkl. Zusatzdrehung)
        void* page = g_api.LoadPage(m_doc, index);
        if (!page) continue;
        const int dw = std::max(1, static_cast<int>(w * 16)), dh = std::max(1, static_cast<int>(h * 16));   // Geraetepixel mit 16facher Genauigkeit
        double ax = 0, ay = 0, bx = 0, by = 0;
        const bool ok = g_api.DeviceToPage(page, 0, 0, dw, dh, Rotation(index), static_cast<int>(fx0 * dw), static_cast<int>(fy0 * dh), &ax, &ay)
                     && g_api.DeviceToPage(page, 0, 0, dw, dh, Rotation(index), static_cast<int>(fx1 * dw), static_cast<int>(fy1 * dh), &bx, &by);
        if (ok) {
            g_api.PageSetCropBox(page, static_cast<float>(std::min(ax, bx)), static_cast<float>(std::min(ay, by)),
                                 static_cast<float>(std::max(ax, bx)), static_cast<float>(std::max(ay, by)));
            ++done;
        }
        g_api.ClosePage(page);
    }
    if (!done) { g_error = L"Der Zuschnitt konnte nicht gesetzt werden."; return false; }
    m_modified = true;
    return true;
}

bool PdfDocument::ResetCrop(const std::vector<int>& indices) {
    if (!m_doc) return false;
    CloseForm();
    int done = 0;
    for (int index : indices) {
        void* page = g_api.LoadPage(m_doc, index);
        if (!page) continue;
        float l = 0, b = 0, r = 0, t = 0;
        if (g_api.PageGetMediaBox(page, &l, &b, &r, &t)) { g_api.PageSetCropBox(page, l, b, r, t); ++done; }
        g_api.ClosePage(page);
    }
    if (!done) { g_error = L"Der Zuschnitt konnte nicht aufgehoben werden."; return false; }
    m_modified = true;
    return true;
}

// ---------- Formulare ----------
namespace {
    void (*g_redrawHook)() = nullptr;
    std::map<int, void (*)(int)>& TimerMap() { static std::map<int, void (*)(int)> m; return m; }
    void CALLBACK FormTimerProc(HWND, UINT, UINT_PTR id, DWORD) {
        auto it = TimerMap().find(static_cast<int>(id));
        if (it != TimerMap().end()) it->second(static_cast<int>(id));
    }
}

struct FormCallbacks {
    struct Info : FPDF_FORMFILLINFO { PdfDocument* owner; };
    static PdfDocument* Owner(FPDF_FORMFILLINFO* p) { return static_cast<Info*>(p)->owner; }
    static void Release(FPDF_FORMFILLINFO*) {}
    static void Invalidate(FPDF_FORMFILLINFO* p, FPDF_PAGE, double, double, double, double) {
        Owner(p)->m_formDirty = true;
        if (g_redrawHook) g_redrawHook();
    }
    static void OutputSelectedRect(FPDF_FORMFILLINFO*, FPDF_PAGE, double, double, double, double) {}
    static void SetCursor(FPDF_FORMFILLINFO*, int) {}
    static int SetTimer(FPDF_FORMFILLINFO*, int elapse, void (*cb)(int)) {
        // Timer ohne Fenster: der Rueckruf kommt ueber die Nachrichtenschleife; die Id merken wir fuer KillTimer
        const UINT_PTR id = ::SetTimer(nullptr, 0, static_cast<UINT>(elapse), &FormTimerProc);
        TimerMap()[static_cast<int>(id)] = cb;
        return static_cast<int>(id);
    }
    static void KillTimer(FPDF_FORMFILLINFO*, int id) { ::KillTimer(nullptr, static_cast<UINT_PTR>(id)); TimerMap().erase(id); }
    static FPDF_SYSTEMTIME GetLocalTime(FPDF_FORMFILLINFO*) {
        SYSTEMTIME st; ::GetLocalTime(&st);
        return FPDF_SYSTEMTIME{ st.wYear, st.wMonth, st.wDayOfWeek, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds };
    }
    static void OnChange(FPDF_FORMFILLINFO* p) { Owner(p)->m_modified = true; }
    static FPDF_PAGE GetPage(FPDF_FORMFILLINFO* p, FPDF_DOCUMENT, int idx) { PdfDocument* d = Owner(p); return d->m_formPageIdx == idx ? d->m_formPage : nullptr; }
    static FPDF_PAGE GetCurrentPage(FPDF_FORMFILLINFO* p, FPDF_DOCUMENT) { return Owner(p)->m_formPage; }
    static int GetRotation(FPDF_FORMFILLINFO*, FPDF_PAGE) { return 0; }
    static void NamedAction(FPDF_FORMFILLINFO*, const char*) {}
    static void SetTextFieldFocus(FPDF_FORMFILLINFO*, const unsigned short*, unsigned long, FPDF_BOOL) {}
    static void DoURI(FPDF_FORMFILLINFO*, const char*) {}
    static void DoGoTo(FPDF_FORMFILLINFO*, int, int, float*, int) {}
};

void PdfDocument::SetFormRedrawHook(void (*hook)()) { g_redrawHook = hook; }

void PdfDocument::CloseForm() {
    if (m_form) {
        g_api.FormKillFocus(m_form);
        if (m_formPage) { g_api.FormBeforeClosePage(m_formPage, m_form); g_api.ClosePage(m_formPage); }
        g_api.FormExit(m_form);
    }
    delete static_cast<FormCallbacks::Info*>(m_formInfo);
    m_form = nullptr; m_formInfo = nullptr; m_formPage = nullptr; m_formPageIdx = -1;
    m_hasForms = -1;
    m_formDirty = false;
}

bool PdfDocument::HasForms() {
    if (!m_doc) return false;
    if (m_hasForms < 0) m_hasForms = g_api.GetFormType(m_doc) == 1 ? 1 : 0;   // 1 = AcroForm (XFA wird nicht unterstuetzt)
    return m_hasForms == 1;
}

bool PdfDocument::UseFormPage(int index) {
    if (!m_doc || !HasForms()) return false;
    if (!m_form) {
        auto* info = new FormCallbacks::Info{};
        info->version = 1;
        info->Release = &FormCallbacks::Release;
        info->FFI_Invalidate = &FormCallbacks::Invalidate;
        info->FFI_OutputSelectedRect = &FormCallbacks::OutputSelectedRect;
        info->FFI_SetCursor = &FormCallbacks::SetCursor;
        info->FFI_SetTimer = &FormCallbacks::SetTimer;
        info->FFI_KillTimer = &FormCallbacks::KillTimer;
        info->FFI_GetLocalTime = &FormCallbacks::GetLocalTime;
        info->FFI_OnChange = &FormCallbacks::OnChange;
        info->FFI_GetPage = &FormCallbacks::GetPage;
        info->FFI_GetCurrentPage = &FormCallbacks::GetCurrentPage;
        info->FFI_GetRotation = &FormCallbacks::GetRotation;
        info->FFI_ExecuteNamedAction = &FormCallbacks::NamedAction;
        info->FFI_SetTextFieldFocus = &FormCallbacks::SetTextFieldFocus;
        info->FFI_DoURIAction = &FormCallbacks::DoURI;
        info->FFI_DoGoToAction = &FormCallbacks::DoGoTo;
        info->owner = this;
        m_form = g_api.FormInit(m_doc, info);
        if (!m_form) { delete info; return false; }
        m_formInfo = info;
        g_api.FormHighlightColor(m_form, 0, 0xFFE4DD);   // alle Feldarten leicht blau hinterlegen
        g_api.FormHighlightAlpha(m_form, 100);
    }
    if (m_formPageIdx != index || !m_formPage) {
        g_api.FormKillFocus(m_form);
        if (m_formPage) { g_api.FormBeforeClosePage(m_formPage, m_form); g_api.ClosePage(m_formPage); m_formPage = nullptr; }
        m_formPage = g_api.LoadPage(m_doc, index);
        m_formPageIdx = m_formPage ? index : -1;
        if (m_formPage) g_api.FormAfterLoadPage(m_formPage, m_form);
    }
    return m_formPage != nullptr;
}

HBITMAP PdfDocument::RenderPageForm(int index, int w, int h) {
    if (!m_doc || w <= 0 || h <= 0) return nullptr;
    if (!UseFormPage(index)) return RenderPage(index, w, h);
    m_formDirty = false;
    return RenderImpl(m_formPage, index, w, h, true);
}

bool PdfDocument::TakeFormDirty() { const bool d = m_formDirty; m_formDirty = false; return d; }

int PdfDocument::FormFieldAt(int index, int w, int h, int dx, int dy) {
    if (!UseFormPage(index)) return -1;
    double px = 0, py = 0;
    if (!g_api.DeviceToPage(m_formPage, 0, 0, w, h, Rotation(index), dx, dy, &px, &py)) return -1;
    return g_api.FormFieldAtPoint(m_form, m_formPage, px, py);
}

bool PdfDocument::FormMouse(FormMouseKind kind, int index, int w, int h, int dx, int dy) {
    if (!UseFormPage(index)) return false;
    double px = 0, py = 0;
    if (!g_api.DeviceToPage(m_formPage, 0, 0, w, h, Rotation(index), dx, dy, &px, &py)) return false;
    switch (kind) {
    case FormDown: return g_api.FormLButtonDown(m_form, m_formPage, 0, px, py) != 0;
    case FormUp:   return g_api.FormLButtonUp(m_form, m_formPage, 0, px, py) != 0;
    default:       return g_api.FormMouseMove(m_form, m_formPage, 0, px, py) != 0;
    }
}

bool PdfDocument::FormKey(int index, int vk, bool down, int modifiers) {
    if (!UseFormPage(index)) return false;
    return (down ? g_api.FormKeyDown(m_form, m_formPage, vk, modifiers) : g_api.FormKeyUp(m_form, m_formPage, vk, modifiers)) != 0;
}

bool PdfDocument::FormChar(int index, int ch, int modifiers) {
    if (!UseFormPage(index)) return false;
    return g_api.FormChar(m_form, m_formPage, ch, modifiers) != 0;
}

std::wstring PdfDocument::FormSelectedText(int index) {
    if (!UseFormPage(index)) return L"";
    const unsigned long bytes = g_api.FormGetSelectedText(m_form, m_formPage, nullptr, 0);
    if (bytes <= 2) return L"";
    std::vector<unsigned short> buf(bytes / 2 + 1, 0);
    g_api.FormGetSelectedText(m_form, m_formPage, buf.data(), bytes);
    return std::wstring(reinterpret_cast<wchar_t*>(buf.data()));
}

bool PdfDocument::FormReplaceSelection(int index, const std::wstring& text) {
    if (!UseFormPage(index)) return false;
    g_api.FormReplaceSelection(m_form, m_formPage, reinterpret_cast<const unsigned short*>(text.c_str()));
    return true;
}

bool PdfDocument::FormSelectAll(int index) {
    if (!UseFormPage(index)) return false;
    return g_api.FormSelectAllText(m_form, m_formPage) != 0;
}

void PdfDocument::FormKillFocus() {
    if (m_form) g_api.FormKillFocus(m_form);
}
