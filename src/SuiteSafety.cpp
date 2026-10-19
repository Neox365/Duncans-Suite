#include "SuiteSafety.h"

#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>

#include <cstdio>
#include <mutex>
#include <vector>

namespace suite_safety {

namespace {
    std::mutex g_logMutex;

    std::wstring ProgramData() {
        wchar_t base[MAX_PATH] = {};
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, SHGFP_TYPE_CURRENT, base))) return L"";
        return base;
    }

    std::string ToUtf8(const std::wstring& w) {
        if (w.empty()) return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
        return s;
    }

#pragma pack(push, 1)
    struct RestorePointInfo { DWORD eventType; DWORD pointType; INT64 seq; WCHAR description[256]; };
    struct StateMgrStatus { DWORD status; INT64 seq; };
#pragma pack(pop)
    using SetFn = BOOL(WINAPI*)(RestorePointInfo*, StateMgrStatus*);
}

namespace {
    // Rechte fuer %ProgramData%\DuncansSuite: SYSTEM und Administratoren Vollzugriff, normale Benutzer nur Lesen (geschuetzt, erbt an Unterordner/Dateien).
    // Ohne das duerfen Benutzer dort Dateien anlegen und einem Programm mit Adminrechten Dateien unterschieben.
    const wchar_t* kDirSddl = L"D:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)";

    bool IsReparse(const std::wstring& path) {
        const DWORD a = GetFileAttributesW(path.c_str());
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    }

    // true, wenn der Besitzer SYSTEM, Administratoren oder TrustedInstaller ist (dann hat kein normaler Benutzer die Datei angelegt)
    bool OwnerTrusted(const std::wstring& path) {
        PSID owner = nullptr;
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &sd) != ERROR_SUCCESS) return false;
        bool ok = false;
        BYTE buf[SECURITY_MAX_SID_SIZE];
        DWORD size = sizeof(buf);
        for (WELL_KNOWN_SID_TYPE t : { WinBuiltinAdministratorsSid, WinLocalSystemSid }) {
            size = sizeof(buf);
            if (CreateWellKnownSid(t, nullptr, buf, &size) && EqualSid(owner, buf)) ok = true;
        }
        LocalFree(sd);
        return ok;
    }

    // Rechte setzen und (soweit moeglich) Besitz auf Administratoren uebertragen; nur mit Adminrechten aufrufen
    void HardenDir(const std::wstring& dir) {
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(kDirSddl, SDDL_REVISION_1, &sd, nullptr)) {
            BOOL present = FALSE, defaulted = FALSE;
            PACL dacl = nullptr;
            if (GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted) && present) {
                // Besitz zuerst: Administratoren duerfen sich das Eigentum nehmen (SeTakeOwnershipPrivilege ist im erhoehten Token vorhanden)
                HANDLE tok = nullptr;
                if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
                    TOKEN_PRIVILEGES tp{};
                    tp.PrivilegeCount = 1;
                    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                    if (LookupPrivilegeValueW(nullptr, SE_TAKE_OWNERSHIP_NAME, &tp.Privileges[0].Luid)) AdjustTokenPrivileges(tok, FALSE, &tp, 0, nullptr, nullptr);
                    CloseHandle(tok);
                }
                BYTE sid[SECURITY_MAX_SID_SIZE];
                DWORD sidSize = sizeof(sid);
                if (CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sid, &sidSize))
                    SetNamedSecurityInfoW(const_cast<wchar_t*>(dir.c_str()), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, sid, nullptr, nullptr, nullptr);
                SetNamedSecurityInfoW(const_cast<wchar_t*>(dir.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                      nullptr, nullptr, dacl, nullptr);
            }
            LocalFree(sd);
        }
    }

    std::wstring TimeStamp() {
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t b[32];
        swprintf(b, 32, L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return b;
    }
}

