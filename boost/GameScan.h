#pragma once
// Duncan's Boost - installierte Spiele finden: liest die Steam- und Epic-Games-Bibliotheken (nur lesen, nichts wird veraendert) und ermittelt je
// Spiel die Haupt-EXE, damit man das Spiel fuer die Automatik anhaken kann, ohne es vorher zu starten.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <utility>
#include <vector>

namespace boostmode {

struct InstalledGame {
    std::wstring name;      // Anzeigename, z. B. "Counter-Strike 2"
    std::wstring exe;       // Dateiname der Haupt-EXE, klein geschrieben (so speichert Boost angehakte Spiele)
    std::wstring store;     // "Steam" oder "Epic"
};

// --- reine Hilfen (getrennt testbar) ---
// Bibliotheksordner aus libraryfolders.vdf (die "path"-Eintraege, mit aufgeloesten \\)
std::vector<std::wstring> ParseSteamLibraries(const std::string& vdf);
// Name und Installationsordner aus einer appmanifest_*.acf
bool ParseSteamManifest(const std::string& acf, std::wstring& name, std::wstring& installDir);
// Name, Installationsort und Start-EXE aus einer Epic-Manifest-Datei (.item, JSON)
bool ParseEpicManifest(const std::string& json, std::wstring& name, std::wstring& installLocation, std::wstring& exe);
bool IsHelperExe(const std::wstring& fileNameLower);      // Deinstaller, Crash-Melder, Anti-Cheat-Dienste, Vorbedingungen ...
bool IsToolName(const std::wstring& gameName);            // Steam-Werkzeuge wie "Steamworks Common Redistributables" oder Proton
// Haupt-EXE aus (Dateiname klein, Groesse): die groesste, die kein Helfer ist; gibt es nur Helfer, die groesste insgesamt; leer, wenn keine
std::wstring PickMainExe(const std::vector<std::pair<std::wstring, unsigned long long>>& candidates);

// Alle installierten Steam- und Epic-Spiele, nach Name sortiert, ohne doppelte EXE. Leer, wenn keiner der Launcher installiert ist.
std::vector<InstalledGame> ScanInstalledGames();

}  // namespace boostmode
