#include "ClipLogic.h"

#include <shlobj.h>
#include <wincrypt.h>

#include <algorithm>
#include <iterator>

namespace clip {

namespace {
    std::wstring g_dataDir;

    std::wstring LowerCopy(const std::wstring& s) {
        std::wstring r = s;
        if (!r.empty()) CharLowerBuffW(&r[0], static_cast<DWORD>(r.size()));
        return r;
    }

    bool IsBlank(const std::wstring& s) {
        return std::all_of(s.begin(), s.end(), [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; });
    }

    // Trennzeichen der Speicherdatei duerfen im Text nicht vorkommen
    std::wstring Sanitize(std::wstring s) {
        for (wchar_t& c : s) if (c == 0x1E || c == 0x1F) c = L' ';
        return s;
    }

    std::string ToUtf8(const std::wstring& w) {
        if (w.empty()) return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string r(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &r[0], n, nullptr, nullptr);
        return r;
    }

    std::wstring FromUtf8(const std::string& s) {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring r(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &r[0], n);
        return r;
    }

    std::wstring Path(const wchar_t* name) { return DataDir() + L"\\" + name; }

    bool WriteFileBytes(const std::wstring& path, const std::string& data) {
        const std::wstring tmp = path + L".tmp";
        HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD w = 0;
        const bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &w, nullptr) && w == data.size();
        CloseHandle(h);
        if (!ok) { DeleteFileW(tmp.c_str()); return false; }
        return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;   // atomar: nie eine halb geschriebene Datei
    }

    bool ReadFileBytes(const std::wstring& path, std::string& out) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        const DWORD size = GetFileSize(h, nullptr);
        if (size == INVALID_FILE_SIZE || size > (64u << 20)) { CloseHandle(h); return false; }
        out.assign(size, '\0');
        DWORD r = 0;
        const bool ok = size == 0 || (ReadFile(h, &out[0], size, &r, nullptr) && r == size);
        CloseHandle(h);
        return ok;
    }

    bool OpenClipboardRetry() {
        for (int i = 0; i < 10; ++i) {
            if (OpenClipboard(nullptr)) return true;
            Sleep(20);
        }
        return false;
    }

    const char kMagic[] = "DCLP1\n";

    // Datensaetze: Felder mit 0x1F, Saetze mit 0x1E getrennt
    std::string PackRecords(const std::vector<std::vector<std::wstring>>& recs) {
        std::wstring all;
        for (size_t i = 0; i < recs.size(); ++i) {
            if (i) all += static_cast<wchar_t>(0x1E);
            for (size_t f = 0; f < recs[i].size(); ++f) {
                if (f) all += static_cast<wchar_t>(0x1F);
                all += Sanitize(recs[i][f]);
            }
        }
        return std::string(kMagic) + ToUtf8(all);
    }

    bool UnpackRecords(const std::string& blob, std::vector<std::vector<std::wstring>>& out) {
        out.clear();
        if (blob.compare(0, sizeof(kMagic) - 1, kMagic) != 0) return false;
        const std::wstring all = FromUtf8(blob.substr(sizeof(kMagic) - 1));
        if (all.empty()) return true;
        size_t start = 0;
        while (start <= all.size()) {
            const size_t e = all.find(static_cast<wchar_t>(0x1E), start);
            const std::wstring rec = all.substr(start, e == std::wstring::npos ? std::wstring::npos : e - start);
            std::vector<std::wstring> fields;
            size_t fs = 0;
            while (fs <= rec.size()) {
                const size_t fe = rec.find(static_cast<wchar_t>(0x1F), fs);
                fields.push_back(rec.substr(fs, fe == std::wstring::npos ? std::wstring::npos : fe - fs));
                if (fe == std::wstring::npos) break;
                fs = fe + 1;
            }
            out.push_back(std::move(fields));
            if (e == std::wstring::npos) break;
            start = e + 1;
        }
        return true;
    }
}

