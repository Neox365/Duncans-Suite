#pragma once
// Duncan's Photo Commander - Bilder in die Zwischenablage kopieren (Strg+C).
// Dateien kommen als Dateiliste (CF_HDROP, "Kopieren"): Einfuegen im Explorer, in Chat-Programmen, Mail usw. liefert die Originaldateien.
// Bei genau einem Bild liegt zusaetzlich das Bild selbst als Bitmap (CF_DIB) bereit, damit es auch in Paint, Word oder Bildbearbeitung einfuegbar ist.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>

#include <cstring>
#include <string>
#include <vector>

namespace photoclip {

// Nutzdaten der Dateiliste (DROPFILES + doppelt nullterminierte Pfadliste)
inline std::vector<BYTE> BuildDropFiles(const std::vector<std::wstring>& paths) {
    size_t chars = 1;                                       // abschliessende zweite Null
    for (const auto& p : paths) chars += p.size() + 1;
    std::vector<BYTE> buf(sizeof(DROPFILES) + chars * sizeof(wchar_t), 0);
    auto* df = reinterpret_cast<DROPFILES*>(buf.data());
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    wchar_t* w = reinterpret_cast<wchar_t*>(buf.data() + sizeof(DROPFILES));
    for (const auto& p : paths) {
        std::memcpy(w, p.c_str(), p.size() * sizeof(wchar_t));
        w += p.size() + 1;
    }
    return buf;
}

// BGRA (von oben nach unten) -> Nutzdaten fuer CF_DIB: 32 Bit, von unten nach oben, Transparenz auf Weiss gerechnet
inline std::vector<BYTE> BuildDib(const BYTE* bgra, UINT w, UINT h) {
    std::vector<BYTE> buf(sizeof(BITMAPINFOHEADER) + static_cast<size_t>(w) * h * 4);
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = static_cast<LONG>(w);
    bi.biHeight = static_cast<LONG>(h);
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = w * h * 4;
    std::memcpy(buf.data(), &bi, sizeof(bi));
    BYTE* dst = buf.data() + sizeof(bi);
    for (UINT y = 0; y < h; ++y) {
        const BYTE* s = bgra + static_cast<size_t>(h - 1 - y) * w * 4;
        BYTE* d = dst + static_cast<size_t>(y) * w * 4;
        for (UINT x = 0; x < w; ++x, s += 4, d += 4) {
            const unsigned a = s[3];
            for (int c = 0; c < 3; ++c) d[c] = static_cast<BYTE>((s[c] * a + 255 * (255 - a) + 127) / 255);
            d[3] = 0;
        }
    }
    return buf;
}

inline HGLOBAL ToGlobal(const void* data, size_t size) {
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!g) return nullptr;
    void* p = GlobalLock(g);
    if (!p) { GlobalFree(g); return nullptr; }
    std::memcpy(p, data, size);
    GlobalUnlock(g);
    return g;
}

// Legt die Dateien (und, wenn bgra != nullptr, das Bild) in die Zwischenablage. false bei Fehler (Zwischenablage gesperrt o. ae.).
inline bool CopyToClipboard(HWND owner, const std::vector<std::wstring>& files, const BYTE* bgra, UINT w, UINT h) {
    if (files.empty()) return false;
    bool opened = false;
    for (int i = 0; i < 10 && !opened; ++i) { opened = OpenClipboard(owner) != 0; if (!opened) Sleep(30); }
    if (!opened) return false;
    EmptyClipboard();
    bool ok = false;
    const std::vector<BYTE> drop = BuildDropFiles(files);
    if (HGLOBAL g = ToGlobal(drop.data(), drop.size())) {
        if (SetClipboardData(CF_HDROP, g)) ok = true; else GlobalFree(g);
    }
    if (ok) {
        const UINT fmt = RegisterClipboardFormatW(L"Preferred DropEffect");
        const DWORD effect = DROPEFFECT_COPY;
        if (HGLOBAL g = ToGlobal(&effect, sizeof effect)) { if (!SetClipboardData(fmt, g)) GlobalFree(g); }
    }
    if (ok && bgra && w > 0 && h > 0 && static_cast<unsigned long long>(w) * h <= 100000000ull) {
        const std::vector<BYTE> dib = BuildDib(bgra, w, h);
        if (HGLOBAL g = ToGlobal(dib.data(), dib.size())) { if (!SetClipboardData(CF_DIB, g)) GlobalFree(g); }
    }
    CloseClipboard();
    return ok;
}

}  // namespace photoclip
