#include "VendorReleases.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <wininet.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <thread>

#include "../suite/public_key.inc"   // kSuitePublicKey: dieselbe Signatur wie bei den Suite-Downloads
#include "../common/VersionUtil.h"
#include "../common/Crypto.h"

#ifdef _MSC_VER
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "bcrypt.lib")
#endif

namespace
{
    // Stand der eingebauten Tabelle: 18.10.2026
    //   NVIDIA 617.42 WHQL (06.10.2026)  -> Windows-Version 32.0.16.1742  (NVIDIA: 617.42 = "6" + "1742")
    //   Intel  32.0.101.9150 WHQL (12.10.2026)
    //   AMD:   bewusst ohne Version - die Windows-Treiberversion (32.0.xxxxx.xxxx) zur Adrenalin-
    //          Version ist nicht verifiziert. Eintrag in vendor_releases.json ergaenzen.
    std::vector<VendorRelease> BuiltIn()
    {
        std::vector<VendorRelease> v;
        v.push_back({ L"NVIDIA", L"VEN_10DE", L"GeForce Game Ready Treiber 617.42", L"32.0.16.1742", L"06.10.2026", 20261006,
                      L"https://www.nvidia.com/en-us/geforce/drivers/" });
        v.push_back({ L"AMD", L"VEN_1002", L"Radeon Software Adrenalin Edition", L"", L"", 0,
                      L"https://www.amd.com/en/support/download/drivers.html" });
        v.push_back({ L"Intel", L"VEN_8086", L"Intel Arc & Core Ultra Grafiktreiber 32.0.101.9150", L"32.0.101.9150", L"12.10.2026", 20261012,
                      L"https://www.intel.com/content/www/us/en/download-center/home.html" });
        return v;
    }

    // ---- Minimaler, toleranter JSON-Leser fuer { "releases": [ { "key": "value", ... }, ... ] } ----

