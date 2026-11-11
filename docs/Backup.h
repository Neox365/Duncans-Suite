#pragma once
// Datensicherung und Steuerberater-Export als ZIP. Reine Logik (Dateien lesen ueber ein uebergebenes Lesen-Objekt), getrennt testbar.
// Sicherung: der ganze Rechnungsordner einschliesslich versteckter Dateien (Index, Kundenliste, Rechnungsdaten, Protokoll).
// Steuerberater-Export: Belegliste (CSV) eines Jahres, die PDFs dieses Jahres, das Aenderungsprotokoll und eine kurze Erklaerung.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../office/Zip.h"
#include "Export.h"
#include "Store.h"

namespace docs {

using ZipFiles = std::vector<std::pair<std::string, std::string>>;   // Name im ZIP (UTF-8, '/'), Inhalt

inline std::string ZipName(const std::wstring& rel)
{
    std::wstring n = rel;
    std::replace(n.begin(), n.end(), L'\\', L'/');
    return WideToUtf8(n);
}

// Alle Dateien unter root (relativ, mit '\'), ohne unfertige ".tmp"-Dateien und ohne die Zieldatei selbst (liegt die Sicherung im Ordner).
inline std::vector<std::wstring> CollectBackupFiles(const std::filesystem::path& root, const std::filesystem::path& exclude = {})
{
    std::vector<std::wstring> out;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::filesystem::path& p = it->path();
        if (p.extension() == L".tmp") continue;
        if (!exclude.empty() && std::filesystem::equivalent(p, exclude, ec)) continue;
        std::wstring rel = std::filesystem::relative(p, root, ec).wstring();
        std::replace(rel.begin(), rel.end(), L'/', L'\\');
        out.push_back(rel);
    }
    std::sort(out.begin(), out.end());
    return out;
}

inline const char* kBackupReadme =
    "Sicherung der Rechnungen (Oggys Docs-Files)\r\n\r\n"
    "Dieses ZIP enthaelt den kompletten Rechnungsordner. Zum Zurueckspielen alles in einen leeren Ordner entpacken und diesen in\r\n"
    "Docs-Files als Rechnungsordner waehlen (Zahnrad -> Rechnungsordner aendern). Die versteckten Dateien (.oggys-index.tsv, .kunden.tsv,\r\n"
    ".rechnungsdaten) gehoeren dazu: Sie enthalten Status, Betraege, Kundenliste und die Daten fuer Stornos. Protokoll.log ist das Aenderungsprotokoll.\r\n";

// Steuerberater-Export fuer ein Jahr. read(rel) liefert den Dateiinhalt (false, wenn nicht lesbar). missing zaehlt nicht lesbare Dateien.
inline ZipFiles BuildTaxExport(const std::vector<Doc>& all, const std::wstring& year, const std::function<bool(const std::wstring&, std::string&)>& read, int& missing)
{
    ZipFiles out;
    missing = 0;
    std::vector<Doc> rows;
    for (const Doc& d : all)
        if (d.year == year) rows.push_back(d);
    std::sort(rows.begin(), rows.end(), [](const Doc& a, const Doc& b) { return a.rel < b.rel; });
    out.push_back({"Belegliste_" + WideToUtf8(year) + ".csv", BuildCsv(rows)});
    for (const Doc& d : rows) {
        std::string data;
        if (read(d.rel, data)) out.push_back({"Belege/" + ZipName(d.rel), std::move(data)});
        else ++missing;
    }
    std::string log;
    if (read(L"Protokoll.log", log)) out.push_back({"Protokoll.log", std::move(log)});
    out.push_back({"LIESMICH.txt",
                   "Rechnungen " + WideToUtf8(year) + " (Oggys Docs-Files)\r\n\r\n"
                   "Belegliste_" + WideToUtf8(year) + ".csv: eine Zeile je Beleg (Semikolon-getrennt, UTF-8, oeffnet sich direkt in Excel).\r\n"
                   "Belege: die PDFs dieses Jahres, sortiert nach Glaeubiger. Eigene Rechnungen enthalten als Anhang die E-Rechnungsdaten (ZUGFeRD/Factur-X).\r\n"
                   "Protokoll.log: Aenderungsprotokoll der Rechnungen (Erstellen, Storno, Statusaenderungen).\r\n"});
    return out;
}

}  // namespace docs
