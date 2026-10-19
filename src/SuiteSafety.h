#pragma once
// Gemeinsame Sicherheitsfunktionen der Duncan's-Suite-Programme: Datenordner unter %ProgramData%, Logdatei,
// Systemwiederherstellungspunkt und Administrator-Pruefung. Wird vom Privacy Shield und vom Driver Updater genutzt.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace suite_safety {

// %ProgramData%\DuncansSuite (wird bei Bedarf angelegt)
std::wstring DataDir();
// %ProgramData%\DuncansSuite\Backups
std::wstring BackupsDir();
// %ProgramData%\DuncansSuite\duncans_suite.log
std::wstring LogPath();

// Haengt eine Zeile "Zeitstempel [Modul] Text" an duncans_suite.log an (UTF-8; ab 1 MB wird auf .1 gewechselt). Thread-sicher.
void Log(const wchar_t* module, const std::wstring& text);

// Legt den Wiederherstellungspunkt "Duncan's Suite Pre-Optimization" an (srclient.dll / SRSetRestorePointW).
// pointType: 12 = MODIFY_SETTINGS, 10 = DEVICE_DRIVER_INSTALL. Rueckgabe false: note enthaelt den Grund
// (Systemschutz aus, Dienst nicht verfuegbar, ...). Kann Sekunden dauern, nicht im UI-Thread aufrufen.
bool CreateRestorePoint(DWORD pointType, std::wstring& note);

// true, wenn der Prozess mit erhoehten Rechten (Administrator) laeuft.
bool IsElevated();

}  // namespace suite_safety

namespace suite_safety {

// Pfade nur berechnen (nichts anlegen) - fuer Anzeigen in der Oberflaeche
std::wstring BackupsDirPath();
std::wstring LogFilePath();

// Gemeinsame Einstellung der Suite (%LOCALAPPDATA%\DuncansSuite\settings.ini, [Allgemein] AutoWiederherstellungspunkt, Standard: an):
// Privacy Shield und Driver Updater legen vor kritischen Aktionen nur dann einen Wiederherstellungspunkt an, wenn sie an ist.
bool AutoRestorePointEnabled();

}  // namespace suite_safety
