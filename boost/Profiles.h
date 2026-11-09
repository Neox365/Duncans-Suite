#pragma once
// Duncan's Boost - Profile (Arbeitsplatz-Sets): Ein Profil ist eine Liste von Programmen (z. B. "Arbeit": Browser, PDF-Programm, Office).
// Beim Wechsel werden Programme anderer Profile, die das neue nicht braucht, hoeflich beendet (nie hart) und die des neuen Profils gestartet.
// Fensteranordnung gehoert nicht dazu. Die Planung (PlanSwitch) ist reine Logik und getestet.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace boostmode {

constexpr size_t kMaxProfiles = 12;
constexpr size_t kMaxProfilePrograms = 30;

struct Profile {
    std::wstring name;
    std::vector<std::wstring> paths;      // vollstaendige Pfade der Programme
};

// EXE-Name klein aus einem Pfad ("C:\\x\\Chrome.exe" -> "chrome.exe")
std::wstring ExeOfPath(const std::wstring& path);
// Vollstaendiger Pfad eines laufenden Programms (erster Prozess dieses EXE-Namens); leer = laeuft nicht
std::wstring PathOfRunning(const std::wstring& exeLower);

struct SwitchPlan {
    std::vector<std::wstring> closeExes;      // laufende Programme, die beendet werden
    std::vector<std::wstring> startPaths;     // Programme, die gestartet werden
};
// Was passiert beim Wechsel zu all[target]? runningExes = laufende Programme (klein). Beendet wird nur, was in einem ANDEREN Profil steht,
// im Zielprofil nicht vorkommt, laeuft und nicht gesperrt ist (Systemprozesse, Suite). Gestartet wird, was im Zielprofil steht und nicht laeuft.
SwitchPlan PlanSwitch(const std::vector<Profile>& all, size_t target, const std::vector<std::wstring>& runningExes);

// Namen: leer wird zu "Profil", Laenge gekuerzt, doppelte bekommen " 2", " 3" ...
std::wstring UniqueProfileName(const std::vector<Profile>& all, const std::wstring& wanted);

std::vector<Profile> LoadProfiles();
bool SaveProfiles(const std::vector<Profile>& p);

// Fuehrt den Wechsel aus: erst beenden (hoeflich, graceMs warten, kein hartes Beenden), dann starten. note = Zusammenfassung.
bool RunSwitch(const SwitchPlan& plan, int graceMs, std::wstring& note);

}  // namespace boostmode
