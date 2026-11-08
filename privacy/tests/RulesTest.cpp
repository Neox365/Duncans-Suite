// Pruefung der Einstellungsliste des Privacy Shields (ohne Registry- oder Aufgaben-Aenderungen): eindeutige Ids,
// zusammenhaengende Gruppen, keine widerspruechlichen Registry-Ziele, wohlgeformte geplante Aufgaben und App-Berechtigungen.
#include "../PrivacyRules.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

int main()
{
    using namespace privacy;
    const auto& all = AllSettings();
    CHECK(all.size() >= 30);

    std::set<int> ids;
    std::vector<std::wstring> groupOrder;
    std::map<std::wstring, std::wstring> target;   // "HKLM\Pfad\Name" -> Zielwert; zwei Einstellungen duerfen sich nicht widersprechen
    for (const Setting& s : all) {
        CHECK(ids.insert(s.id).second);                                          // Id nur einmal
        CHECK(s.group && *s.group && s.title && *s.title && s.description && *s.description);
        CHECK(!s.ops.empty() || !s.services.empty() || !s.tasks.empty());        // jede Einstellung tut etwas
        if (groupOrder.empty() || groupOrder.back() != s.group) {
            for (const auto& g : groupOrder) CHECK(g != s.group);                // Gruppen stehen zusammenhaengend in der Liste
            groupOrder.push_back(s.group);
        }
        for (const RegOp& op : s.ops) {
            const std::wstring id = std::wstring(op.hive == Hive::Machine ? L"HKLM\\" : L"HKCU\\") + op.key + L"\\" + op.name;
            const std::wstring val = op.type == ValType::Dword ? std::to_wstring(op.dword) : std::wstring(L"sz:") + (op.str ? op.str : L"");
            auto it = target.find(id);
            if (it != target.end() && it->second != val) { std::wprintf(L"Widerspruch bei %ls\n", id.c_str()); CHECK(false); }
            target[id] = val;
        }
        for (const TaskOp& t : s.tasks) {
            CHECK(t.folder && t.folder[0] == L'\\' && t.name && *t.name);        // Ordner beginnt mit Backslash
            CHECK(std::wstring(t.folder).find(L"Microsoft") != std::wstring::npos);   // nur Windows-eigene Aufgaben
        }
    }

    // geplante Telemetrie-Aufgaben (Id 31): alle im Aufgabenordner von Windows, ohne doppelte Eintraege
    const Setting* tasks = nullptr;
    const Setting* camera = nullptr;
    const Setting* personal = nullptr;
    for (const Setting& s : all) {
        if (s.id == 31) tasks = &s;
        if (s.id == 32) camera = &s;
        if (s.id == 34) personal = &s;
    }
    CHECK(tasks && tasks->ops.empty() && tasks->tasks.size() >= 8 && !tasks->manualOnly);
    if (tasks) {
        std::set<std::wstring> seen;
        for (const TaskOp& t : tasks->tasks) CHECK(seen.insert(std::wstring(t.folder) + L"\\" + t.name).second);
        const bool applied = IsApplied(*tasks);   // darf auf jedem Rechner laufen (nur Lesen) und nicht abstuerzen
        std::printf("Telemetrie-Aufgaben aktuell abgeschaltet: %s\n", applied ? "ja" : "nein");
    }
    // App-Berechtigungen: nur bewusst einschalten (Nebenwirkungen), Richtlinienwert 2 = nicht zulassen
    CHECK(camera && camera->manualOnly && camera->ops.size() == 2 && camera->ops[0].dword == 2);
    CHECK(personal && personal->manualOnly && personal->ops.size() >= 10);
    for (int id : {32, 33, 34}) {
        for (const Setting& s : all)
            if (s.id == id) {
                CHECK(s.manualOnly);
                for (const RegOp& op : s.ops) CHECK(op.hive == Hive::Machine && (op.type == ValType::Dword ? op.dword == 2 : std::wstring(op.str) == L"Deny"));
            }
    }

    if (g_fail) { std::printf("%d von %d Pruefungen fehlgeschlagen\n", g_fail, g_n); return 1; }
    std::printf("Privacy-Einstellungen: alle %d Pruefungen bestanden\n", g_n);
    return 0;
}