// Der Ordner wird nur von Programmen mit Adminrechten angelegt (und abgesichert). Normale Programme (die Suite) bekommen "" zurueck, wenn es ihn noch
// nicht gibt oder er nicht vertrauenswuerdig ist (Verknuepfung oder Besitzer ist kein Administrator/SYSTEM).
std::wstring DataDir() {
    const std::wstring pd = ProgramData();
    if (pd.empty()) return L"";
    const std::wstring dir = pd + L"\\DuncansSuite";
    const bool admin = IsElevated();
    DWORD a = GetFileAttributesW(dir.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) {
        if (!admin) return L"";
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, FALSE };
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(kDirSddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor, nullptr)) {
            CreateDirectoryW(dir.c_str(), &sa);
            LocalFree(sa.lpSecurityDescriptor);
        }
        a = GetFileAttributesW(dir.c_str());
        if (a == INVALID_FILE_ATTRIBUTES) return L"";
    }
    if ((a & FILE_ATTRIBUTE_REPARSE_POINT) || !(a & FILE_ATTRIBUTE_DIRECTORY)) return L"";    // Verknuepfung/Datei statt Ordner: nicht benutzen
    static bool hardened = false;
    if (admin && !hardened) { HardenDir(dir); hardened = true; }                                // aeltere Ordner (offene Rechte) nachziehen
    if (!OwnerTrusted(dir)) return L"";
    return dir;
}

std::wstring BackupsDir() {
    const std::wstring d = DataDir();
    if (d.empty()) return L"";
    const std::wstring dir = d + L"\\Backups";
    if (IsElevated()) {
        const DWORD ex = GetFileAttributesW(dir.c_str());
        if (ex != INVALID_FILE_ATTRIBUTES && (ex & FILE_ATTRIBUTE_REPARSE_POINT) && (ex & FILE_ATTRIBUTE_DIRECTORY))
            RemoveDirectoryW(dir.c_str());                                  // untergeschobene Ordner-Verknuepfung entfernen (das Ziel bleibt unberuehrt)
        else if (ex != INVALID_FILE_ATTRIBUTES && (ex & FILE_ATTRIBUTE_DIRECTORY) && !OwnerTrusted(dir))
            HardenDir(dir);                                                 // von einem Benutzer angelegter Ordner: Rechte und Besitz uebernehmen
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, FALSE };
        if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES &&
            ConvertStringSecurityDescriptorToSecurityDescriptorW(kDirSddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor, nullptr)) {
            CreateDirectoryW(dir.c_str(), &sa);
            LocalFree(sa.lpSecurityDescriptor);
        }
    }
    const DWORD a = GetFileAttributesW(dir.c_str());
    if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT) || !(a & FILE_ATTRIBUTE_DIRECTORY) || !OwnerTrusted(dir)) return L"";
    return dir;
}

// Datei vor der Benutzung pruefen: Verknuepfung oder fremder Besitzer -> beiseite verschieben ("<Name>.untrusted_<Zeit>"), damit ein Programm mit
// Adminrechten nie fremde Inhalte liest oder ueber eine Verknuepfung woanders hinschreibt. true = Pfad ist jetzt frei oder vertrauenswuerdig.
bool EnsureTrustedFile(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return true;                       // gibt es nicht: wird neu angelegt
    if (!(a & FILE_ATTRIBUTE_REPARSE_POINT) && (a & FILE_ATTRIBUTE_DIRECTORY) == 0 && OwnerTrusted(path)) return true;
    const std::wstring aside = path + L".untrusted_" + TimeStamp();
    return MoveFileExW(path.c_str(), aside.c_str(), 0) != 0;
}

std::wstring LogPath() {
    const std::wstring d = DataDir();
    return d.empty() ? L"" : d + L"\\duncans_suite.log";
}