// ---- Verlauf und Prompts ----
bool AddHistory(Store& s, const std::wstring& text) {
    if (IsBlank(text) || text.size() > kMaxTextLen) return false;
    auto it = std::find(s.history.begin(), s.history.end(), text);
    if (it != s.history.end()) {
        if (it == s.history.begin()) return false;
        std::rotate(s.history.begin(), it, it + 1);       // nach vorn holen
        return true;
    }
    s.history.insert(s.history.begin(), text);
    if (s.history.size() > kMaxHistory) s.history.resize(kMaxHistory);
    return true;
}

bool RemoveHistory(Store& s, size_t index) {
    if (index >= s.history.size()) return false;
    s.history.erase(s.history.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

Prompt MakePrompt(const std::wstring& text) {
    Prompt p;
    p.text = text;
    size_t i = 0;
    while (i < text.size()) {                               // erste nicht leere Zeile
        const size_t e = text.find_first_of(L"\r\n", i);
        std::wstring line = text.substr(i, e == std::wstring::npos ? std::wstring::npos : e - i);
        const size_t a = line.find_first_not_of(L" \t");
        if (a != std::wstring::npos) {
            line = line.substr(a);
            while (!line.empty() && (line.back() == L' ' || line.back() == L'\t')) line.pop_back();
            p.title = line;
            break;
        }
        if (e == std::wstring::npos) break;
        i = e + 1;
    }
    if (p.title.size() > 40) { p.title.resize(39); p.title += L"…"; }
    if (p.title.empty()) p.title = L"Prompt";
    return p;
}

std::wstring Expand(const std::wstring& text, const std::wstring& clipText) {
    const std::wstring key = L"{clip}";
    const std::wstring low = LowerCopy(text);
    std::wstring out;
    size_t pos = 0;
    for (;;) {
        const size_t f = low.find(key, pos);
        if (f == std::wstring::npos) { out += text.substr(pos); break; }
        out += text.substr(pos, f - pos);
        out += clipText;
        pos = f + key.size();
    }
    return out;
}

bool Matches(const std::wstring& haystack, const std::wstring& filter) {
    const std::wstring h = LowerCopy(haystack), f = LowerCopy(filter);
    size_t i = 0;
    while (i < f.size()) {
        while (i < f.size() && f[i] == L' ') ++i;
        const size_t e = f.find(L' ', i);
        const std::wstring word = f.substr(i, e == std::wstring::npos ? std::wstring::npos : e - i);
        if (!word.empty() && h.find(word) == std::wstring::npos) return false;
        if (e == std::wstring::npos) break;
        i = e + 1;
    }
    return true;
}

std::wstring Preview(const std::wstring& text, size_t maxChars) {
    std::wstring r;
    bool lastSpace = true;
    for (wchar_t c : text) {
        const bool ws = c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
        if (ws) { if (!lastSpace) r += L' '; lastSpace = true; }
        else { r += c; lastSpace = false; }
        if (r.size() > maxChars + 1) break;
    }
    while (!r.empty() && r.back() == L' ') r.pop_back();
    if (r.size() > maxChars) { r.resize(maxChars > 0 ? maxChars - 1 : 0); r += L"…"; }
    return r;
}

// ---- Sperrregeln ----
bool IsPasswordManagerExe(const std::wstring& exeLower) {
    static const wchar_t* const kList[] = {
        L"keepass.exe", L"keepassxc.exe", L"bitwarden.exe", L"1password.exe", L"lastpass.exe", L"dashlane.exe",
        L"enpass.exe", L"nordpass.exe", L"roboform.exe", L"duncansvault.exe",
    };
    for (const wchar_t* p : kList) if (exeLower == p) return true;
    return false;
}

bool ShouldSkip(const std::wstring& text, bool excludedByFormat, const std::wstring& ownerExeLower) {
    return excludedByFormat || IsBlank(text) || text.size() > kMaxTextLen || IsPasswordManagerExe(ownerExeLower);
}

// ---- Zwischenablage ----
bool ReadClipboardText(std::wstring& out) {
    out.clear();
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return false;
    if (!OpenClipboardRetry()) return false;
    bool ok = false;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            const size_t maxChars = GlobalSize(h) / sizeof(wchar_t);
            size_t n = 0;
            while (n < maxChars && p[n]) ++n;
            out.assign(p, n);
            GlobalUnlock(h);
            ok = true;
        }
    }
    CloseClipboard();
    return ok;
}

