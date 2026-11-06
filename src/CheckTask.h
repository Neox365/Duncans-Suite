#pragma once
// CheckTask.h - Geplante Aufgabe "woechentlich im Hintergrund nach Treiber-Updates suchen" fuer den Driver Updater.
// Die Aufgabe startet eine Kopie des Programms unter %ProgramData%\DuncansSuite\watch (nur fuer Administratoren beschreibbar, damit kein
// normaler Benutzer eine Datei austauschen kann, die mit hoechsten Rechten laeuft) mit --check: scannen, Windows Update und
// Hersteller-Tabelle abfragen und nur bei Treffern eine Benachrichtigung zeigen.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace drvcheck {

const wchar_t* TaskName();
std::wstring CopyPath();                                                           // "" wenn der Datenordner nicht vertrauenswuerdig ist
std::wstring TaskXml(const std::wstring& exePath, const std::wstring& user);      // Aufgabenbeschreibung (XML), wird als UTF-16 geschrieben
bool TaskExists();
bool Enable(const std::wstring& currentExe, std::wstring& error);
bool Disable(std::wstring& error);
void RefreshCopy(const std::wstring& currentExe);                                  // haelt die Kopie auf dem Stand des laufenden Programms (nur wenn eine Kopie existiert)

// --check: Hintergrund-Pruefung ohne Fenster; zeigt bei Updates eine Benachrichtigung (Klick oeffnet das Programm). Rueckgabe = Exitcode.
int RunCheck(HINSTANCE inst);

}  // namespace drvcheck
