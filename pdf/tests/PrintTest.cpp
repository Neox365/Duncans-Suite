// Test von Text kopieren (PdfDocument::GetText) und Drucken (Print.cpp). Braucht pdfium.dll neben der EXE (fehlt sie: SKIP).
// Gedruckt wird in "Microsoft Print to PDF" mit Ausgabedatei (keine Rueckfrage); fehlt dieser Drucker, wird nur dieser Teil uebersprungen.
// g++ -std=c++17 -DUNICODE -D_UNICODE -I.. PrintTest.cpp ../PdfDocument.cpp ../Print.cpp -o PrintTest.exe -lgdiplus -lcomdlg32 -lwinspool -lgdi32 -luser32 -lole32 -ladvapi32 -lshell32 -static
#include "../PdfDocument.h"
#include "../Print.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static std::wstring TempFile(const wchar_t* name) {
    wchar_t dir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + L"duncanspdf_print_" + name;
}

static bool WriteAll(const std::wstring& path, const std::string& data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    const bool ok = WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &w, nullptr) && w == data.size();
    CloseHandle(f);
    return ok;
}

static std::string ReadAll(const std::wstring& path) {
    std::string s;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return s;
    char buf[65536];
    DWORD got = 0;
    while (ReadFile(f, buf, sizeof(buf), &got, nullptr) && got) s.append(buf, got);
    CloseHandle(f);
    return s;
}

static std::string MakePdf() {
    const std::string content =
        "BT /F1 18 Tf 72 700 Td (Hello public line) Tj ET\n"
        "BT /F1 18 Tf 72 650 Td (SECRETIBAN123) Tj ET\n";
    std::vector<std::string> obj;
    obj.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    obj.push_back("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>");
    obj.push_back("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream");
    obj.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> off;
    for (size_t i = 0; i < obj.size(); ++i) {
        off.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + obj[i] + "\nendobj\n";
    }
    const size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(obj.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t o : off) { char b[32]; std::snprintf(b, sizeof(b), "%010zu 00000 n \n", o); pdf += b; }
    pdf += "trailer\n<< /Size " + std::to_string(obj.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return pdf;
}

int main() {
    // Seitenbereich (reine Logik)
    CHECK((pdfprint::PageRange(5, 0, 0) == std::vector<int>{0, 1, 2, 3, 4}));
    CHECK((pdfprint::PageRange(5, 2, 4) == std::vector<int>{1, 2, 3}));
    CHECK((pdfprint::PageRange(5, 4, 2) == std::vector<int>{1, 2, 3}));      // vertauscht
    CHECK((pdfprint::PageRange(5, 3, 99) == std::vector<int>{2, 3, 4}));     // zu gross: begrenzt
    CHECK(pdfprint::PageRange(0, 1, 1).empty());

    if (!PdfDocument::InitLibrary()) { std::printf("SKIP: %ls\n", PdfDocument::LastError().c_str()); return g_fail ? 1 : 0; }
    const std::wstring src = TempFile(L"src.pdf");
    CHECK(WriteAll(src, MakePdf()));
    PdfDocument d;
    CHECK(d.Open(src));

    // Text kopieren: die Treffer der Suche ergeben genau den gesuchten Text
    {
        const auto hits = d.Search(L"SECRET");
        CHECK(hits.size() == 1);
        if (!hits.empty()) {
            std::wstring t;
            CHECK(d.GetText(hits[0].page, hits[0].start, hits[0].count, t));
            CHECK(t == L"SECRET");
        }
        const auto all = d.Search(L"Hello");
        std::wstring line;
        CHECK(!all.empty() && d.GetText(all[0].page, all[0].start, 17, line) && line.find(L"Hello public line") == 0);
        std::wstring none;
        CHECK(!d.GetText(0, 0, 0, none) && !d.GetText(0, -1, 5, none) && !d.GetText(7, 0, 5, none));
    }

    // Drucken in eine PDF-Datei
    HDC dc = CreateDCW(L"WINSPOOL", L"Microsoft Print to PDF", nullptr, nullptr);
    if (!dc) {
        std::printf("Hinweis: Drucker \"Microsoft Print to PDF\" fehlt, Drucktest uebersprungen\n");
    } else {
        const std::wstring out = TempFile(L"out.pdf");
        DeleteFileW(out.c_str());
        std::wstring err;
        const bool ok = pdfprint::PrintPages(dc, d, {0, 0}, L"Testauftrag", out.c_str(), err);
        if (!ok) std::printf("Fehler beim Drucken: %ls\n", err.c_str());
        CHECK(ok);
        DeleteDC(dc);
        const std::string data = ReadAll(out);
        CHECK(data.size() > 1000);
        CHECK(data.compare(0, 5, "%PDF-") == 0);
        PdfDocument printed;
        CHECK(printed.Open(out) && printed.PageCount() == 2);               // zwei Seiten gedruckt
        double w = 0, h = 0;
        CHECK(printed.PageSize(0, w, h) && w > 500 && w < 650 && h > 750 && h < 900);   // etwa A4
        DeleteFileW(out.c_str());

        // Fehlerfaelle
        HDC dc2 = CreateDCW(L"WINSPOOL", L"Microsoft Print to PDF", nullptr, nullptr);
        std::wstring e2;
        CHECK(!pdfprint::PrintPages(dc2, d, {}, L"x", nullptr, e2) && !e2.empty());   // keine Seiten
        CHECK(!pdfprint::PrintPages(nullptr, d, {0}, L"x", nullptr, e2));            // kein Geraet
        if (dc2) DeleteDC(dc2);
    }
    d.Close();
    DeleteFileW(src.c_str());
    std::printf("%s\n", g_fail ? "FEHLGESCHLAGEN" : "alle Pruefungen bestanden");
    return g_fail ? 1 : 0;
}
