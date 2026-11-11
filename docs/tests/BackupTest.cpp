// Test von Sicherung und Steuerberater-Export (docs/Backup.h).  Baut mit MinGW:  g++ -std=c++17 -I.. BackupTest.cpp -o BackupTest.exe
// Mit einem Ordner als Argument werden zusaetzlich ZIP-Dateien dort abgelegt (fuer tools/CheckBackup.py).
#include "../Backup.h"

#include <cstdio>
#include <map>

namespace fs = std::filesystem;
static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

static void Put(const fs::path& p, const std::string& s) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << s; }
static std::string Get(const fs::path& p) { std::ifstream in(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); }

int main(int argc, char** argv)
{
    const fs::path root = fs::temp_directory_path() / "docs_backup_test";
    fs::remove_all(root);
    const fs::path pdf = root / "2026" / "Muster GmbH" / L"Rechnung_RE-2026-001_Müller.pdf";
    Put(pdf, std::string("%PDF-1.7\n") + std::string("\0\xFF\x01", 3) + "rest");
    Put(root / "2025" / "Alt AG" / "Beleg.pdf", "alt");
    Put(root / ".oggys-index.tsv", "index");
    Put(root / ".kunden.tsv", "kunden");
    Put(root / ".rechnungsdaten" / "RE-2026-001.dat", "daten");
    Put(root / "Protokoll.log", "log");
    Put(root / "2026" / "halb.pdf.tmp", "unfertig");
    Put(root / "Sicherung.zip", "alte sicherung");

    const auto files = docs::CollectBackupFiles(root, root / "Sicherung.zip");
    CHECK(files.size() == 6);                                                      // ohne .tmp und ohne die Zieldatei
    CHECK(std::find(files.begin(), files.end(), L".oggys-index.tsv") != files.end() && std::find(files.begin(), files.end(), L".rechnungsdaten\\RE-2026-001.dat") != files.end());
    CHECK(std::find(files.begin(), files.end(), L"2026\\halb.pdf.tmp") == files.end() && std::find(files.begin(), files.end(), L"Sicherung.zip") == files.end());

    docs::ZipFiles zf;
    for (const auto& rel : files) zf.push_back({docs::ZipName(rel), Get(root / rel)});
    zf.push_back({"LIESMICH.txt", docs::kBackupReadme});
    const std::string z = zip::Write(zf);
    std::vector<zip::Entry> entries;
    CHECK(zip::ReadDirectory(z, entries) && entries.size() == 7);
    std::string back;
    CHECK(zip::ReadEntry(z, entries, "2026/Muster GmbH/Rechnung_RE-2026-001_M\xC3\xBC" "ller.pdf", back) && back == Get(pdf));   // binaer, Umlaut im Namen
    CHECK(zip::ReadEntry(z, entries, ".kunden.tsv", back) && back == "kunden");

    // Steuerberater-Export
    std::vector<docs::Doc> all;
    auto mk = [&](const wchar_t* rel, const wchar_t* year, const wchar_t* cred, long long cents) {
        docs::Doc d; d.rel = rel; d.year = year; d.creditor = cred; d.name = fs::path(rel).filename().wstring(); d.m.cents = cents; d.m.income = true; return d; };
    all.push_back(mk(L"2026\\Muster GmbH\\Rechnung_RE-2026-001_Müller.pdf", L"2026", L"Muster GmbH", 25939));
    all.push_back(mk(L"2025\\Alt AG\\Beleg.pdf", L"2025", L"Alt AG", 1000));
    all.push_back(mk(L"2026\\Weg\\fehlt.pdf", L"2026", L"Weg", 500));
    int missing = 0;
    const auto tax = docs::BuildTaxExport(all, L"2026", [&](const std::wstring& rel, std::string& out) {
        const fs::path p = root / rel;
        if (!fs::exists(p)) return false;
        out = Get(p);
        return true; }, missing);
    CHECK(missing == 1);                                                           // fehlt.pdf nicht lesbar
    std::map<std::string, std::string> m(tax.begin(), tax.end());
    CHECK(m.count("Belegliste_2026.csv") && m.count("Protokoll.log") && m.count("LIESMICH.txt"));
    CHECK(m.count("Belege/2026/Muster GmbH/Rechnung_RE-2026-001_M\xC3\xBC" "ller.pdf") && !m.count("Belege/2025/Alt AG/Beleg.pdf") && !m.count("Belege/2026/Weg/fehlt.pdf"));
    const std::string& csv = m["Belegliste_2026.csv"];
    CHECK(csv.compare(0, 3, "\xEF\xBB\xBF") == 0 && csv.find("259,39") != std::string::npos && csv.find("Beleg.pdf") == std::string::npos && csv.find("fehlt.pdf") != std::string::npos);   // Liste enthaelt auch den nicht lesbaren Beleg
    CHECK(docs::BuildTaxExport(all, L"2024", [](const std::wstring&, std::string&) { return false; }, missing).size() == 2 && missing == 0);   // leeres Jahr: Liste und Erklaerung

    if (argc > 1) {
        std::ofstream(fs::path(argv[1]) / "sicherung.zip", std::ios::binary) << z;
        docs::ZipFiles tz(tax.begin(), tax.end());
        std::ofstream(fs::path(argv[1]) / "steuer.zip", std::ios::binary) << zip::Write(tz);
    }
    fs::remove_all(root);
    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