void Log(const wchar_t* module, const std::wstring& text) {
    const std::wstring path = LogPath();
    if (path.empty()) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    static bool checked = false;
    if (!checked) { EnsureTrustedFile(path); EnsureTrustedFile(path + L".1"); checked = true; }   // untergeschobene Datei/Verknuepfung beiseite schieben
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad) && fad.nFileSizeLow > (1u << 20))
        MoveFileExW(path.c_str(), (path + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);   // einfache Rotation
    // Verknuepfungen nicht folgen: sonst koennte ein untergeschobener Link das Anhaengen an eine fremde Datei umleiten
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(f, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { CloseHandle(f); return; }
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t head[96];
    swprintf(head, 96, L"%04d-%02d-%02d %02d:%02d:%02d [%ls] ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, module);
    const std::string line = ToUtf8(std::wstring(head) + text) + "\r\n";
    DWORD written = 0;
    WriteFile(f, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(f);
}

bool CreateRestorePoint(DWORD pointType, std::wstring& note) {
    HMODULE lib = LoadLibraryExW(L"srclient.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);   // nur aus System32 laden
    if (!lib) { note = L"Systemwiederherstellung nicht verfügbar (srclient.dll fehlt)."; Log(L"Sicherheit", L"Wiederherstellungspunkt: " + note); return false; }
    auto fn = reinterpret_cast<SetFn>(reinterpret_cast<void*>(GetProcAddress(lib, "SRSetRestorePointW")));
    if (!fn) { FreeLibrary(lib); note = L"SRSetRestorePointW wurde nicht gefunden."; Log(L"Sicherheit", L"Wiederherstellungspunkt: " + note); return false; }

    RestorePointInfo info{};
    info.eventType = 100;       // BEGIN_SYSTEM_CHANGE
    info.pointType = pointType;
    lstrcpynW(info.description, L"Duncan's Suite Pre-Optimization", 256);
    StateMgrStatus st{};
    const BOOL ok = fn(&info, &st) && st.status == 0;
    if (ok) {
        RestorePointInfo end{};
        end.eventType = 101;    // END_SYSTEM_CHANGE
        end.seq = st.seq;
        StateMgrStatus endSt{};
        fn(&end, &endSt);
    }
    FreeLibrary(lib);
    if (!ok) {
        note = st.status == ERROR_SERVICE_DISABLED
            ? L"Der Systemschutz ist auf diesem PC deaktiviert."
            : L"Der Wiederherstellungspunkt konnte nicht erstellt werden (Status " + std::to_wstring(st.status) + L").";
        Log(L"Sicherheit", L"Wiederherstellungspunkt \"Duncan's Suite Pre-Optimization\": FEHLER – " + note);
        return false;
    }
    Log(L"Sicherheit", L"Wiederherstellungspunkt \"Duncan's Suite Pre-Optimization\" angelegt (Nr. " + std::to_wstring(st.seq) + L")");
    return true;
}

bool IsElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION el{};
    DWORD n = 0;
    const bool ok = GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &n) != 0;
    CloseHandle(tok);
    return ok && el.TokenIsElevated != 0;
}

}  // namespace suite_safety

namespace suite_safety {

std::wstring BackupsDirPath() {
    const std::wstring pd = ProgramData();
    return pd.empty() ? L"" : pd + L"\\DuncansSuite\\Backups";
}

std::wstring LogFilePath() {
    const std::wstring pd = ProgramData();
    return pd.empty() ? L"" : pd + L"\\DuncansSuite\\duncans_suite.log";
}

bool AutoRestorePointEnabled() {
    wchar_t local[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local))) return true;
    const std::wstring ini = std::wstring(local) + L"\\DuncansSuite\\settings.ini";
    return GetPrivateProfileIntW(L"Allgemein", L"AutoWiederherstellungspunkt", 1, ini.c_str()) != 0;
}

}  // namespace suite_safety

namespace suite_safety {

bool IsTrustedFile(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_REPARSE_POINT) && !(a & FILE_ATTRIBUTE_DIRECTORY) && OwnerTrusted(path);
}

}  // namespace suite_safety
