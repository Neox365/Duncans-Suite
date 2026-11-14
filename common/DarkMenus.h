#pragma once
// Dunkle Kontextmenues fuer alle TrackPopupMenu-Aufrufe des Programms (undokumentiert: uxtheme-Ordinal 135 = SetPreferredAppMode,
// 136 = FlushMenuThemes). Einmal am Programmstart aufrufen. Fehlen die Funktionen (aeltere Windows-Versionen), bleiben die Menues wie sie sind.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace darkmenus {

inline void Enable() {
    if (HMODULE ux = LoadLibraryW(L"uxtheme.dll")) {
        using SetMode = int(WINAPI*)(int);
        using Flush = void(WINAPI*)();
        if (auto f = reinterpret_cast<SetMode>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))))) f(2 /*ForceDark*/);
        if (auto f = reinterpret_cast<Flush>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))))) f();
    }
}

}  // namespace darkmenus
