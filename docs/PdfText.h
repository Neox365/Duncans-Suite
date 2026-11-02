#pragma once
// Liest den Text der ersten Seiten einer PDF mit PDFium (pdfium.dll) - nur zum Erraten von Betrag und Faelligkeit beim Import.
// Die DLL wird NICHT aus dem Suchpfad geladen, sondern nur aus dem Ordner, in dem auch Duncan's PDF Reader liegt (Suite-Installation, signiert)
// oder neben dieser EXE. Fehlt sie, bleibt der Import wie bisher (nichts wird geraten).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
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

    // Text der ersten maxPages Seiten (UTF-16); leer bei Fehler oder verschluesselter/gescannter PDF
    std::wstring Extract(const std::wstring& path, int maxPages = 3) const {
        std::wstring out;
        if (!m_ok) return out;
        std::ifstream in(path, std::ios::binary);
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
};

}  // namespace docs
