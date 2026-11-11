#pragma once
// Liest den Text der ersten Seiten einer PDF mit PDFium (pdfium.dll) - nur zum Erraten von Betrag und Faelligkeit beim Import.
// Die DLL wird NICHT aus dem Suchpfad geladen, sondern nur aus dem Ordner, in dem auch Duncan's PDF Reader liegt (Suite-Installation, signiert)
// oder neben dieser EXE. Fehlt sie, bleibt der Import wie bisher (nichts wird geraten).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace docs {

class PdfText {
public:
    // dirs: Ordner, in denen pdfium.dll gesucht wird (der Reihe nach)
    explicit PdfText(const std::vector<std::wstring>& dirs) { Load(dirs); }
    ~PdfText() { if (m_mod) FreeLibrary(m_mod); }
    PdfText(const PdfText&) = delete;
    PdfText& operator=(const PdfText&) = delete;
    bool Ready() const { return m_ok; }

    // Eingebettete XML-Datei (ZUGFeRD/Factur-X: factur-x.xml, zugferd-invoice.xml, xrechnung.xml ...) als UTF-8; leer, wenn keine da ist
    std::string ExtractXmlAttachment(const std::wstring& path) const {
        std::string out;
        if (!m_ok || !m_attCount || !m_att || !m_attName || !m_attFile) return out;
        std::ifstream in(std::filesystem::path(path), std::ios::binary);
        if (!in) return out;
        std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (data.empty() || data.size() > 128u * 1024 * 1024) return out;
        void* doc = m_load(data.data(), static_cast<int>(data.size()), nullptr);
        if (!doc) return out;
        const int n = std::min(m_attCount(doc), 20);
        for (int i = 0; i < n && out.empty(); ++i) {
            void* att = m_att(doc, i);
            if (!att) continue;
            const unsigned long nb = m_attName(att, nullptr, 0);                      // Bytes inkl. abschliessender 0
            std::vector<unsigned short> name(nb / 2 + 1, 0);
            if (nb >= 2) m_attName(att, name.data(), nb);
            std::wstring wn;
            for (unsigned short c : name) { if (!c) break; wn.push_back(static_cast<wchar_t>(c)); }
            std::transform(wn.begin(), wn.end(), wn.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
            if (wn.size() < 4 || wn.substr(wn.size() - 4) != L".xml") continue;
            unsigned long len = 0;
            m_attFile(att, nullptr, 0, &len);
            if (len == 0 || len > 8u * 1024 * 1024) continue;
            std::string buf(len, '\0');
            unsigned long got = 0;
            if (m_attFile(att, &buf[0], len, &got) && got <= len) { buf.resize(got); out = std::move(buf); }
        }
        m_close(doc);
        return out;
    }

    // Seite pageIndex als Bild (BGRA, weisser Hintergrund, laengste Kante = longEdge Pixel) fuer die Texterkennung; false, wenn es die Seite nicht gibt
    bool RenderPageBgra(const std::wstring& path, int pageIndex, int longEdge, std::vector<unsigned char>& bgra, int& w, int& h) const {
        if (!m_ok || !m_pageW || !m_pageH || !m_bmpCreate || !m_bmpFill || !m_bmpBuf || !m_bmpStride || !m_bmpDestroy || !m_render || pageIndex < 0) return false;
        std::ifstream in(std::filesystem::path(path), std::ios::binary);
        if (!in) return false;
        std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (data.empty() || data.size() > 128u * 1024 * 1024) return false;
        void* doc = m_load(data.data(), static_cast<int>(data.size()), nullptr);
        if (!doc) return false;
        bool ok = false;
        if (pageIndex < m_count(doc)) {
            if (void* page = m_page(doc, pageIndex)) {
                const float pw = m_pageW(page), ph = m_pageH(page);
                if (pw > 1 && ph > 1) {
                    const float scale = static_cast<float>(longEdge) / std::max(pw, ph);
                    w = std::max(1, static_cast<int>(pw * scale));
                    h = std::max(1, static_cast<int>(ph * scale));
                    if (void* bmp = m_bmpCreate(w, h, 0)) {
                        m_bmpFill(bmp, 0, 0, w, h, 0xFFFFFFFFul);
                        m_render(bmp, page, 0, 0, w, h, 0, 0x01 /* FPDF_ANNOT */);
                        const unsigned char* src = static_cast<const unsigned char*>(m_bmpBuf(bmp));
                        const int stride = m_bmpStride(bmp);
                        if (src && stride >= w * 4) {
                            bgra.resize(static_cast<size_t>(w) * h * 4);
                            for (int y = 0; y < h; ++y) std::copy(src + static_cast<size_t>(y) * stride, src + static_cast<size_t>(y) * stride + static_cast<size_t>(w) * 4, bgra.begin() + static_cast<size_t>(y) * w * 4);
                            for (size_t i = 3; i < bgra.size(); i += 4) bgra[i] = 255;
                            ok = true;
                        }
                        m_bmpDestroy(bmp);
                    }
                }
                m_closePage(page);
            }
        }
        m_close(doc);
        return ok;
    }

    // Text der ersten maxPages Seiten (UTF-16); leer bei Fehler oder verschluesselter/gescannter PDF
    std::wstring Extract(const std::wstring& path, int maxPages = 3) const {
        std::wstring out;
        if (!m_ok) return out;
        std::ifstream in(std::filesystem::path(path), std::ios::binary);
        if (!in) return out;
        std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (data.empty() || data.size() > 128u * 1024 * 1024) return out;
        void* doc = m_load(data.data(), static_cast<int>(data.size()), nullptr);
        if (!doc) return out;
        const int pages = std::min(m_count(doc), maxPages);
        for (int p = 0; p < pages; ++p) {
            void* page = m_page(doc, p);
            if (!page) continue;
            if (void* tp = m_textLoad(page)) {
                const int n = m_chars(tp);
                if (n > 0) {
                    std::vector<unsigned short> buf(static_cast<size_t>(n) + 1, 0);
                    const int got = m_text(tp, 0, n, buf.data());
                    for (int i = 0; i + 1 < got; ++i) out.push_back(buf[i] == 0 ? L' ' : static_cast<wchar_t>(buf[i]));   // got zaehlt die abschliessende 0 mit
                    out.push_back(L'\n');
                }
                m_textClose(tp);
            }
            m_closePage(page);
        }
        m_close(doc);
        return out;
    }

private:
    struct Config { int version; const char** fonts; void* isolate; unsigned slot; void* platform; int renderer; };
    template <class T> bool Get(const char* name, T& fn) { fn = reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(m_mod, name))); return fn != nullptr; }
    void Load(const std::vector<std::wstring>& dirs) {
        for (const std::wstring& d : dirs) {
            const std::wstring p = d + L"\\pdfium.dll";
            if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            m_mod = LoadLibraryExW(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (m_mod) break;
        }
        if (!m_mod) return;
        void (*init)(const Config*) = nullptr;
        m_ok = Get("FPDF_InitLibraryWithConfig", init) && Get("FPDF_LoadMemDocument", m_load) && Get("FPDF_GetPageCount", m_count) &&
               Get("FPDF_LoadPage", m_page) && Get("FPDF_ClosePage", m_closePage) && Get("FPDF_CloseDocument", m_close) &&
               Get("FPDFText_LoadPage", m_textLoad) && Get("FPDFText_ClosePage", m_textClose) && Get("FPDFText_CountChars", m_chars) &&
               Get("FPDFText_GetText", m_text);
        Get("FPDFDoc_GetAttachmentCount", m_attCount);       // optional: aeltere DLLs ohne Anhaenge lesen weiter den Text
        Get("FPDFDoc_GetAttachment", m_att);
        Get("FPDFAttachment_GetName", m_attName);
        Get("FPDFAttachment_GetFile", m_attFile);
        Get("FPDF_GetPageWidthF", m_pageW);          // optional: nur fuer die Texterkennung gescannter PDFs
        Get("FPDF_GetPageHeightF", m_pageH);
        Get("FPDFBitmap_Create", m_bmpCreate);
        Get("FPDFBitmap_FillRect", m_bmpFill);
        Get("FPDF_RenderPageBitmap", m_render);
        Get("FPDFBitmap_GetBuffer", m_bmpBuf);
        Get("FPDFBitmap_GetStride", m_bmpStride);
        Get("FPDFBitmap_Destroy", m_bmpDestroy);
        if (m_ok) { Config cfg{}; cfg.version = 2; init(&cfg); }
    }
    HMODULE m_mod = nullptr;
    bool m_ok = false;
    void* (*m_load)(const void*, int, const char*) = nullptr;
    int (*m_count)(void*) = nullptr;
    void* (*m_page)(void*, int) = nullptr;
    void (*m_closePage)(void*) = nullptr;
    void (*m_close)(void*) = nullptr;
    void* (*m_textLoad)(void*) = nullptr;
    void (*m_textClose)(void*) = nullptr;
    int (*m_chars)(void*) = nullptr;
    int (*m_text)(void*, int, int, unsigned short*) = nullptr;
    int (*m_attCount)(void*) = nullptr;
    void* (*m_att)(void*, int) = nullptr;
    unsigned long (*m_attName)(void*, unsigned short*, unsigned long) = nullptr;
    int (*m_attFile)(void*, void*, unsigned long, unsigned long*) = nullptr;
    float (*m_pageW)(void*) = nullptr;
    float (*m_pageH)(void*) = nullptr;
    void* (*m_bmpCreate)(int, int, int) = nullptr;
    void (*m_bmpFill)(void*, int, int, int, int, unsigned long) = nullptr;
    void (*m_render)(void*, void*, int, int, int, int, int, int) = nullptr;
    void* (*m_bmpBuf)(void*) = nullptr;
    int (*m_bmpStride)(void*) = nullptr;
    void (*m_bmpDestroy)(void*) = nullptr;
};

}  // namespace docs
