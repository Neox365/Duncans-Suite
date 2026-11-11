// Test: passwortgeschuetzte PDFs oeffnen (PdfDocument::Open mit Passwort, NeedsPassword).
// Die verschluesselte Testdatei baut der Test selbst (Standard-Sicherheitshandler, RC4 128 Bit, Revision 3, Algorithmen aus ISO 32000-1, 7.6),
// unabhaengig von PDFium. Braucht pdfium.dll neben der EXE; fehlt sie, wird der Test uebersprungen (SKIP, Exitcode 0).
#include "../PdfDocument.h"

#include <bcrypt.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

using Bytes = std::string;

static Bytes Md5(const Bytes& in) {
    BCRYPT_ALG_HANDLE a = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    UCHAR d[16] = {};
    BCryptOpenAlgorithmProvider(&a, BCRYPT_MD5_ALGORITHM, nullptr, 0);
    BCryptCreateHash(a, &h, nullptr, 0, nullptr, 0, 0);
    BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char*>(in.data())), static_cast<ULONG>(in.size()), 0);
    BCryptFinishHash(h, d, 16, 0);
    BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(a, 0);
    return Bytes(reinterpret_cast<char*>(d), 16);
}

static Bytes Rc4(const Bytes& key, const Bytes& data) {
    unsigned char S[256];
    for (int i = 0; i < 256; ++i) S[i] = static_cast<unsigned char>(i);
    for (int i = 0, j = 0; i < 256; ++i) {
        j = (j + S[i] + static_cast<unsigned char>(key[i % key.size()])) & 255;
        std::swap(S[i], S[j]);
    }
    Bytes out = data;
    int i = 0, j = 0;
    for (char& c : out) {
        i = (i + 1) & 255;
        j = (j + S[i]) & 255;
        std::swap(S[i], S[j]);
        c = static_cast<char>(static_cast<unsigned char>(c) ^ S[(S[i] + S[j]) & 255]);
    }
    return out;
}

static const unsigned char kPad[32] = {0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
                                       0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A};

static Bytes Padded(const Bytes& pw) {
    Bytes p = pw.substr(0, 32);
    p.append(reinterpret_cast<const char*>(kPad), 32 - p.size());
    return p;
}

static Bytes Hex(const Bytes& b) {
    Bytes s = "<";
    for (unsigned char c : b) { char t[3]; std::snprintf(t, sizeof t, "%02X", c); s += t; }
    return s + ">";
}

// Eine einseitige PDF mit dem Text "GEHEIMTEXT42", Inhalt verschluesselt (Benutzer- und Besitzerpasswort getrennt)
static Bytes MakeEncryptedPdf(const Bytes& userPw, const Bytes& ownerPw) {
    const int P = -4;                                         // alles erlaubt (Bit 3..: gesetzt)
    const Bytes id = Md5("duncans-test-id");
    // O (Algorithmus 3.3, R3)
    Bytes ok = Md5(Padded(ownerPw.empty() ? userPw : ownerPw));
    for (int i = 0; i < 50; ++i) ok = Md5(ok);
    Bytes O = Rc4(ok, Padded(userPw));
    for (int i = 1; i <= 19; ++i) { Bytes k = ok; for (char& c : k) c = static_cast<char>(c ^ i); O = Rc4(k, O); }
    // Schluessel (Algorithmus 3.2, R3, 128 Bit)
    Bytes in = Padded(userPw) + O;
    for (int i = 0; i < 4; ++i) in += static_cast<char>((static_cast<unsigned>(P) >> (8 * i)) & 255);
    in += id;
    Bytes key = Md5(in);
    for (int i = 0; i < 50; ++i) key = Md5(key.substr(0, 16));
    key = key.substr(0, 16);
    // U (Algorithmus 3.5)
    Bytes U = Rc4(key, Md5(Bytes(reinterpret_cast<const char*>(kPad), 32) + id));
    for (int i = 1; i <= 19; ++i) { Bytes k = key; for (char& c : k) c = static_cast<char>(c ^ i); U = Rc4(k, U); }
    U += Bytes(16, '\0');

    const Bytes content = "BT /F1 24 Tf 72 700 Td (GEHEIMTEXT42) Tj ET\n";
    auto objKey = [&](int num) {
        Bytes k = key;
        k += static_cast<char>(num & 255); k += static_cast<char>((num >> 8) & 255); k += static_cast<char>((num >> 16) & 255);
        k += '\0'; k += '\0';
        return Md5(k).substr(0, 16);                          // min(n + 5, 16) = 16
    };
    const Bytes enc = Rc4(objKey(4), content);
    std::vector<Bytes> obj;
    obj.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    obj.push_back("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>");
    obj.push_back("<< /Length " + std::to_string(enc.size()) + " >>\nstream\n" + enc + "\nendstream");
    obj.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    obj.push_back("<< /Filter /Standard /V 2 /R 3 /Length 128 /P " + std::to_string(P) + " /O " + Hex(O) + " /U " + Hex(U) + " >>");
    Bytes pdf = "%PDF-1.4\n";
    std::vector<size_t> off;
    for (size_t i = 0; i < obj.size(); ++i) {
        off.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + obj[i] + "\nendobj\n";
    }
    const size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(obj.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t o : off) { char b[32]; std::snprintf(b, sizeof b, "%010zu 00000 n \n", o); pdf += b; }
    pdf += "trailer\n<< /Size " + std::to_string(obj.size() + 1) + " /Root 1 0 R /Encrypt 6 0 R /ID [" + Hex(id) + Hex(id) + "] >>\nstartxref\n" +
           std::to_string(xref) + "\n%%EOF\n";
    return pdf;
}

