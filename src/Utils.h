#pragma once
// Utils.h - Kleine Windows-Hilfsfunktionen.

#include <string>

// true, wenn der Prozess mit Administrator-Rechten laeuft (erhoehtes Token).
bool IsRunningAsAdmin();

// Aktuelle lokale Uhrzeit als "TT.MM.JJJJ HH:MM".
std::wstring CurrentTimeText();