    std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty())
            return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring out(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
        return out;
    }

    // Liest alle "key": "value"-Paare innerhalb eines {...}-Blocks.
    std::vector<std::pair<std::string, std::string>> ReadPairs(const std::string& block)
    {
        std::vector<std::pair<std::string, std::string>> pairs;
        size_t pos = 0;
        auto readString = [&](std::string& out) -> bool
        {
            if (pos >= block.size() || block[pos] != '"')
                return false;
            ++pos;
            out.clear();
            while (pos < block.size() && block[pos] != '"')
            {
                if (block[pos] == '\\' && pos + 1 < block.size())
                    ++pos;
                out += block[pos++];
            }
            if (pos >= block.size())
                return false;
            ++pos;
            return true;
        };
        while (pos < block.size())
        {
            if (block[pos] != '"')
            {
                ++pos;
                continue;
            }
            std::string key, value;
            if (!readString(key))
                break;
            while (pos < block.size() && (block[pos] == ' ' || block[pos] == '\t' || block[pos] == '\r' || block[pos] == '\n'))
                ++pos;
            if (pos >= block.size() || block[pos] != ':')
                continue;
            ++pos;
            while (pos < block.size() && (block[pos] == ' ' || block[pos] == '\t' || block[pos] == '\r' || block[pos] == '\n'))
                ++pos;
            if (readString(value))
                pairs.emplace_back(key, value);
        }
        return pairs;
    }

    int DateKeyFromIso(const std::string& iso, std::wstring& text)
    {
        int y = 0, m = 0, d = 0;
        if (sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || y < 1990 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31)
            return 0;
        wchar_t buf[16];
        wsprintfW(buf, L"%02d.%02d.%04d", d, m, y);
        text = buf;
        return y * 10000 + m * 100 + d;
    }

    // ---- Online-Tabelle: signierte Kopie von GitHub, lokal zwischengespeichert ----------------
    const wchar_t* kOnlineJson = L"https://raw.githubusercontent.com/Neox365/Duncans-Suite/main/resources/vendor_releases.json";
    const wchar_t* kOnlineSig  = L"https://raw.githubusercontent.com/Neox365/Duncans-Suite/main/resources/vendor_releases.json.sig";

    std::wstring CacheDir()
    {
        wchar_t buf[MAX_PATH * 2] = {};
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH * 2);
        return n > 0 && n < MAX_PATH * 2 ? std::wstring(buf) + L"\\DuncansDriverUpdater" : std::wstring();
    }

    bool ReadWholeFile(const std::wstring& file, std::string& data, size_t maxBytes)
    {
        HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        data.clear();
        char buffer[4096];
        DWORD read = 0;
        while (data.size() < maxBytes && ReadFile(h, buffer, sizeof(buffer), &read, nullptr) && read > 0)
            data.append(buffer, read);
        CloseHandle(h);
        return true;
    }

    bool WriteWholeFile(const std::wstring& file, const std::string& data)
    {
        const std::wstring tmp = file + L".tmp";
        HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        const bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
        CloseHandle(h);
        if (!ok || !MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(tmp.c_str());
            return false;
        }
        return true;
    }

    // ECDSA-P256-Signatur (64 Byte) ueber SHA-256 der Daten, geprueft gegen den Suite-Schluessel.
    bool VerifySignature(const std::string& data, const std::string& sig)
    {
        return crypto::VerifyData(data, sig, kSuitePublicKey, sizeof(kSuitePublicKey));
    }

    bool DownloadHttps(HINTERNET session, const wchar_t* url, std::string& out, size_t maxBytes)
    {
        out.clear();
        HINTERNET h = InternetOpenUrlW(session, url, nullptr, 0,
                                       INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_UI, 0);
        if (!h)
            return false;
        DWORD status = 0, len = sizeof(status);
        bool ok = HttpQueryInfoW(h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &len, nullptr) && status == 200;
        char buf[4096];
        DWORD n = 0;
        while (ok && InternetReadFile(h, buf, sizeof(buf), &n) && n > 0)
        {
            if (out.size() + n > maxBytes) { ok = false; break; }   // zu gross: verwerfen
            out.append(buf, n);
        }
        InternetCloseHandle(h);
        return ok && !out.empty();
    }

    // Ein {...}-Block pro Eintrag; Datum entscheidet, ob ein Eintrag einen vorhandenen ersetzt:
    // gleicher oder neuerer Stand ersetzt, aelterer nicht (so verdraengt eine veraltete mitgelieferte Datei die Online-Tabelle nie).
    void ApplyJson(std::string data, std::vector<VendorRelease>& releases)
    {
        if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF)
            data.erase(0, 3);   // UTF-8-BOM

        // jeden {...}-Block einzeln lesen
        size_t pos = 0;
        while ((pos = data.find('{', pos)) != std::string::npos)
        {
            const size_t end = data.find('}', pos);
            if (end == std::string::npos)
                break;
            const std::string block = data.substr(pos + 1, end - pos - 1);
            pos = end + 1;

            VendorRelease r;
            std::string iso;
            for (const auto& kv : ReadPairs(block))
            {
                if (kv.first == "vendor")       r.vendor = Utf8ToWide(kv.second);
                else if (kv.first == "ven")     r.venId = Utf8ToWide(kv.second);
                else if (kv.first == "name")    r.name = Utf8ToWide(kv.second);
                else if (kv.first == "version") r.version = Utf8ToWide(kv.second);
                else if (kv.first == "date")    iso = kv.second;
                else if (kv.first == "url")     r.url = Utf8ToWide(kv.second);
            }
            if (r.venId.empty())
                continue;
            CharUpperBuffW(&r.venId[0], static_cast<DWORD>(r.venId.size()));
            r.dateKey = DateKeyFromIso(iso, r.dateText);

            // gleicher Hersteller -> ersetzt den bisherigen Eintrag nur bei gleichem oder neuerem Datum, sonst neu
            auto it = std::find_if(releases.begin(), releases.end(), [&](const VendorRelease& e) { return e.venId == r.venId; });
            if (it != releases.end())
            {
                if (r.dateKey < it->dateKey)
                    continue;
                if (r.vendor.empty())
                    r.vendor = it->vendor;
                if (r.url.empty())
                    r.url = it->url;
                *it = r;
            }
            else
                releases.push_back(r);
        }
    }

    // Zwischengespeicherte Online-Tabelle: wird bei jedem Laden erneut gegen die Signatur geprueft.
    void LoadOnlineCache(std::vector<VendorRelease>& releases)
    {
        const std::wstring dir = CacheDir();
        if (dir.empty())
            return;
        std::string json, sig;
        if (!ReadWholeFile(dir + L"\\vendor_releases_online.json", json, 1u << 17) ||
            !ReadWholeFile(dir + L"\\vendor_releases_online.json.sig", sig, 64) || !VerifySignature(json, sig))
            return;
        ApplyJson(json, releases);
    }

    // Tabelle neben der EXE (vom Installer mitgeliefert oder von Hand gepflegt)
    void LoadOverrides(std::vector<VendorRelease>& releases)
    {
        wchar_t path[MAX_PATH * 2] = {};
        if (GetModuleFileNameW(nullptr, path, MAX_PATH * 2) == 0)
            return;
        std::wstring file = path;
        const size_t slash = file.find_last_of(L"\\/");
        file = (slash == std::wstring::npos ? L"" : file.substr(0, slash + 1)) + L"vendor_releases.json";
        std::string data;
        if (ReadWholeFile(file, data, 1u << 20))   // max. 1 MB
            ApplyJson(data, releases);
    }
}