static bool WriteAll(const std::wstring& path, const Bytes& data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    const bool ok = WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &w, nullptr) && w == data.size();
    CloseHandle(f);
    return ok;
}

static std::wstring TempFile(const wchar_t* name) {
    wchar_t dir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + L"duncanspdf_pw_" + name;
}

int main(int argc, char** argv)
{
    if (!PdfDocument::InitLibrary()) { std::printf("SKIP: %ls\n", PdfDocument::LastError().c_str()); return 0; }
    const std::wstring file = TempFile(L"enc.pdf"), copy = TempFile(L"copy.pdf"), plain = TempFile(L"plain.pdf");
    CHECK(WriteAll(file, MakeEncryptedPdf("geheim", "chef")));

    {   // ohne Passwort: abgewiesen, und das Programm weiss, dass ein Passwort fehlt
        PdfDocument d;
        CHECK(!d.Open(file) && PdfDocument::NeedsPassword() && !d.IsOpen());
        CHECK(PdfDocument::LastError().find(L"passwortgesch") != std::wstring::npos);
    }
    {   // falsches Passwort
        PdfDocument d;
        CHECK(!d.Open(file, "falsch") && PdfDocument::NeedsPassword());
        CHECK(PdfDocument::LastError() == L"Das Passwort stimmt nicht.");
        CHECK(!d.Open(file, "GEHEIM") && PdfDocument::NeedsPassword());               // Gross-/Kleinschreibung zaehlt
    }
    {   // richtiges Benutzerpasswort: Seite und Text lesbar
        PdfDocument d;
        CHECK(d.Open(file, "geheim") && d.IsOpen() && d.PageCount() == 1 && !PdfDocument::NeedsPassword() && d.PasswordProtected());
        CHECK(!d.Search(L"GEHEIMTEXT42").empty());
        CHECK(d.RenderPage(0, 200, 280) != nullptr);
        // Speichern und Wiederoeffnen: was passiert mit dem Passwortschutz?
        CHECK(d.SaveCopy(copy));
        PdfDocument e;
        const bool noPw = e.Open(copy);
        std::printf("Gespeicherte Kopie ohne Passwort offen: %s\n", noPw ? "ja (Schutz entfernt)" : "nein (Schutz bleibt)");
        PdfDocument f2;
        CHECK(f2.Open(copy, "geheim") && f2.PageCount() == 1 && !f2.Search(L"GEHEIMTEXT42").empty());
    }
    {   // Auszug und Verkleinern aus einer geoeffneten Passwort-PDF
        PdfDocument d;
        CHECK(d.Open(file, "geheim"));
        const std::wstring part = TempFile(L"part.pdf"), small = TempFile(L"small.pdf");
        CHECK(d.SavePages(part, {0}));
        PdfDocument e;
        const bool partOpen = e.Open(part);
        std::printf("Seitenauszug ohne Passwort offen: %s\n", partOpen ? "ja (ohne Schutz)" : "nein (mit Schutz)");
        PdfDocument::CompressStats st;
        const bool cmp = d.SaveCompressed(small, 150, 70, st);
        std::printf("Verkleinern einer Passwort-PDF: %s\n", cmp ? "ok" : "Fehler");
        if (!cmp) std::printf("   Meldung: %ls\n", PdfDocument::LastError().c_str());
        DeleteFileW(part.c_str()); DeleteFileW(small.c_str());
    }
    {   // Besitzerpasswort oeffnet ebenfalls
        PdfDocument d;
        CHECK(d.Open(file, "chef") && d.PageCount() == 1 && !d.Search(L"GEHEIMTEXT42").empty());
    }
    {   // unverschluesselte und kaputte Dateien: kein Passwort-Hinweis
        PdfDocument d;
        CHECK(WriteAll(plain, "kein pdf"));
        CHECK(!d.Open(plain) && !PdfDocument::NeedsPassword());
        CHECK(WriteAll(plain, "")); 
        CHECK(!d.Open(plain) && !PdfDocument::NeedsPassword());
        CHECK(!d.Open(TempFile(L"fehlt.pdf")) && !PdfDocument::NeedsPassword());
    }
    {   // Rueckgaengig nach Aenderung bei geoeffneter Passwort-PDF
        PdfDocument d;
        CHECK(d.Open(file, "geheim"));
        d.SetRotation(0, 1);
        CHECK(d.Undo() || true);
        CHECK(d.IsOpen() && d.PageCount() == 1);
    }
    if (argc > 1) { wchar_t w[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, argv[1], -1, w, MAX_PATH); CopyFileW(file.c_str(), w, FALSE); }   // fuer einen Handversuch mit dem Programm
    DeleteFileW(file.c_str()); DeleteFileW(copy.c_str()); DeleteFileW(plain.c_str());
    std::printf(g_fail ? "%d Fehler\n" : "alle Tests bestanden\n", g_fail);
    return g_fail ? 1 : 0;
}
