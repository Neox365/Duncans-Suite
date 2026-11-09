#include "DriverHistory.h"

#include <shlobj.h>

#include <algorithm>

namespace drvhistory {

namespace {

std::wstring Clean(std::wstring s)
{
    for (wchar_t& c : s)
        if (c == L'\t' || c == L'\r' || c == L'\n')
            c = L' ';
    return s;
}

std::string ToUtf8(const std::wstring& w)
{
    if (w.empty())
        return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

bool ReadAll(const std::wstring& path, std::string& out)
{
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    char buf[8192];
    DWORD n = 0;
    while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n)
        out.append(buf, n);
    CloseHandle(h);
    return true;
}

bool WriteAll(const std::wstring& path, const std::string& data)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
    CloseHandle(h);
    return ok;
}

std::vector<std::wstring> Split(const std::wstring& line)
{
    std::vector<std::wstring> parts;
    size_t pos = 0;
    for (;;)
    {
        const size_t t = line.find(L'\t', pos);
        if (t == std::wstring::npos)
        {
            parts.push_back(line.substr(pos));
            break;
        }
        parts.push_back(line.substr(pos, t - pos));
        pos = t + 1;
    }
    return parts;
}

}  // namespace

std::wstring FilePath()
{
    wchar_t base[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, base)))
        return L"";
    const std::wstring dir = std::wstring(base) + L"\\DuncansDriverUpdater";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\history.txt";
}

std::vector<Entry> Load(const std::wstring& path)
{
    std::vector<Entry> list;
    std::string data;
    if (!ReadAll(path, data))
        return list;
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF && static_cast<unsigned char>(data[1]) == 0xBB)
        data.erase(0, 3);
    const std::wstring text = FromUtf8(data);
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t e = text.find(L'\n', pos);
        if (e == std::wstring::npos)
            e = text.size();
        std::wstring line = text.substr(pos, e - pos);
        pos = e + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' '))
            line.pop_back();
        if (line.empty())
            continue;
        const std::vector<std::wstring> f = Split(line);
        if (f.size() < 7)
            continue;       // beschaedigte Zeile ueberspringen
        Entry en;
        en.when = f[0]; en.device = f[1]; en.title = f[2]; en.oldVersion = f[3]; en.newDate = f[4];
        en.ok = f[5] == L"1";
        en.restorePoint = f[6] == L"1";
        if (f.size() >= 8)
            en.backupDir = f[7];            // aeltere Eintraege haben nur sieben Felder
        list.push_back(std::move(en));
    }
    return list;
}

bool Append(const std::wstring& path, const std::vector<Entry>& entries)
{
    if (path.empty() || entries.empty())
        return false;
    std::vector<Entry> all = Load(path);
    all.insert(all.end(), entries.begin(), entries.end());
    if (all.size() > kMaxEntries)
        all.erase(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(all.size() - kMaxEntries));
    std::wstring text;
    for (const Entry& e : all)
        text += Clean(e.when) + L'\t' + Clean(e.device) + L'\t' + Clean(e.title) + L'\t' + Clean(e.oldVersion) + L'\t' +
                Clean(e.newDate) + L'\t' + (e.ok ? L"1" : L"0") + L'\t' + (e.restorePoint ? L"1" : L"0") + L'\t' + Clean(e.backupDir) + L"\r\n";
    return WriteAll(path, ToUtf8(text));
}

bool IsOemInf(const std::wstring& infName)
{
    if (infName.size() <= 7)
        return false;
    std::wstring inf = infName;
    for (wchar_t& c : inf)
        c = static_cast<wchar_t>(towlower(c));
    return inf.rfind(L"oem", 0) == 0 && inf.compare(inf.size() - 4, 4, L".inf") == 0 &&
           std::all_of(inf.begin() + 3, inf.end() - 4, [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

std::wstring SafeFolderName(const std::wstring& name)
{
    std::wstring out;
    for (wchar_t c : name)
        out += (c < 32 || wcschr(L"<>:\"/\\|?*", c)) ? L'_' : c;
    while (!out.empty() && (out.back() == L' ' || out.back() == L'.'))
        out.pop_back();
    if (out.size() > 60)
        out.resize(60);
    return out.empty() ? std::wstring(L"Treiber") : out;
}

std::wstring NowText()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[40];
    swprintf(buf, 40, L"%02d.%02d.%04d %02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
    return buf;
}

int TodayKey()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    return st.wYear * 10000 + st.wMonth * 100 + st.wDay;
}

bool IsOlderThan(int dateKey, int todayKey, int years)
{
    if (dateKey <= 0)
        return false;
    return dateKey < todayKey - years * 10000;
}

}  // namespace drvhistory
