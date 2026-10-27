// Test der PDF-Bearbeitung: Seiten-Auszug, Stempel/Unterschrift (Bild mit Alphakanal) und Schwaerzen.
// Braucht pdfium.dll neben der EXE; fehlt sie, wird der Test uebersprungen (Ausgabe SKIP, Exitcode 0).
#include "../PdfDocument.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static std::wstring TempFile(const wchar_t* name) {
    wchar_t dir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + L"duncanspdf_" + name;
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

// Einseitige A4-PDF (595 x 842) mit drei Textzeilen; die mittlere ist der "geheime" Text
static std::string MakePdf() {
    const std::string content =
        "BT /F1 18 Tf 72 700 Td (Hello public line) Tj ET\n"
        "BT /F1 18 Tf 72 650 Td (SECRETIBAN123) Tj ET\n"
        "BT /F1 18 Tf 72 600 Td (Another public) Tj ET\n";
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

// ---- Minimaler Inflate-Dekoder (nach dem Muster von zlibs puff.c), damit auch Flate-Streams nach Text durchsucht werden koennen ----
namespace inflate {
struct State {
    const unsigned char* in; size_t inLen, inPos = 0; unsigned bitBuf = 0; int bitCnt = 0;
    std::string out; bool err = false;
};
static int Bits(State& s, int need) {
    long val = s.bitBuf;
    while (s.bitCnt < need) {
        if (s.inPos >= s.inLen) { s.err = true; return 0; }
        val |= static_cast<long>(s.in[s.inPos++]) << s.bitCnt;
        s.bitCnt += 8;
    }
    s.bitBuf = static_cast<unsigned>(val >> need);
    s.bitCnt -= need;
    return static_cast<int>(val & ((1L << need) - 1));
}
struct Huff { short count[16]; short symbol[288]; };
static int Decode(State& s, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; ++len) {
        code |= Bits(s, 1);
        if (s.err) return -1;
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -1;
}
static void Build(Huff& h, const short* length, int n) {
    for (int i = 0; i < 16; ++i) h.count[i] = 0;
    for (int i = 0; i < n; ++i) h.count[length[i]]++;
    short offs[16]; offs[1] = 0;
    for (int i = 1; i < 15; ++i) offs[i + 1] = static_cast<short>(offs[i] + h.count[i]);
    for (int i = 0; i < n; ++i) if (length[i]) h.symbol[offs[length[i]]++] = static_cast<short>(i);
}
static bool Codes(State& s, const Huff& lc, const Huff& dc) {
    static const short lens[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const short lext[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const short dists[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    static const short dext[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    for (;;) {
        int sym = Decode(s, lc);
        if (sym < 0) return false;
        if (sym < 256) s.out.push_back(static_cast<char>(sym));
        else if (sym == 256) return true;
        else {
            sym -= 257;
            if (sym >= 29) return false;
            const int len = lens[sym] + Bits(s, lext[sym]);
            const int ds = Decode(s, dc);
            if (ds < 0 || ds >= 30) return false;
            const size_t dist = static_cast<size_t>(dists[ds] + Bits(s, dext[ds]));
            if (s.err || dist > s.out.size()) return false;
            for (int i = 0; i < len; ++i) s.out.push_back(s.out[s.out.size() - dist]);
        }
    }
}
// data: zlib-Stream (2 Byte Kopf + Deflate). Gibt false zurueck, wenn es kein gueltiger Stream ist.
static bool Zlib(const unsigned char* data, size_t len, std::string& out) {
    if (len < 3) return false;
    State s{ data + 2, len - 2 };
    int last;
    do {
        last = Bits(s, 1);
        const int type = Bits(s, 2);
        if (s.err) return false;
        if (type == 0) {
            s.bitBuf = 0; s.bitCnt = 0;
            if (s.inPos + 4 > s.inLen) return false;
            const unsigned n = s.in[s.inPos] | (s.in[s.inPos + 1] << 8);
            s.inPos += 4;
            if (s.inPos + n > s.inLen) return false;
            s.out.append(reinterpret_cast<const char*>(s.in + s.inPos), n);
            s.inPos += n;
        } else if (type == 1) {
            static Huff lc, dc;
            short l[288];
            int i = 0;
            for (; i < 144; ++i) l[i] = 8;
            for (; i < 256; ++i) l[i] = 9;
            for (; i < 280; ++i) l[i] = 7;
            for (; i < 288; ++i) l[i] = 8;
            Build(lc, l, 288);
            short d[30];
            for (i = 0; i < 30; ++i) d[i] = 5;
            Build(dc, d, 30);
            if (!Codes(s, lc, dc)) return false;
        } else if (type == 2) {
            static const short order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
            const int nlen = Bits(s, 5) + 257, ndist = Bits(s, 5) + 1, ncode = Bits(s, 4) + 4;
            if (s.err || nlen > 286 || ndist > 30) return false;
            short lengths[320] = {};
            for (int i = 0; i < ncode; ++i) lengths[order[i]] = static_cast<short>(Bits(s, 3));
            Huff lencode;
            Build(lencode, lengths, 19);
            int idx = 0;
            short ll[320] = {};
            while (idx < nlen + ndist) {
                int sym = Decode(s, lencode);
                if (sym < 0) return false;
                if (sym < 16) ll[idx++] = static_cast<short>(sym);
                else {
                    int prev = 0, rep;
                    if (sym == 16) { if (idx == 0) return false; prev = ll[idx - 1]; rep = 3 + Bits(s, 2); }
                    else if (sym == 17) rep = 3 + Bits(s, 3);
                    else rep = 11 + Bits(s, 7);
                    if (idx + rep > nlen + ndist) return false;
                    while (rep--) ll[idx++] = static_cast<short>(prev);
                }
            }
            static Huff lc, dc;
            Build(lc, ll, nlen);
            Build(dc, ll + nlen, ndist);
            if (!Codes(s, lc, dc)) return false;
        } else return false;
    } while (!last && !s.err);
    out = s.out;
    return !s.err;
}
}  // namespace inflate

// Alles, was in der Datei als Text steht: Rohbytes plus jeder aufgeblasene Flate-Stream
static std::string AllText(const std::string& file) {
    std::string all = file;
    for (size_t p = file.find("stream"); p != std::string::npos; p = file.find("stream", p + 6)) {
        if (p >= 3 && file.compare(p - 3, 3, "end") == 0) continue;
        size_t b = p + 6;
        if (b < file.size() && file[b] == '\r') ++b;
        if (b < file.size() && file[b] == '\n') ++b;
        const size_t e = file.find("endstream", b);
        if (e == std::string::npos) break;
        std::string dec;
        if (inflate::Zlib(reinterpret_cast<const unsigned char*>(file.data()) + b, e - b, dec)) all += "\n" + dec;
        p = e;
    }
    return all;
}

// Kommt der Text vor, entweder als Klartext in einer Zeichenkette oder als Hex-Zeichenkette (so schreibt PDFium neu erzeugte Seiteninhalte)?
static bool HasText(const std::string& all, const std::string& text) {
    std::string up, lo;
    for (unsigned char c : text) { char b[4]; std::snprintf(b, sizeof(b), "%02X", c); up += b; std::snprintf(b, sizeof(b), "%02x", c); lo += b; }
    return all.find(text) != std::string::npos || all.find(up) != std::string::npos || all.find(lo) != std::string::npos;
}

struct Px { int r, g, b; };
static Px PixelAt(HBITMAP bmp, int x, int y) {
    BITMAP bm{};
    GetObjectW(bmp, sizeof(bm), &bm);
    const unsigned char* p = static_cast<const unsigned char*>(bm.bmBits) + static_cast<size_t>(y) * bm.bmWidthBytes + x * 4;
    return { p[2], p[1], p[0] };
}
static bool Near(Px a, int r, int g, int b) { return std::abs(a.r - r) < 40 && std::abs(a.g - g) < 40 && std::abs(a.b - b) < 40; }

int main() {
    if (!PdfDocument::InitLibrary()) { std::printf("SKIP: %ls\n", PdfDocument::LastError().c_str()); return 0; }
    CHECK(PdfDocument::EditSupported());
    const std::wstring src = TempFile(L"src.pdf");
    CHECK(WriteAll(src, MakePdf()));

    {   // Kontrolle des Suchwerkzeugs: eine ungeschwaerzte Kopie enthaelt den Text (im Klartext oder als Flate-Stream)
        PdfDocument d;
        CHECK(d.Open(src));
        const std::wstring out = TempFile(L"plain.pdf");
        CHECK(d.SaveCopy(out));
        CHECK(HasText(AllText(ReadAll(out)), "SECRETIBAN123"));
        DeleteFileW(out.c_str());
    }

    {   // Auszug
        PdfDocument d;
        CHECK(d.Open(src));
        CHECK(d.PageCount() == 1);
        const std::wstring out = TempFile(L"auszug.pdf");
        CHECK(d.SavePages(out, { 0 }));
        PdfDocument e;
        CHECK(e.Open(out) && e.PageCount() == 1);
        CHECK(!e.Search(L"SECRETIBAN").empty());
        CHECK(!d.SavePages(out, {}));
        DeleteFileW(out.c_str());
    }

    {   // Schwaerzen, Textweg: die mittlere Zeile komplett
        PdfDocument d;
        CHECK(d.Open(src));
        const RECT r{ 60, 170, 330, 200 };                      // Zeile 2 liegt bei y = 842 - 650 = 192 (Grundlinie)
        PdfDocument::RedactResult res;
        CHECK(d.Redact(0, 595, 842, { r }, res));
        CHECK(!res.flattened && res.textRemoved == 1);
        CHECK(d.Redacted() && d.Modified());
        HBITMAP bmp = d.RenderPage(0, 595, 842);
        CHECK(bmp && Near(PixelAt(bmp, 120, 186), 0, 0, 0));      // schwarzer Balken
        if (bmp) DeleteObject(bmp);
        const std::wstring out = TempFile(L"redact1.pdf");
        CHECK(d.SaveCopy(out));
        const std::string bytes = AllText(ReadAll(out));
        CHECK(!HasText(bytes, "SECRETIBAN123"));
        CHECK(HasText(bytes, "Hello public line"));     // der Rest bleibt Text
        PdfDocument e;
        CHECK(e.Open(out));
        CHECK(e.Search(L"SECRETIBAN").empty());
        CHECK(!e.Search(L"Another public").empty());
        DeleteFileW(out.c_str());
    }

    {   // Schwaerzen, Bildweg: nur ein Teil einer Zeile -> Seite wird neu aufgebaut
        PdfDocument d;
        CHECK(d.Open(src));
        const RECT r{ 100, 170, 160, 200 };
        PdfDocument::RedactResult res;
        CHECK(d.Redact(0, 595, 842, { r }, res));
        CHECK(res.flattened);
        CHECK(d.PageCount() == 1);
        const std::wstring out = TempFile(L"redact2.pdf");
        CHECK(d.SaveCopy(out));
        const std::string bytes = AllText(ReadAll(out));
        CHECK(!HasText(bytes, "SECRETIBAN123"));
        CHECK(!HasText(bytes, "Hello public line"));
        PdfDocument e;
        CHECK(e.Open(out) && e.PageCount() == 1);
        double w = 0, h = 0;
        CHECK(e.PageSize(0, w, h) && std::abs(w - 595) < 1 && std::abs(h - 842) < 1);
        HBITMAP bmp = e.RenderPage(0, 595, 842);
        CHECK(bmp && Near(PixelAt(bmp, 130, 186), 0, 0, 0));
        if (bmp) DeleteObject(bmp);
        DeleteFileW(out.c_str());
    }

    // Stempel mit vier verschiedenfarbigen Vierteln; Ausrichtung auf der normalen und der gedrehten Seite pruefen
    PdfDocument::Image img;
    img.w = img.h = 40;
    img.bgra.assign(40 * 40 * 4, 255);
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x) {
            unsigned char* p = &img.bgra[(y * 40 + x) * 4];
            const bool right = x >= 20, bottom = y >= 20;
            const Px c = !right && !bottom ? Px{ 255, 0, 0 } : right && !bottom ? Px{ 0, 200, 0 } : !right ? Px{ 0, 0, 255 } : Px{ 255, 160, 0 };   // links oben rot, rechts oben gruen, links unten blau, rechts unten orange
            p[0] = static_cast<unsigned char>(c.b); p[1] = static_cast<unsigned char>(c.g); p[2] = static_cast<unsigned char>(c.r); p[3] = 255;
        }
    for (int rot = 0; rot < 4; ++rot) {
        PdfDocument d;
        CHECK(d.Open(src));
        d.SetRotation(0, rot);
        const int pw = (rot & 1) ? 842 : 595, ph = (rot & 1) ? 595 : 842;
        const RECT r{ 300, 400, 400, 500 };
        CHECK(d.PlaceImage(0, pw, ph, r, img));
        HBITMAP bmp = d.RenderPage(0, pw, ph);
        CHECK(bmp != nullptr);
        if (bmp) {
            CHECK(Near(PixelAt(bmp, 325, 425), 255, 0, 0));
            CHECK(Near(PixelAt(bmp, 375, 425), 0, 200, 0));
            CHECK(Near(PixelAt(bmp, 325, 475), 0, 0, 255));
            CHECK(Near(PixelAt(bmp, 375, 475), 255, 160, 0));
            DeleteObject(bmp);
        }
        const std::wstring out = TempFile(L"stamp.pdf");
        CHECK(d.SaveCopy(out));
        PdfDocument e;
        CHECK(e.Open(out));
        HBITMAP b2 = e.RenderPage(0, pw, ph);
        CHECK(b2 && Near(PixelAt(b2, 375, 425), 0, 200, 0));
        if (b2) DeleteObject(b2);
        DeleteFileW(out.c_str());
    }

    DeleteFileW(src.c_str());
    if (g_fail) { std::printf("%d Pruefung(en) fehlgeschlagen\n", g_fail); return 1; }
    std::printf("PDF-Bearbeitung: alle Pruefungen bestanden\n");
    return 0;
}
