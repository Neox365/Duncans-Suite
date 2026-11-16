#include "Screenshot.h"

#include <objidl.h>
#include <gdiplus.h>
#include <shlobj.h>

namespace shot {

std::wstring Folder()
{
    wchar_t p[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_MYVIDEO, nullptr, SHGFP_TYPE_CURRENT, p)) || !p[0]) return L"";
    return std::wstring(p) + L"\\Boost-Clips";
}

static bool PngClsid(CLSID* out)
{
    UINT n = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&n, &size) != Gdiplus::Ok || size == 0) return false;
    std::string buf(size, '\0');
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    if (Gdiplus::GetImageEncoders(n, size, info) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < n; ++i)
        if (info[i].MimeType && wcscmp(info[i].MimeType, L"image/png") == 0) { *out = info[i].Clsid; return true; }
    return false;
}

bool Save(HMONITOR mon, const std::wstring& dir, std::wstring& file, std::wstring& err)
{
    if (dir.empty()) { err = L"Der Videos-Ordner wurde nicht gefunden."; return false; }
    MONITORINFO mi{ sizeof(mi) };
    if (!mon || !GetMonitorInfoW(mon, &mi)) { err = L"Der Bildschirm wurde nicht gefunden."; return false; }
    const int w = mi.rcMonitor.right - mi.rcMonitor.left, h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    if (w <= 0 || h <= 0) { err = L"Der Bildschirm hat keine Größe."; return false; }
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    const BOOL copied = BitBlt(mem, 0, 0, w, h, screen, mi.rcMonitor.left, mi.rcMonitor.top, SRCCOPY);   // ohne CAPTUREBLT: Layer-Fenster (Overlay) bleiben draussen
    SelectObject(mem, old);
    bool ok = false;
    if (!copied) err = L"Das Bild konnte nicht kopiert werden.";
    else {
        CLSID png;
        SYSTEMTIME st;
        GetLocalTime(&st);
        const std::wstring path = dir + L"\\" + FileName(st);
        Gdiplus::Bitmap image(bmp, nullptr);
        if (!PngClsid(&png)) err = L"PNG wird nicht unterstützt.";
        else if (image.Save(path.c_str(), &png, nullptr) != Gdiplus::Ok) err = L"Die Datei konnte nicht geschrieben werden.";
        else { file = path; ok = true; }
    }
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}

}  // namespace shot
