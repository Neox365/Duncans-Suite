// Test: unsichtbarer Text ueber gescannten Seiten (PdfDocument::AddTextLayer) - Durchsuchbarkeit, Lage, Drehung, Speichern, Rueckgaengig.
// Braucht pdfium.dll neben der EXE; fehlt sie, wird der Test uebersprungen (SKIP, Exitcode 0).
#include "../PdfDocument.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

static std::wstring TempFile(const wchar_t* name)
{
    wchar_t dir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + L"duncanspdf_ocr_" + name;
}

static bool WriteAll(const std::wstring& path, const std::string& data)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    const bool ok = WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &w, nullptr) && w == data.size();
    CloseHandle(f);
    return ok;
}

// Leere A4-Seite (595 x 842) ohne jeden Text, optional mit /Rotate
static std::string BlankPdf(int rotate)
{
    std::vector<std::string> obj;
    obj.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    obj.push_back("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Rotate " + std::to_string(rotate) + " /Contents 4 0 R /Resources << >> >>");
    obj.push_back("<< /Length 0 >>\nstream\n\nendstream");
    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> off;
    for (size_t i = 0; i < obj.size(); ++i) {
        off.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + obj[i] + "\nendobj\n";
    }
    const size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(obj.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t o : off) { char b[32]; std::snprintf(b, sizeof b, "%010zu 00000 n \n", o); pdf += b; }
    pdf += "trailer\n<< /Size " + std::to_string(obj.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return pdf;
}

static PdfDocument::OcrWord W(const wchar_t* t, float x, float y, float w, float h)
{
    PdfDocument::OcrWord o;
    o.text = t; o.x = x; o.y = y; o.w = w; o.h = h;
    return o;
}

// Liegt der erste Treffer von needle ungefaehr im Rahmen (x, y, w, h) des Bildes (w x h Pixel)?
static bool HitNear(const PdfDocument& d, const wchar_t* needle, int imgW, int imgH, float x, float y, float w, float h, float tol)
{
    const auto hits = d.Search(needle);
    if (hits.empty()) return false;
    std::vector<RECT> rc;
    d.HitRects(hits[0], imgW, imgH, rc);
    if (rc.empty()) return false;
    LONG l = rc[0].left, t = rc[0].top, r = rc[0].right, b = rc[0].bottom;
    for (const RECT& q : rc) { l = std::min(l, q.left); t = std::min(t, q.top); r = std::max(r, q.right); b = std::max(b, q.bottom); }
    return std::abs(l - x) <= tol && std::abs(r - (x + w)) <= tol && t >= y - tol && b <= y + h + tol && (b - t) > h * 0.3f;
}

int main()
{
    if (!PdfDocument::InitLibrary()) { std::printf("SKIP: %ls\n", PdfDocument::LastError().c_str()); return 0; }
    const std::wstring file = TempFile(L"blank.pdf"), saved = TempFile(L"saved.pdf");
    CHECK(WriteAll(file, BlankPdf(0)));
    {
        PdfDocument d;
        CHECK(d.Open(file) && d.CanAddTextLayer());
        CHECK(d.CountTextChars(0) == 0);                                   // ein Scan: keine Textebene
        CHECK(d.Search(L"Rechnung").empty());
        // Seitenbild fuer die Erkennung: 595 x 842 pt, laengste Kante 1684 px = Faktor 2
        std::vector<unsigned char> px;
        int w = 0, h = 0;
        CHECK(d.RenderPageBgra(0, 1684, px, w, h) && w == 1190 && h == 1684 && px.size() == static_cast<size_t>(w) * h * 4);
        CHECK(px[0] == 255 && px[1] == 255 && px[2] == 255 && px[3] == 255);   // leere Seite = weiss
        d.MarkUndoPoint();
        const std::vector<PdfDocument::OcrWord> words = {W(L"Rechnung", 100, 100, 300, 50), W(L"Müller", 100, 200, 220, 50), W(L"Straße", 400, 200, 200, 50),
                                                         W(L"1.234,56", 700, 900, 240, 50), W(L"", 5, 5, 10, 10), W(L"leer", 5, 5, 0, 0)};
        CHECK(d.AddTextLayer(0, w, h, words) == 4);                         // leeres Wort und Nullbreite werden uebersprungen
        CHECK(d.Modified());
        CHECK(d.CountTextChars(0) > 20);
        CHECK(HitNear(d, L"Rechnung", w, h, 100, 100, 300, 50, 12));
        CHECK(HitNear(d, L"Müller", w, h, 100, 200, 220, 50, 12));
        CHECK(HitNear(d, L"Straße", w, h, 400, 200, 200, 50, 12));
        CHECK(HitNear(d, L"1.234,56", w, h, 700, 900, 240, 50, 12));
        CHECK(!d.Search(L"RECHNUNG").empty());                              // Gross-/Kleinschreibung egal wie bei jeder Suche
        // Speichern, neu oeffnen: Text bleibt, bleibt unsichtbar (Seite sieht weiterhin weiss aus)
        CHECK(d.SaveCopy(saved));
        PdfDocument e;
        CHECK(e.Open(saved) && !e.Search(L"Rechnung").empty() && e.CountTextChars(0) > 20);
        std::vector<unsigned char> px2;
        int w2 = 0, h2 = 0;
        CHECK(e.RenderPageBgra(0, 1684, px2, w2, h2));
        size_t nonWhite = 0;
        for (size_t i = 0; i < px2.size(); i += 4) nonWhite += (px2[i] != 255 || px2[i + 1] != 255 || px2[i + 2] != 255) ? 1 : 0;
        CHECK(nonWhite == 0);                                               // unsichtbar: kein einziger Bildpunkt veraendert
        // Rueckgaengig: der Text ist wieder weg
        CHECK(d.CanUndo() && d.Undo());
        CHECK(d.Search(L"Rechnung").empty() && d.CountTextChars(0) == 0);
        CHECK(d.Redo() && !d.Search(L"Rechnung").empty());
    }
    {   // Zusaetzliche Drehung im Programm (90 Grad): Bild ist 1684 x 1190, Wortrahmen beziehen sich auf das gedrehte Bild
        PdfDocument d;
        CHECK(d.Open(file));
        d.SetRotation(0, 1);
        std::vector<unsigned char> px;
        int w = 0, h = 0;
        CHECK(d.RenderPageBgra(0, 1684, px, w, h) && w == 1684 && h == 1190);
        CHECK(d.AddTextLayer(0, w, h, {W(L"Rechnung", 200, 300, 400, 60)}) == 1);
        CHECK(HitNear(d, L"Rechnung", w, h, 200, 300, 400, 60, 14));
    }
    {   // Seite mit eigenem /Rotate 90 in der Datei
        const std::wstring rf = TempFile(L"rot90.pdf");
        CHECK(WriteAll(rf, BlankPdf(90)));
        PdfDocument d;
        CHECK(d.Open(rf));
        std::vector<unsigned char> px;
        int w = 0, h = 0;
        CHECK(d.RenderPageBgra(0, 1684, px, w, h) && w == 1684 && h == 1190);
        CHECK(d.AddTextLayer(0, w, h, {W(L"Rechnung", 300, 500, 500, 80)}) == 1);
        CHECK(HitNear(d, L"Rechnung", w, h, 300, 500, 500, 80, 14));
        DeleteFileW(rf.c_str());
    }
    {   // Ungueltige Eingaben
        PdfDocument d;
        CHECK(d.AddTextLayer(0, 100, 100, {W(L"x", 1, 1, 5, 5)}) == -1);   // nichts offen
        CHECK(d.Open(file));
        CHECK(d.AddTextLayer(5, 100, 100, {W(L"x", 1, 1, 5, 5)}) == -1);   // Seite gibt es nicht
        CHECK(d.AddTextLayer(0, 0, 0, {W(L"x", 1, 1, 5, 5)}) == -1);
        CHECK(d.AddTextLayer(0, 1190, 1684, {}) == 0 && !d.Modified());
        std::vector<unsigned char> px;
        int w = 0, h = 0;
        CHECK(!d.RenderPageBgra(3, 1000, px, w, h));
    }
    DeleteFileW(file.c_str());
    DeleteFileW(saved.c_str());
    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
