// main.cpp - Einstiegspunkt von "Duncan's Driver Updater".

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cwchar>

#include "MainWindow.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdLine, int showCommand)
{
    // Die App laeuft als Administrator: nachgeladene DLLs nur aus System32, nie aus dem (evtl. beschreibbaren) Programmordner
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    MainWindow window;
    const bool autoInstall = cmdLine && wcsstr(cmdLine, L"/install") != nullptr;
    if (!window.Create(instance, showCommand, autoInstall))
    {
        MessageBoxW(nullptr, L"Das Hauptfenster konnte nicht erstellt werden.", L"Duncan's Driver Updater",
                    MB_ICONERROR | MB_OK);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
