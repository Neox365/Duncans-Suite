#include "VendorLinks.h"

#include <cwctype>

namespace {

std::wstring Upper(std::wstring s)
{
    for (wchar_t& c : s) c = static_cast<wchar_t>(std::towupper(c));
    return s;
}

// "PCI\VEN_8086&DEV_..." / "USB\VID_..." -> "VEN_8086"
std::wstring VenOf(const DriverInfo& d)
{
    std::vector<std::wstring> ids = d.hardwareIds;
    if (!d.hardwareId.empty()) ids.push_back(d.hardwareId);
    for (const std::wstring& id : ids)
    {
        const std::wstring u = Upper(id);
        const size_t p = u.find(L"VEN_");
        if (p != std::wstring::npos && p + 8 <= u.size() && (p == 0 || u[p - 1] == L'\\' || u[p - 1] == L'&'))
            return u.substr(p, 8);
    }
    return std::wstring();
}

}  // namespace

std::wstring VendorPageForVen(const std::wstring& venId)
{
    const std::wstring v = Upper(venId);
    if (v == L"VEN_8086") return L"https://www.intel.com/content/www/us/en/download-center/home.html";
    if (v == L"VEN_10DE") return L"https://www.nvidia.com/en-us/drivers/";
    if (v == L"VEN_1002" || v == L"VEN_1022") return L"https://www.amd.com/en/support/download/drivers.html";
    if (v == L"VEN_10EC") return L"https://www.realtek.com/Download";
    if (v == L"VEN_14E4") return L"https://www.broadcom.com/support/download-search";
    return std::wstring();
}

std::wstring UrlEncode(const std::wstring& text)
{
    std::string utf8;
    for (size_t i = 0; i < text.size(); ++i)
    {
        unsigned long cp = text[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
        {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<unsigned long>(text[i + 1]) - 0xDC00);
            ++i;
        }
        if (cp < 0x80) utf8 += static_cast<char>(cp);
        else if (cp < 0x800) { utf8 += static_cast<char>(0xC0 | (cp >> 6)); utf8 += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { utf8 += static_cast<char>(0xE0 | (cp >> 12)); utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); utf8 += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { utf8 += static_cast<char>(0xF0 | (cp >> 18)); utf8 += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); utf8 += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    static const char* hex = "0123456789ABCDEF";
    std::wstring out;
    for (unsigned char c : utf8)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<wchar_t>(c);
        else if (c == ' ') out += L'+';
        else { out += L'%'; out += static_cast<wchar_t>(hex[c >> 4]); out += static_cast<wchar_t>(hex[c & 15]); }
    }
    return out;
}

std::wstring VendorSupportUrl(const DriverInfo& d)
{
    const std::wstring page = VendorPageForVen(VenOf(d));
    if (!page.empty()) return page;
    std::wstring q = d.manufacturer.empty() ? d.provider : d.manufacturer;
    if (!q.empty()) q += L" ";
    q += d.deviceName + L" Treiber";
    return L"https://duckduckgo.com/?q=" + UrlEncode(q);
}
