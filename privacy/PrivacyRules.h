#pragma once
// Duncan's Privacy Shield - Einstellungen, Registry-Logik, Sicherung und Wiederherstellung.
// Jede Einstellung besteht aus mehreren Registry-Werten (RegOp). Vor dem ersten Schreiben wird der urspruengliche
// Zustand jedes Wertes (vorhanden + Wert oder "nicht vorhanden") in einer Sicherungsdatei abgelegt; "Rueckgaengig"
// stellt genau diesen Zustand wieder her. Es werden nur die hier aufgefuehrten Werte angefasst.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace privacy {

enum class Hive { Machine, User };       // HKEY_LOCAL_MACHINE bzw. HKEY_CURRENT_USER
enum class ValType { Dword, String };

struct RegOp {
    Hive hive;
    const wchar_t* key;
    const wchar_t* name;
    ValType type;
    DWORD dword;               // Zielwert (Dword)
    const wchar_t* str;        // Zielwert (String)
};

struct Setting {
    int id;
    const wchar_t* group;
    const wchar_t* title;
    const wchar_t* description;
    std::vector<RegOp> ops;
    std::vector<const wchar_t*> stopServices;   // Dienste, die nach dem Anwenden beendet werden (Start-Typ steht in ops)
};

const std::vector<Setting>& AllSettings();

// Sicherungsdatei (Standard: %LOCALAPPDATA%\DuncansPrivacyShield\backup.ini); fuer Tests ueberschreibbar.
std::wstring BackupPath();

bool IsApplied(const Setting& s);                           // alle Werte stehen auf dem Zielwert
bool Apply(const Setting& s, std::wstring& error);          // sichert die Originalwerte, schreibt die Zielwerte
bool Revert(const Setting& s, std::wstring& error);         // stellt die gesicherten Originalwerte wieder her
bool RevertAll(int& restored, std::wstring& error);         // alles, was in der Sicherung steht
bool HasBackup();

// Wiederherstellungspunkt (Systemschutz). Kann Sekunden dauern, daher nicht im UI-Thread aufrufen.
// Rueckgabe false: error enthaelt den Grund (z. B. Systemschutz deaktiviert).
bool CreateRestorePoint(const std::wstring& description, std::wstring& error);

}  // namespace privacy
