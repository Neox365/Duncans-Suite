#include "Profiles.h"

#include <tlhelp32.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdlib>
#include <set>

#include "BoostLogic.h"

namespace boostmode {

namespace {
bool Contains(const std::vector<std::wstring>& v, const std::wstring& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
std::wstring ProfilesFile() { return DataDir() + L"\\profiles.ini"; }
}  // namespace

std::wstring ExeOfPath(const std::wstring& path) {
    std::wstring n = path;
    const size_t slash = n.find_last_of(L"\\/");
    if (slash != std::wstring::npos) n.erase(0, slash + 1);
    return Lower(n);
}

std::wstring PathOfRunning(const std::wstring& exeLower) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return L"";
    std::wstring found;
    PROCESSENTRY32W pe{ sizeof(pe) };
    for (BOOL ok = Process32FirstW(snap, &pe); ok && found.empty(); ok = Process32NextW(snap, &pe)) {
        if (Lower(pe.szExeFile) != exeLower) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) continue;
        wchar_t buf[MAX_PATH * 2] = {};
        DWORD n = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(h, 0, buf, &n)) found.assign(buf, n);
        CloseHandle(h);
    }
    CloseHandle(snap);
    return found;
}

SwitchPlan PlanSwitch(const std::vector<Profile>& all, size_t target, const std::vector<std::wstring>& running) {
    SwitchPlan plan;
    if (target >= all.size()) return plan;
    std::vector<std::wstring> wanted;
    for (const std::wstring& p : all[target].paths) wanted.push_back(ExeOfPath(p));
    std::set<std::wstring> seenClose;
    for (size_t i = 0; i < all.size(); ++i) {
        if (i == target) continue;
        for (const std::wstring& p : all[i].paths) {
            const std::wstring exe = ExeOfPath(p);
            if (exe.empty() || Contains(wanted, exe) || !Contains(running, exe) || IsProtectedExe(exe)) continue;
            if (seenClose.insert(exe).second) plan.closeExes.push_back(exe);
        }
    }
    std::set<std::wstring> seenStart;
    for (const std::wstring& p : all[target].paths) {
        const std::wstring exe = ExeOfPath(p);
        if (exe.empty() || Contains(running, exe) || IsProtectedExe(exe)) continue;
        if (seenStart.insert(exe).second) plan.startPaths.push_back(p);
    }
    return plan;
}

std::wstring UniqueProfileName(const std::vector<Profile>& all, const std::wstring& wanted) {
    std::wstring base = wanted;
    while (!base.empty() && (base.front() == L' ' || base.front() == L'\t')) base.erase(base.begin());
    while (!base.empty() && (base.back() == L' ' || base.back() == L'\t')) base.pop_back();
    for (wchar_t& c : base) if (c == L'\r' || c == L'\n' || c == L'=' || c == L'|') c = L' ';
    if (base.size() > 30) base.resize(30);
    if (base.empty()) base = L"Profil";
    auto taken = [&](const std::wstring& n) {
        return std::any_of(all.begin(), all.end(), [&](const Profile& p) { return Lower(p.name) == Lower(n); });
    };
    std::wstring name = base;
    for (int i = 2; taken(name) && i < 100; ++i) name = base + L" " + std::to_wstring(i);
    return name;
}

std::vector<Profile> LoadProfiles() {
    std::vector<Profile> out;
    const std::wstring f = ProfilesFile();
    const int count = std::min<int>(static_cast<int>(GetPrivateProfileIntW(L"Profiles", L"Count", 0, f.c_str())), static_cast<int>(kMaxProfiles));
    std::vector<wchar_t> buf(32768);
    for (int i = 0; i < count; ++i) {
        Profile p;
        GetPrivateProfileStringW(L"Profiles", (L"Name" + std::to_wstring(i)).c_str(), L"", buf.data(), static_cast<DWORD>(buf.size()), f.c_str());
        p.name = buf.data();
        GetPrivateProfileStringW(L"Profiles", (L"Paths" + std::to_wstring(i)).c_str(), L"", buf.data(), static_cast<DWORD>(buf.size()), f.c_str());
        p.paths = Split(buf.data());
        if (p.paths.size() > kMaxProfilePrograms) p.paths.resize(kMaxProfilePrograms);
        if (!p.name.empty()) out.push_back(std::move(p));
    }
    return out;
}

bool SaveProfiles(const std::vector<Profile>& p) {
    const std::wstring f = ProfilesFile();
    DeleteFileW(f.c_str());
    bool ok = WritePrivateProfileStringW(L"Profiles", L"Count", std::to_wstring(p.size()).c_str(), f.c_str()) != 0;
    for (size_t i = 0; i < p.size(); ++i) {
        ok &= WritePrivateProfileStringW(L"Profiles", (L"Name" + std::to_wstring(i)).c_str(), p[i].name.c_str(), f.c_str()) != 0;
        ok &= WritePrivateProfileStringW(L"Profiles", (L"Paths" + std::to_wstring(i)).c_str(), Join(p[i].paths).c_str(), f.c_str()) != 0;
    }
    return ok;
}

bool RunSwitch(const SwitchPlan& plan, int graceMs, std::wstring& note) {
    int closed = 0, stillRunning = 0, started = 0, failed = 0;
    if (!plan.closeExes.empty()) {
        const CloseResult r = CloseApps(plan.closeExes, graceMs, false);
        closed = r.closed;
        stillRunning = r.failed;
    }
    for (const std::wstring& path : plan.startPaths) {
        std::wstring dir = path;
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir.resize(slash); else dir.clear();
        const HINSTANCE h = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, dir.empty() ? nullptr : dir.c_str(), SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(h) > 32) ++started; else ++failed;
    }
    note = std::to_wstring(closed) + L" beendet, " + std::to_wstring(started) + L" gestartet";
    if (stillRunning) note += L" (" + std::to_wstring(stillRunning) + L" läuft noch – wartet vermutlich auf eine Rückfrage)";
    if (failed) note += L", " + std::to_wstring(failed) + L" nicht gefunden";
    note += L".";
    return failed == 0 && stillRunning == 0;
}

}  // namespace boostmode