bool WriteClipboardText(const std::wstring& text, bool markPrivate) {
    if (!OpenClipboardRetry()) return false;
    EmptyClipboard();
    bool ok = false;
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(h);
            ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
        }
        if (!ok) GlobalFree(h);
    }
    // eigene Schreibvorgaenge nicht in den Windows-Verlauf und die Cloud-Zwischenablage (und damit auch nicht in unseren Verlauf) aufnehmen
    if (markPrivate) for (const wchar_t* name : { L"CanIncludeInClipboardHistory", L"CanUploadToCloudClipboard" }) {
        if (HGLOBAL f = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD))) {
            if (DWORD* d = static_cast<DWORD*>(GlobalLock(f))) {
                *d = 0;
                GlobalUnlock(f);
                if (!SetClipboardData(RegisterClipboardFormatW(name), f)) GlobalFree(f);
            } else GlobalFree(f);
        }
    }
    CloseClipboard();
    return ok;
}

bool ClipboardIsExcluded() {
    if (IsClipboardFormatAvailable(RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing"))) return true;
    bool excluded = false;
    for (const wchar_t* name : { L"CanIncludeInClipboardHistory", L"CanUploadToCloudClipboard" }) {
        const UINT fmt = RegisterClipboardFormatW(name);
        if (!IsClipboardFormatAvailable(fmt)) continue;
        if (!OpenClipboardRetry()) continue;
        if (HANDLE h = GetClipboardData(fmt)) {
            if (GlobalSize(h) >= sizeof(DWORD))
                if (const DWORD* d = static_cast<const DWORD*>(GlobalLock(h))) { if (*d == 0) excluded = true; GlobalUnlock(h); }
        }
        CloseClipboard();
    }
    return excluded;
}

std::wstring ClipboardOwnerExe() {
    HWND owner = GetClipboardOwner();
    if (!owner) return L"";
    DWORD pid = 0;
    GetWindowThreadProcessId(owner, &pid);
    if (!pid) return L"";
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD n = static_cast<DWORD>(std::size(buf));
    std::wstring r;
    if (QueryFullProcessImageNameW(h, 0, buf, &n)) {
        r.assign(buf, n);
        const size_t slash = r.find_last_of(L'\\');
        if (slash != std::wstring::npos) r = r.substr(slash + 1);
    }
    CloseHandle(h);
    return LowerCopy(r);
}

void SendPasteKeys() {
    INPUT in[4] = {};
    for (INPUT& i : in) i.type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_CONTROL;
    in[1].ki.wVk = 'V';
    in[2].ki.wVk = 'V';  in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
}

// ---- Einstellungen und Speicherung ----
int HotkeyCount() { return 3; }

const Hotkey& HotkeyPreset(int index) {
    static const Hotkey kKeys[3] = {
        { MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'V', L"Strg+Alt+V" },
        { MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'V', L"Strg+Umschalt+V" },
        { MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'C', L"Strg+Alt+C" },
    };
    return kKeys[std::clamp(index, 0, 2)];
}

std::wstring DataDir() {
    if (!g_dataDir.empty()) return g_dataDir;
    wchar_t local[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local))) return L".";
    g_dataDir = std::wstring(local) + L"\\DuncansClip";
    CreateDirectoryW(g_dataDir.c_str(), nullptr);
    return g_dataDir;
}

