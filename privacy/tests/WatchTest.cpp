// Logik-Tests fuer "Schutz erneuern" im Privacy Shield (gewuenschte Einstellungen, Aufgabenbeschreibung; ohne Registry-Aenderungen)
#include "../PrivacyRules.h"

#include <cstdio>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

int main()
{
    using namespace privacy;
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    const std::wstring path = std::wstring(tmp) + L"privwanted" + std::to_wstring(GetCurrentProcessId()) + L".ini";
    DeleteFileW(path.c_str());
    SetWantedPathForTest(path);

    CHECK(!HasWantedFile() && LoadWanted().empty());
    CHECK(SaveWanted({ 3, 1, 25 }));
    CHECK(HasWantedFile());
    std::vector<int> w = LoadWanted();
    CHECK(w.size() == 3 && w[0] == 3 && w[1] == 1 && w[2] == 25);
    MarkWanted(7, true);  CHECK(LoadWanted().size() == 4);
    MarkWanted(7, true);  CHECK(LoadWanted().size() == 4);        // nicht doppelt
    MarkWanted(1, false); w = LoadWanted(); CHECK(w.size() == 3 && w[0] == 3 && w[1] == 25 && w[2] == 7);
    MarkWanted(99, false); CHECK(LoadWanted().size() == 3);        // unbekannte Id entfernen: nichts passiert
    CHECK(SaveWanted({}));  CHECK(LoadWanted().empty());
    {   // beschaedigte Datei: Zahlen werden gelesen, Muell und Dopplungen ignoriert
        WritePrivateProfileStringW(L"Wanted", L"Ids", L"5,abc,5,-2,0,12,", path.c_str());
        w = LoadWanted(); CHECK(w.size() == 2 && w[0] == 5 && w[1] == 12);
    }

    // Fehlende Einstellungen kennt nur, was in der Liste steht; unbekannte Ids werden ignoriert
    SaveWanted({ 99999 });
    CHECK(MissingWanted().empty());
    std::vector<std::wstring> titles, errors;
    CHECK(ReapplyMissing(titles, errors) == 0 && titles.empty() && errors.empty());

    // Aufgabenbeschreibung
    const std::wstring xml = WatchTaskXml(L"C:/ProgramData/DuncansSuite/watch/DuncansPrivacy.exe", L"PC/Du & Ich <1>");
    CHECK(xml.find(L"<Command>C:/ProgramData/DuncansSuite/watch/DuncansPrivacy.exe</Command>") != std::wstring::npos);
    CHECK(xml.find(L"<Arguments>--reapply</Arguments>") != std::wstring::npos);
    CHECK(xml.find(L"<RunLevel>HighestAvailable</RunLevel>") != std::wstring::npos);
    CHECK(xml.find(L"<LogonTrigger>") != std::wstring::npos && xml.find(L"<Delay>PT30S</Delay>") != std::wstring::npos);
    CHECK(xml.find(L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>") != std::wstring::npos);
    CHECK(xml.find(L"PC/Du &amp; Ich &lt;1&gt;") != std::wstring::npos && xml.find(L"Du & Ich") == std::wstring::npos);   // Sonderzeichen entschaerft
    CHECK(xml.compare(0, 5, L"<?xml") == 0);
    CHECK(WatchExePath().empty() || WatchExePath().find(L"DuncansPrivacy.exe") != std::wstring::npos);

    DeleteFileW(path.c_str());
    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
