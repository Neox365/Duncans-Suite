#pragma once
// Gemeinsamer Versionsvergleich ("1.1.26", "v1.1.26", "32.0.101.9150": bis zu vier Zahlen, fehlende zaehlen als 0).
#include <cstdlib>
#include <cwchar>
#include <string>

namespace versionutil {

struct Version
{
    unsigned long part[4] = { 0, 0, 0, 0 };
    bool valid = false;   // mindestens eine Zahl gelesen
};

inline Version Parse(const std::wstring& text)
{
    Version v;
    const wchar_t* s = text.c_str();
    if (*s == L'v' || *s == L'V')
        ++s;
    size_t index = 0;
    while (*s && index < 4)
    {
        wchar_t* end = nullptr;
        const unsigned long value = wcstoul(s, &end, 10);
        if (end == s)
            break;
        v.part[index++] = value;
        v.valid = true;
        if (*end != L'.')
            break;
        s = end + 1;
    }
    return v;
}

// -1: a < b, 0: gleich, 1: a > b
inline int Compare(const Version& a, const Version& b)
{
    for (int i = 0; i < 4; ++i)
        if (a.part[i] != b.part[i])
            return a.part[i] < b.part[i] ? -1 : 1;
    return 0;
}

inline bool IsNewer(const Version& a, const Version& b) { return Compare(a, b) > 0; }

}  // namespace versionutil