void SetDataDirForTest(const std::wstring& dir) { g_dataDir = dir; CreateDirectoryW(dir.c_str(), nullptr); }

Settings LoadSettings() {
    Settings s;
    const std::wstring f = Path(L"settings.ini");
    s.saveHistory = GetPrivateProfileIntW(L"Settings", L"SaveHistory", 0, f.c_str()) != 0;
    s.hotkey = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Settings", L"Hotkey", 0, f.c_str())), 0, HotkeyCount() - 1);
    s.autostart = GetAutostart();
    return s;
}

bool SaveSettings(const Settings& s) {
    const std::wstring f = Path(L"settings.ini");
    bool ok = WritePrivateProfileStringW(L"Settings", L"SaveHistory", s.saveHistory ? L"1" : L"0", f.c_str()) != 0;
    ok &= WritePrivateProfileStringW(L"Settings", L"Hotkey", std::to_wstring(s.hotkey).c_str(), f.c_str()) != 0;
    return ok;
}

bool Protect(const std::string& plain, std::string& cipher) {
    DATA_BLOB in{ static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data())) }, out{};
    if (!CryptProtectData(&in, L"DuncansClip", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    cipher.assign(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return true;
}

bool Unprotect(const std::string& cipher, std::string& plain) {
    DATA_BLOB in{ static_cast<DWORD>(cipher.size()), reinterpret_cast<BYTE*>(const_cast<char*>(cipher.data())) }, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    plain.assign(reinterpret_cast<char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return true;
}

namespace {
    bool SaveEncrypted(const wchar_t* name, const std::string& blob) {
        std::string cipher;
        return Protect(blob, cipher) && WriteFileBytes(Path(name), cipher);
    }
    bool LoadEncrypted(const wchar_t* name, std::vector<std::vector<std::wstring>>& recs) {
        std::string cipher, plain;
        if (!ReadFileBytes(Path(name), cipher) || !Unprotect(cipher, plain)) return false;
        const bool ok = UnpackRecords(plain, recs);
        SecureZeroMemory(&plain[0], plain.size());
        return ok;
    }
}

bool SavePrompts(const std::vector<Prompt>& p) {
    std::vector<std::vector<std::wstring>> recs;
    for (const Prompt& x : p) recs.push_back({ x.title, x.text });
    return SaveEncrypted(L"prompts.dat", PackRecords(recs));
}

std::vector<Prompt> LoadPrompts() {
    std::vector<Prompt> out;
    std::vector<std::vector<std::wstring>> recs;
    if (!LoadEncrypted(L"prompts.dat", recs)) return out;
    for (const auto& r : recs) if (r.size() == 2) out.push_back({ r[0], r[1] });
    return out;
}

bool SaveHistory(const std::vector<std::wstring>& h) {
    std::vector<std::vector<std::wstring>> recs;
    for (const std::wstring& x : h) recs.push_back({ x });
    return SaveEncrypted(L"history.dat", PackRecords(recs));
}

std::vector<std::wstring> LoadHistory() {
    std::vector<std::wstring> out;
    std::vector<std::vector<std::wstring>> recs;
    if (!LoadEncrypted(L"history.dat", recs)) return out;
    for (const auto& r : recs) if (r.size() == 1 && !r[0].empty() && out.size() < kMaxHistory) out.push_back(r[0]);
    return out;
}

void DeleteHistoryFile() { DeleteFileW(Path(L"history.dat").c_str()); }

// ---- Autostart ----
namespace { const wchar_t* const kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"; }

bool GetAutostart() {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    const bool exists = RegQueryValueExW(k, L"DuncansClip", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(k);
    return exists;
}

bool SetAutostart(bool on, const std::wstring& exePath) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r;
    if (on) {
        const std::wstring cmd = L"\"" + exePath + L"\" --background";
        r = RegSetValueExW(k, L"DuncansClip", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        r = RegDeleteValueW(k, L"DuncansClip");
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

}  // namespace clip
