#pragma once
// DriverHistory.h - Verlauf der ueber den Driver Updater installierten Treiber-Updates (Textdatei, neueste zuerst anzeigbar)
// und kleine Rechenhelfer fuer die Uebersicht. Keine Oberflaeche.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace drvhistory {

struct Entry {
    std::wstring when;          // "TT.MM.JJJJ HH:MM"
    std::wstring device;        // Geraetename
    std::wstring title;         // Titel des Updates (Windows Update)
    std::wstring oldVersion;    // Treiberversion vor dem Update
    std::wstring newDate;       // Datum des neuen Treibers (TT.MM.JJJJ), kann leer sein
    bool ok = false;            // wirklich installiert
    bool restorePoint = false;  // vorher wurde ein Wiederherstellungspunkt erstellt
};

constexpr size_t kMaxEntries = 200;

// %LOCALAPPDATA%\DuncansDriverUpdater\history.txt (legt den Ordner an); leer, wenn nicht ermittelbar
std::wstring FilePath();

// Alle Eintraege, aeltester zuerst (Reihenfolge der Datei)
std::vector<Entry> Load(const std::wstring& path);
// Haengt Eintraege an; behaelt hoechstens kMaxEntries (die neuesten)
bool Append(const std::wstring& path, const std::vector<Entry>& entries);
// Aktuelle Zeit als "TT.MM.JJJJ HH:MM"
std::wstring NowText();

// Datum JJJJMMTT: ist es mehr als `years` Jahre vor `todayKey` (JJJJMMTT)? Unbekannt (0) zaehlt nie als alt.
bool IsOlderThan(int dateKey, int todayKey, int years);
int TodayKey();

}  // namespace drvhistory
