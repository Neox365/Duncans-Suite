#pragma once
// PDF Reader: Texterkennung fuer gescannte Seiten. Seiten ohne eigenen Text werden gerendert, mit der Windows-Texterkennung (common/Ocr.h)
// gelesen, und das Ergebnis kommt als unsichtbarer Text auf die Seite zurueck: danach sind Suche und Kopieren moeglich, das Bild bleibt unveraendert.
#include <functional>
#include <string>
#include <vector>

#include "../common/Ocr.h"
#include "PdfDocument.h"

namespace pdfocr {

struct Summary {
    int pages = 0;          // Seiten, auf denen Text erkannt wurde
    int words = 0;          // geschriebene Woerter
    int skipped = 0;        // Seiten, die schon Text hatten (nicht angefasst)
    int empty = 0;          // Seiten, auf denen nichts erkannt wurde (leer, Zeichnung)
    bool cancelled = false;
    std::wstring error;     // z. B. fehlendes Windows-Sprachpaket
};

constexpr int kMinTextChars = 20;   // so viele Zeichen Text gelten als "hat schon Text" (Scans haben 0 oder nur Rauschen)
constexpr int kLongEdge = 2800;     // Rendergroesse der Seite fuer die Erkennung (~240 dpi bei A4)

// progress(done, total): vor jeder Seite; false = abbrechen. Legt einen Rueckgaengig-Schritt fuer die ganze Erkennung an.
inline Summary Run(PdfDocument& doc, const std::function<bool(int done, int total)>& progress)
{
    Summary s;
    const int n = doc.PageCount();
    std::vector<int> todo;
    for (int p = 0; p < n; ++p) {
        const int chars = doc.CountTextChars(p);
        if (chars >= kMinTextChars) ++s.skipped; else todo.push_back(p);
    }
    if (todo.empty()) return s;
    doc.MarkUndoPoint();
    std::vector<unsigned char> px;
    for (size_t i = 0; i < todo.size(); ++i) {
        if (progress && !progress(static_cast<int>(i), static_cast<int>(todo.size()))) { s.cancelled = true; break; }
        int w = 0, h = 0;
        if (!doc.RenderPageBgra(todo[i], kLongEdge, px, w, h)) continue;
        const ocr::Result r = ocr::Recognize(px.data(), w, h);
        if (!r.ok) { s.error = r.error; break; }
        std::vector<PdfDocument::OcrWord> words;
        for (const ocr::Line& ln : r.lines)
            for (const ocr::Word& wd : ln.words) {
                PdfDocument::OcrWord o;
                o.text = wd.text; o.x = wd.x; o.y = wd.y; o.w = wd.w; o.h = wd.h;
                words.push_back(std::move(o));
            }
        const int written = doc.AddTextLayer(todo[i], w, h, words);
        if (written > 0) { ++s.pages; s.words += written; } else ++s.empty;
    }
    return s;
}

}  // namespace pdfocr
