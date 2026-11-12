#pragma once
// Diagnose-Text fuer den Support: Programm, Version, Windows, Sprache, Skalierung. Enthaelt bewusst KEINE Pfade, Namen, Dateien oder Einstellungen
// des Nutzers; er ist nur zum Weitergeben gedacht ("Diagnose kopieren" in der Ueber-Ansicht) und wird nirgends automatisch gesendet.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstring>
#include <string>

namespace diag {

// Windows-Version ueber RtlGetVersion (GetVersionEx luegt ohne Manifest); leer, wenn nicht ermittelbar
inline std::wstring WindowsVersion()
{
    using Fn = LONG(WINAPI*)(OSVERSIONINFOW*);
    const HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    const Fn fn = nt ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(nt, "RtlGetVersion"))) : nullptr;
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    if (!fn || fn(&v) != 0) return L"";
    return std::to_wstring(v.dwMajorVersion) + L"." + std::to_wstring(v.dwMinorVersion) + L" (Build " + std::to_wstring(v.dwBuildNumber) + L")";
}

inline std::wstring UserLanguage()
{
    wchar_t buf[LOCALE_NAME_MAX_LENGTH] = {};
    return GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH) > 0 ? std::wstring(buf) : std::wstring();
}

// dpi: Skalierung des Fensters (96 = 100 %)
inline std::wstring Text(const std::wstring& program, const std::wstring& version, unsigned dpi)
{
    const std::wstring win = WindowsVersion(), lang = UserLanguage();
#if defined(_WIN64)
    const wchar_t* bits = L"64 Bit";
#else
    const wchar_t* bits = L"32 Bit";
#endif
    std::wstring t = L"Diagnose (enthält keine persönlichen Daten)\r\n";
    t += L"Programm: " + program + L"\r\n";
    t += L"Version: " + version + L"\r\n";
    t += L"Windows: " + (win.empty() ? std::wstring(L"unbekannt") : win) + L", " + bits + L"\r\n";
    t += L"Sprache: " + (lang.empty() ? std::wstring(L"unbekannt") : lang) + L"\r\n";
    t += L"Skalierung: " + std::to_wstring(dpi ? dpi * 100 / 96 : 100) + L" %\r\n";
    return t;
}

// Text in die Zwischenablage (Unicode); false bei Fehler
inline bool CopyToClipboard(HWND owner, const std::wstring& text)
{
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (mem) {
        void* p = GlobalLock(mem);
        if (p) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(mem);
            ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        }
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

}  // namespace diag