std::vector<VendorRelease> LoadVendorReleases()
{
    // Reihenfolge: eingebaut -> Online-Tabelle (Cache) -> Datei neben der EXE; jeweils gewinnt der neuere Stand
    std::vector<VendorRelease> releases = BuiltIn();
    LoadOnlineCache(releases);
    LoadOverrides(releases);
    return releases;
}

void RefreshVendorReleasesAsync()
{
    std::thread([]()
    {
        const std::wstring dir = CacheDir();
        if (dir.empty())
            return;
        HINTERNET session = InternetOpenW(L"DuncansDriverUpdater", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (!session)
            return;
        DWORD timeout = 8000;
        InternetSetOptionW(session, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        std::string json, sig;
        // Nur eine Tabelle mit gueltiger Signatur wird gespeichert; Fehler (offline, GitHub nicht erreichbar) bleiben still
        if (DownloadHttps(session, kOnlineJson, json, 1u << 17) && DownloadHttps(session, kOnlineSig, sig, 64) && VerifySignature(json, sig))
        {
            CreateDirectoryW(dir.c_str(), nullptr);
            if (WriteWholeFile(dir + L"\\vendor_releases_online.json", json))
                WriteWholeFile(dir + L"\\vendor_releases_online.json.sig", sig);
        }
        InternetCloseHandle(session);
    }).detach();
}

int ApplyVendorReleases(std::vector<DriverInfo>& drivers, const std::vector<VendorRelease>& releases)
{
    int marked = 0;
    for (auto& d : drivers)
    {
        if (d.icon != CategoryIcon::Gpu || d.status != DriverStatus::Ok || !d.updateId.empty())
            continue;

        // Hersteller ueber die PCI-ID (PCI\VEN_10DE&DEV_...) bestimmen
        const VendorRelease* release = nullptr;
        for (const auto& id : d.hardwareIds)
        {
            if (id.rfind(L"PCI\\", 0) != 0)
                continue;
            for (const auto& r : releases)
                if (!r.version.empty() && id.find(r.venId) != std::wstring::npos)
                {
                    release = &r;
                    break;
                }
            if (release)
                break;
        }
        if (!release)
            continue;

        const versionutil::Version installed = versionutil::Parse(d.driverVersion), latest = versionutil::Parse(release->version);
        bool outdated = false;
        if (installed.valid && latest.valid)
        {
            // Aeltere GPU-Generationen haben eigene Treiber-Zweige (z. B. Intel 31.0.x statt 32.0.x):
            // nur innerhalb desselben Hauptzweigs vergleichen, sonst kein falscher Alarm.
            if (installed.part[0] != latest.part[0])
                continue;
            outdated = versionutil::Compare(installed, latest) < 0;
        }
        else if (d.driverDateKey != 0 && release->dateKey != 0)
            outdated = d.driverDateKey < release->dateKey;   // Fallback: Datum

        if (!outdated)
            continue;

        d.status = DriverStatus::UpdateAvailable;
        d.updateTitle = release->name;
        d.updateDate = release->dateText;
        d.updateUrl = release->url;
        ++marked;
    }
    return marked;
}
