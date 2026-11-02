// Test fuer docs/PdfText.h + InvoiceText.h: Text aus einer echten PDF lesen und Betrag/Faelligkeit raten.
// Braucht pdfium.dll neben der EXE; fehlt sie, wird der Test uebersprungen (SKIP, Exitcode 0).
#include <cstdio>
#include <string>
#include <vector>

#include "../InvoiceText.h"
#include "../PdfText.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static std::string MakePdf() {
    const std::string content =
        "BT /F1 14 Tf 72 700 Td (Rechnung Nr. 4711) Tj ET\n"
        "BT /F1 14 Tf 72 670 Td (Gesamtbetrag brutto: 1.234,56 EUR) Tj ET\n"
        "BT /F1 14 Tf 72 640 Td (Zahlbar bis 15.11.2026) Tj ET\n";
    std::vector<std::string> obj;
    obj.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    obj.push_back("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>");
    obj.push_back("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream");
    obj.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> off;
    for (size_t i = 0; i < obj.size(); ++i) { off.push_back(pdf.size()); pdf += std::to_string(i + 1) + " 0 obj\n" + obj[i] + "\nendobj\n"; }
    const size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(obj.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t o : off) { char b[32]; std::snprintf(b, sizeof(b), "%010zu 00000 n \n", o); pdf += b; }
    pdf += "trailer\n<< /Size " + std::to_string(obj.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return pdf;
}

int main() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir.resize(dir.find_last_of(L'\\'));
    docs::PdfText reader({ dir });
    if (!reader.Ready()) { std::printf("SKIP: pdfium.dll nicht gefunden\n"); return 0; }
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring path = std::wstring(tmp) + L"duncans_docs_pdftext.pdf";
    {
        const std::string pdf = MakePdf();
        HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD w = 0;
        WriteFile(f, pdf.data(), static_cast<DWORD>(pdf.size()), &w, nullptr);
        CloseHandle(f);
    }
    const std::wstring text = reader.Extract(path);
    CHECK(text.find(L"Gesamtbetrag") != std::wstring::npos && text.find(L"1.234,56") != std::wstring::npos);
    const docs::ScannedInvoice si = docs::ScanInvoiceText(text);
    CHECK(si.hasAmount && si.cents == 123456 && si.hasDue && si.dueIso == L"2026-11-15");
    CHECK(reader.Extract(L"C:\\gibt\\es\\nicht.pdf").empty());
    DeleteFileW(path.c_str());
    if (g_fail) { std::printf("%d Pruefung(en) fehlgeschlagen\n", g_fail); return 1; }
    std::printf("PDF-Text und Rechnungserkennung: alle Pruefungen bestanden\n");
    return 0;
}
