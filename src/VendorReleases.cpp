#include "VendorReleases.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>

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

    // "a.b.c.d" -> 4 Zahlen; false, wenn nichts Lesbares drinsteht.
    bool ParseVersion(const std::wstring& text, unsigned long (&parts)[4])
    {
        for (auto& p : parts)
            p = 0;
        const wchar_t* s = text.c_str();
        size_t index = 0;
        bool any = false;
        while (*s && index < 4)
        {
            wchar_t* end = nullptr;
            const unsigned long value = wcstoul(s, &end, 10);
            if (end == s)
                break;
            parts[index++] = value;
            any = true;
            if (*end != L'.')
                break;
            s = end + 1;
        }
        return any;
    }
    // -1: a < b, 0: gleich, 1: a > b
    int CompareVersion(const unsigned long (&a)[4], const unsigned long (&b)[4])
    {
        for (int i = 0; i < 4; ++i)
        {
            if (a[i] != b[i])
                return a[i] < b[i] ? -1 : 1;
        }
        return 0;
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

    void LoadOverrides(std::vector<VendorRelease>& releases)
    {
        wchar_t path[MAX_PATH * 2] = {};
        if (GetModuleFileNameW(nullptr, path, MAX_PATH * 2) == 0)
            return;
        std::wstring file = path;
        const size_t slash = file.find_last_of(L"\\/");
        file = (slash == std::wstring::npos ? L"" : file.substr(0, slash + 1)) + L"vendor_releases.json";

        HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return;
        std::string data;
        char buffer[4096];
        DWORD read = 0;
        while (data.size() < (1u << 20) && ReadFile(h, buffer, sizeof(buffer), &read, nullptr) && read > 0)   // max. 1 MB
            data.append(buffer, read);
        CloseHandle(h);
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

            // gleicher Hersteller -> ersetzt den eingebauten Eintrag, sonst neu
            auto it = std::find_if(releases.begin(), releases.end(), [&](const VendorRelease& e) { return e.venId == r.venId; });
            if (it != releases.end())
            {
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
}

std::vector<VendorRelease> LoadVendorReleases()
{
    std::vector<VendorRelease> releases = BuiltIn();
    LoadOverrides(releases);
    return releases;
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

        unsigned long installed[4], latest[4];
        bool outdated = false;
        if (ParseVersion(d.driverVersion, installed) && ParseVersion(release->version, latest))
        {
            // Aeltere GPU-Generationen haben eigene Treiber-Zweige (z. B. Intel 31.0.x statt 32.0.x):
            // nur innerhalb desselben Hauptzweigs vergleichen, sonst kein falscher Alarm.
            if (installed[0] != latest[0])
                continue;
            outdated = CompareVersion(installed, latest) < 0;
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
