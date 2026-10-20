// Test fuer common/Changelog.h (laeuft auch unter Linux):  g++ -std=c++17 -I../../common ChangelogTest.cpp -o t && ./t
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "../../common/Changelog.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    using namespace changelog;

    const std::string text =
        "\xEF\xBB\xBF# Kommentar\n"
        "[A.exe]\n"
        "## 1.2.0 | 2026-10-20\n"
        "- Erster Punkt\n"
        "- Zweiter Punkt mit \xC3\xA4\xC3\xB6\xC3\xBC\n"
        "  und Folgezeile\n"
        "## 1.1.0 | 2026-10-01\n"
        "- Alt\n"
        "\n"
        "[B.exe]\n"
        "## Hinweis\n"
        "- Ohne Datum\n"
        "- Stichpunkt ohne Eintrag davor\r\n";
    auto s = Parse(text);
    CHECK(s.size() == 2);
    CHECK(s[0].key == L"A.exe" && s[0].entries.size() == 2);
    CHECK(s[0].entries[0].title == L"1.2.0" && s[0].entries[0].date == L"2026-10-20");
    CHECK(s[0].entries[0].items.size() == 2);
    CHECK(s[0].entries[0].items[1] == L"Zweiter Punkt mit äöü und Folgezeile");
    CHECK(s[0].entries[1].title == L"1.1.0" && s[0].entries[1].items.size() == 1);
    CHECK(s[1].key == L"B.exe" && s[1].entries.size() == 1 && s[1].entries[0].title == L"Hinweis" && s[1].entries[0].date.empty());
    CHECK(s[1].entries[0].items.size() == 2 && s[1].entries[0].items[1] == L"Stichpunkt ohne Eintrag davor");
    CHECK(Find(s, L"B.exe") == &s[1] && Find(s, L"C.exe") == nullptr);

    // Muell und Grenzen
    CHECK(Parse("").empty());
    CHECK(Parse("- Punkt ohne alles\n## ohne Abschnitt\n").empty());
    CHECK(Parse(std::string("\xFF\xFE\n[X.exe]\n## 1 | 2\n- \xC3\n")).size() == 1);  // ungueltiges UTF-8 stuerzt nicht ab
    {
        std::string big = "[Gross.exe]\n";
        for (int i = 0; i < 500; ++i) big += "## " + std::to_string(i) + "\n- x\n";
        auto g = Parse(big);
        CHECK(g.size() == 1 && g[0].entries.size() == kMaxEntries);
        std::string many;
        for (int i = 0; i < 40; ++i) many += "[S" + std::to_string(i) + ".exe]\n";
        CHECK(Parse(many).size() == kMaxSections);
        std::string longItem = "[L.exe]\n## 1\n- " + std::string(5000, 'a') + "\n";
        CHECK(Parse(longItem)[0].entries[0].items[0].size() == kMaxItemChars);
        std::string ctrl = "[C.exe]\n## 1\n- a\x01" "b\n";
        CHECK(Parse(ctrl)[0].entries[0].items[0] == L"a b");
    }

    // Die echte changelog.txt muss sich lesen lassen und alle Programme enthalten
    {
        std::ifstream f("../../changelog.txt", std::ios::binary);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            auto real = Parse(ss.str());
            for (const wchar_t* k : {L"DuncansTidy.exe", L"DuncansDriverUpdater.exe", L"DuncansPdf.exe", L"DuncansPrivacy.exe",
                                     L"DuncansPhotoCommander.exe", L"DuncansSuite.exe"}) {
                const Section* sec = Find(real, k);
                CHECK(sec && !sec->entries.empty());
                if (sec)
                    for (const auto& e : sec->entries) CHECK(!e.title.empty() && !e.items.empty());
            }
        } else {
            std::printf("changelog.txt nicht gefunden (Test aus suite/tests starten) - uebersprungen\n");
        }
    }

    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
