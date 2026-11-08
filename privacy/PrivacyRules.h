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
    bool optional = false;     // Schreibfehler bricht die Einstellung nicht ab und zaehlt nicht fuer den Schalterstand (z. B. von Windows geschuetzte Werte)
};

// Dienst, dessen Starttyp ueber die Dienstverwaltung (ChangeServiceConfig) geaendert wird; der Originalstarttyp wird gesichert
struct ServiceOp {
    const wchar_t* name;
    DWORD startType;           // SERVICE_DISABLED, SERVICE_DEMAND_START ...
};

// Geplante Aufgabe (Aufgabenplanung), die abgeschaltet wird; ob sie vorher aktiv war, wird gesichert und beim Zuruecknehmen wiederhergestellt
struct TaskOp {
    const wchar_t* folder;     // z. B. L"\\Microsoft\\Windows\\Application Experience"
    const wchar_t* name;
};

struct Setting {
    int id;
    const wchar_t* group;
    const wchar_t* title;
    const wchar_t* description;
    std::vector<RegOp> ops;
    std::vector<const wchar_t*> stopServices;   // Dienste, die nach dem Anwenden beendet werden (Start-Typ steht in ops)
    std::vector<ServiceOp> services;            // Starttyp per Dienstverwaltung; Dienste mit Zieltyp "deaktiviert" werden auch beendet
    bool manualOnly = false;                    // wird von "Alle schuetzen" nicht mit angewendet (Nebenwirkungen: nur bewusst einschalten)
    bool requiresSsd = false;                   // wird abgelehnt, wenn das Systemlaufwerk eine Festplatte ist
    std::vector<TaskOp> tasks;                  // geplante Aufgaben, die deaktiviert werden (nicht vorhandene werden uebersprungen)
};

const std::vector<Setting>& AllSettings();

// Sicherungsdatei (Standard: %LOCALAPPDATA%\DuncansPrivacyShield\backup.ini); fuer Tests ueberschreibbar.
std::wstring BackupPath();

bool IsApplied(const Setting& s);                           // alle Werte stehen auf dem Zielwert
bool Apply(const Setting& s, std::wstring& error);          // sichert die Originalwerte, schreibt die Zielwerte
bool Revert(const Setting& s, std::wstring& error);         // stellt die gesicherten Originalwerte wieder her
bool RevertAll(int& restored, std::wstring& error);         // alles, was in der Sicherung steht
bool HasBackup();

// Ist das Systemlaufwerk eine SSD (keine Suchzeit-Strafe)? known = false, wenn Windows es nicht verraet (dann ist die Rueckgabe false).
bool SystemDriveIsSsd(bool& known);

// Wiederherstellungspunkt (Systemschutz). Kann Sekunden dauern, daher nicht im UI-Thread aufrufen.
// Rueckgabe false: error enthaelt den Grund (z. B. Systemschutz deaktiviert).
bool CreateRestorePoint(const std::wstring& description, std::wstring& error);

// ---- Schutz erneuern: Windows setzt Einstellungen nach grossen Updates manchmal zurueck ----
// Der gewuenschte Schutz (Setting.id) steht in %ProgramData%\DuncansSuite\privacy_wanted.ini (nur Administratoren duerfen schreiben).
bool HasBackupFor(const Setting& s);                       // diese Einstellung wurde vom Programm geaendert (Sicherungseintrag vorhanden)
std::vector<int> LoadWanted();                             // gewuenschte Einstellungen (Setting.id)
bool HasWantedFile();
bool SaveWanted(const std::vector<int>& ids);
void MarkWanted(int settingId, bool on);                   // beim Anwenden/Zuruecknehmen nachfuehren
void SeedWantedFromBackup();                               // einmalig: alle Einstellungen mit Sicherungseintrag uebernehmen
std::vector<int> MissingWanted();                          // gewuenscht, aber nicht mehr angewendet (Setting.id)
int ReapplyMissing(std::vector<std::wstring>& titles, std::vector<std::wstring>& errors);   // wendet fehlende erneut an
void SetWantedPathForTest(const std::wstring& path);

// Geplante Aufgabe "bei jedem Anmelden erneuern": startet eine Kopie des Programms (unter %ProgramData%\DuncansSuite\watch,
// nur fuer Administratoren beschreibbar) mit --reapply, mit hoechsten Rechten, ohne Fenster.
std::wstring WatchExePath();
std::wstring WatchTaskXml(const std::wstring& exePath, const std::wstring& user);   // Aufgabenbeschreibung (XML)
bool WatchTaskExists();
bool EnableWatchTask(const std::wstring& currentExe, std::wstring& error);
bool DisableWatchTask(std::wstring& error);
void RefreshWatchCopy(const std::wstring& currentExe);    // haelt die Kopie auf dem Stand des laufenden Programms

}  // namespace privacy
