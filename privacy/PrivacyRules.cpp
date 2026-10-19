#include "PrivacyRules.h"

#include <shlobj.h>

#include <cwchar>
#include <map>

namespace privacy {

namespace {
    std::wstring g_backupPath;

    HKEY Root(Hive h) { return h == Hive::Machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER; }
    const wchar_t* HiveName(Hive h) { return h == Hive::Machine ? L"HKLM" : L"HKCU"; }

    std::wstring OpId(const RegOp& op) { return std::wstring(HiveName(op.hive)) + L"\\" + op.key + L"\\" + op.name; }

    // Zustand eines Registry-Wertes
    struct Value {
        bool exists = false;
        ValType type = ValType::Dword;
        DWORD dword = 0;
        std::wstring str;
    };

    Value ReadValue(const RegOp& op) {
        Value v;
        HKEY k = nullptr;
        if (RegOpenKeyExW(Root(op.hive), op.key, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return v;
        DWORD type = 0, size = 0;
        if (RegQueryValueExW(k, op.name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS) {
            if (type == REG_DWORD && size == sizeof(DWORD)) {
                DWORD d = 0;
                if (RegQueryValueExW(k, op.name, nullptr, nullptr, reinterpret_cast<BYTE*>(&d), &size) == ERROR_SUCCESS) {
                    v.exists = true; v.type = ValType::Dword; v.dword = d;
                }
            } else if (type == REG_SZ || type == REG_EXPAND_SZ) {
                std::wstring s(size / sizeof(wchar_t) + 1, L'\0');
                DWORD sz2 = size;
                if (RegQueryValueExW(k, op.name, nullptr, nullptr, reinterpret_cast<BYTE*>(&s[0]), &sz2) == ERROR_SUCCESS) {
                    s.resize(wcslen(s.c_str()));
                    v.exists = true; v.type = ValType::String; v.str = s;
                }
            }
        }
        RegCloseKey(k);
        return v;
    }

    bool WriteValue(const RegOp& op, const Value& v, std::wstring& error) {
        HKEY k = nullptr;
        LONG r = RegCreateKeyExW(Root(op.hive), op.key, 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &k, nullptr);
        if (r != ERROR_SUCCESS) {
            error = std::wstring(L"Schreiben nicht möglich (Fehler ") + std::to_wstring(r) + L"): " + OpId(op);
            return false;
        }
        if (v.type == ValType::Dword)
            r = RegSetValueExW(k, op.name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v.dword), sizeof(DWORD));
        else
            r = RegSetValueExW(k, op.name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.str.c_str()),
                               static_cast<DWORD>((v.str.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
        if (r != ERROR_SUCCESS) {
            error = std::wstring(L"Schreiben nicht möglich (Fehler ") + std::to_wstring(r) + L"): " + OpId(op);
            return false;
        }
        return true;
    }

    bool DeleteValue(const RegOp& op, std::wstring& error) {
        HKEY k = nullptr;
        LONG r = RegOpenKeyExW(Root(op.hive), op.key, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &k);
        if (r == ERROR_FILE_NOT_FOUND || r == ERROR_PATH_NOT_FOUND) return true;     // Schluessel gibt es nicht: nichts zu loeschen
        if (r != ERROR_SUCCESS) { error = std::wstring(L"Löschen nicht möglich (Fehler ") + std::to_wstring(r) + L"): " + OpId(op); return false; }
        r = RegDeleteValueW(k, op.name);
        RegCloseKey(k);
        if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND) { error = std::wstring(L"Löschen nicht möglich (Fehler ") + std::to_wstring(r) + L"): " + OpId(op); return false; }
        return true;
    }

    // ---- Sicherung: INI-Datei, Abschnitt [Backup], Schluessel = "HKLM\Pfad\Name", Wert = "absent" | "dword:N" | "sz:Text"
    std::wstring Encode(const Value& v) {
        if (!v.exists) return L"absent";
        return v.type == ValType::Dword ? L"dword:" + std::to_wstring(v.dword) : L"sz:" + v.str;
    }

    bool Decode(const std::wstring& s, Value& v) {
        v = Value{};
        if (s == L"absent") return true;
        if (s.rfind(L"dword:", 0) == 0) { v.exists = true; v.type = ValType::Dword; v.dword = static_cast<DWORD>(wcstoul(s.c_str() + 6, nullptr, 10)); return true; }
        if (s.rfind(L"sz:", 0) == 0) { v.exists = true; v.type = ValType::String; v.str = s.substr(3); return true; }
        return false;
    }

    bool ReadBackup(const std::wstring& id, std::wstring& out) {
        wchar_t buf[2048] = {};
        GetPrivateProfileStringW(L"Backup", id.c_str(), L"\x01", buf, 2048, BackupPath().c_str());
        if (buf[0] == L'\x01' && buf[1] == 0) return false;
        out = buf;
        return true;
    }

    bool WriteBackup(const std::wstring& id, const std::wstring& value) {
        return WritePrivateProfileStringW(L"Backup", id.c_str(), value.c_str(), BackupPath().c_str()) != 0;
    }

    void DropBackup(const std::wstring& id) { WritePrivateProfileStringW(L"Backup", id.c_str(), nullptr, BackupPath().c_str()); }

    Value Target(const RegOp& op) {
        Value v;
        v.exists = true; v.type = op.type; v.dword = op.dword; v.str = op.str ? op.str : L"";
        return v;
    }

    bool Equal(const Value& a, const Value& b) {
        if (a.exists != b.exists) return false;
        if (!a.exists) return true;
        if (a.type != b.type) return false;
        return a.type == ValType::Dword ? a.dword == b.dword : _wcsicmp(a.str.c_str(), b.str.c_str()) == 0;
    }

    void StopService(const wchar_t* name) {
        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm) return;
        if (SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_STOP | SERVICE_QUERY_STATUS)) {
            SERVICE_STATUS st{};
            ControlService(svc, SERVICE_CONTROL_STOP, &st);   // Fehler ignorieren: Dienst laeuft evtl. nicht / ist nicht stoppbar
            CloseServiceHandle(svc);
        }
        CloseServiceHandle(scm);
    }

    void StartServiceIfAuto(const wchar_t* name) {
        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm) return;
        if (SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_START | SERVICE_QUERY_CONFIG)) {
            StartServiceW(svc, 0, nullptr);
            CloseServiceHandle(svc);
        }
        CloseServiceHandle(scm);
    }

    std::wstring ServiceNameFromKey(const wchar_t* key) {
        const wchar_t* p = wcsstr(key, L"\\Services\\");
        return p ? std::wstring(p + 10) : L"";
    }
}

void SetBackupPath(const std::wstring& path) { g_backupPath = path; }

std::wstring BackupPath() {
    if (!g_backupPath.empty()) return g_backupPath;
    wchar_t base[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, base))) return L"DuncansPrivacyShield_backup.ini";
    const std::wstring dir = std::wstring(base) + L"\\DuncansPrivacyShield";
    CreateDirectoryW(dir.c_str(), nullptr);
    g_backupPath = dir + L"\\backup.ini";
    return g_backupPath;
}

bool HasBackup() {
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(L"Backup", nullptr, L"", buf, 64, BackupPath().c_str());   // Schluesselliste; leer = keine Eintraege
    return buf[0] != 0;
}

bool IsApplied(const Setting& s) {
    for (const RegOp& op : s.ops)
        if (!Equal(ReadValue(op), Target(op))) return false;
    return true;
}

bool Apply(const Setting& s, std::wstring& error) {
    // 1) alle Originalwerte sichern (nur, wenn noch keine Sicherung existiert - sonst wuerde der Originalzustand ueberschrieben)
    for (const RegOp& op : s.ops) {
        std::wstring ignore;
        if (ReadBackup(OpId(op), ignore)) continue;
        if (!WriteBackup(OpId(op), Encode(ReadValue(op)))) { error = L"Die Sicherungsdatei kann nicht geschrieben werden: " + BackupPath(); return false; }
    }
    // 2) Zielwerte schreiben; bei einem Fehler mittendrin alles wieder zuruecknehmen (keine halb angewendete Einstellung)
    for (const RegOp& op : s.ops)
        if (!WriteValue(op, Target(op), error)) {
            std::wstring ignore;
            Revert(s, ignore);
            return false;
        }
    // 3) Dienste beenden
    for (const wchar_t* svc : s.stopServices) StopService(svc);
    return true;
}

bool Revert(const Setting& s, std::wstring& error) {
    bool ok = true;
    for (const RegOp& op : s.ops) {
        std::wstring enc;
        Value old;
        if (!ReadBackup(OpId(op), enc) || !Decode(enc, old)) continue;     // nie angewendet: nichts zurueckzusetzen
        if (Equal(old, ReadValue(op))) { DropBackup(OpId(op)); continue; }  // Wert steht schon auf dem Original: nur die Sicherung verwerfen
        const bool done = old.exists ? WriteValue(op, old, error) : DeleteValue(op, error);
        if (!done) { ok = false; continue; }                               // Sicherung behalten, damit ein neuer Versuch moeglich bleibt
        DropBackup(OpId(op));
        if (old.exists && old.type == ValType::Dword && old.dword == 2) {  // Dienst war automatisch gestartet: wieder starten
            const std::wstring svc = ServiceNameFromKey(op.key);
            if (!svc.empty() && _wcsicmp(op.name, L"Start") == 0) StartServiceIfAuto(svc.c_str());
        }
    }
    return ok;
}

bool RevertAll(int& restored, std::wstring& error) {
    restored = 0;
    bool ok = true;
    for (const Setting& s : AllSettings()) {
        bool had = false;
        for (const RegOp& op : s.ops) { std::wstring e; if (ReadBackup(OpId(op), e)) had = true; }
        if (!had) continue;
        if (Revert(s, error)) ++restored; else ok = false;
    }
    return ok;
}

bool CreateRestorePoint(const std::wstring& description, std::wstring& error) {
    HMODULE lib = LoadLibraryExW(L"srclient.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!lib) { error = L"Der Systemschutz (srclient.dll) ist nicht verfügbar."; return false; }
    struct RestorePointInfo { DWORD eventType; DWORD pointType; LONGLONG seq; WCHAR description[256]; };
    struct StateMgrStatus { DWORD status; LONGLONG seq; };
    using SetFn = BOOL(WINAPI*)(RestorePointInfo*, StateMgrStatus*);
    auto fn = reinterpret_cast<SetFn>(reinterpret_cast<void*>(GetProcAddress(lib, "SRSetRestorePointW")));
    if (!fn) { FreeLibrary(lib); error = L"SRSetRestorePointW wurde nicht gefunden."; return false; }

    RestorePointInfo info{};
    info.eventType = 100;      // BEGIN_SYSTEM_CHANGE
    info.pointType = 12;       // MODIFY_SETTINGS
    wcsncpy(info.description, description.c_str(), 255);
    StateMgrStatus st{};
    const BOOL ok = fn(&info, &st);
    if (ok) {
        info.eventType = 101;  // END_SYSTEM_CHANGE
        info.seq = st.seq;
        fn(&info, &st);
    }
    FreeLibrary(lib);
    if (!ok) {
        error = st.status == ERROR_SERVICE_DISABLED
            ? L"Der Systemschutz ist auf diesem PC deaktiviert."
            : L"Der Wiederherstellungspunkt konnte nicht erstellt werden (Status " + std::to_wstring(st.status) + L").";
        return false;
    }
    return true;
}

const std::vector<Setting>& AllSettings() {
    static const std::vector<Setting> all = {
        { 1, L"Telemetrie & Diagnosedaten", L"Telemetrie minimieren",
          L"Setzt die Diagnosedaten-Stufe per Richtlinie auf das Minimum (AllowTelemetry = 0). Bei Windows Home/Pro gilt dann die Stufe „Erforderlich“.",
          { { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", L"AllowTelemetry", ValType::Dword, 0, nullptr },
            { Hive::Machine, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\DataCollection", L"AllowTelemetry", ValType::Dword, 0, nullptr } }, {} },
        { 2, L"Telemetrie & Diagnosedaten", L"Feedback-Aufforderungen abschalten",
          L"Windows fragt nicht mehr nach Feedback und Bewertungen (Feedback-Häufigkeit auf „nie“).",
          { { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", L"DoNotShowFeedbackNotifications", ValType::Dword, 1, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Siuf\\Rules", L"NumberOfSIUFInPeriod", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Siuf\\Rules", L"PeriodInNanoSeconds", ValType::Dword, 0, nullptr } }, {} },
        { 3, L"Telemetrie & Diagnosedaten", L"Telemetrie-Dienste stoppen",
          L"Deaktiviert „Benutzererfahrungen und Telemetrie“ (DiagTrack) und den WAP-Push-Dienst (dmwappushservice).",
          { { Hive::Machine, L"SYSTEM\\CurrentControlSet\\Services\\DiagTrack", L"Start", ValType::Dword, 4, nullptr },
            { Hive::Machine, L"SYSTEM\\CurrentControlSet\\Services\\dmwappushservice", L"Start", ValType::Dword, 4, nullptr } },
          { L"DiagTrack", L"dmwappushservice" } },
        { 4, L"Telemetrie & Diagnosedaten", L"Maßgeschneiderte Erfahrungen aus",
          L"Windows nutzt Diagnosedaten nicht mehr für personalisierte Tipps, Werbung und Empfehlungen.",
          { { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Privacy", L"TailoredExperiencesWithDiagnosticDataEnabled", ValType::Dword, 0, nullptr } }, {} },
        { 5, L"Startmenü-Suche", L"Web- und Bing-Ergebnisse im Startmenü aus",
          L"Die Suche im Startmenü zeigt nur noch lokale Treffer (Apps, Dateien, Einstellungen) und sendet keine Eingaben an Bing.",
          { { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Search", L"BingSearchEnabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Search", L"CortanaConsent", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Explorer", L"DisableSearchBoxSuggestions", ValType::Dword, 1, nullptr } }, {} },
        { 6, L"System-Tracking", L"Werbe-ID deaktivieren",
          L"Apps können die persönliche Werbe-ID nicht mehr für personalisierte Werbung verwenden.",
          { { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled", ValType::Dword, 0, nullptr },
            { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AdvertisingInfo", L"DisabledByGroupPolicy", ValType::Dword, 1, nullptr } }, {} },
        { 7, L"System-Tracking", L"Standortdienste deaktivieren",
          L"Schaltet die Standortbestimmung systemweit ab. Karten-Apps und „Mein Gerät suchen“ funktionieren dann nicht mehr.",
          { { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\LocationAndSensors", L"DisableLocation", ValType::Dword, 1, nullptr },
            { Hive::Machine, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\location", L"Value", ValType::String, 0, L"Deny" } }, {} },
        { 8, L"System-Tracking", L"Eingabe- und Handschriftdaten nicht übermitteln",
          L"Windows sammelt und übermittelt keine Tipp-, Handschrift- und Kontaktdaten zur Verbesserung der Texterkennung mehr.",
          { { Hive::User, L"SOFTWARE\\Microsoft\\Input\\TIPC", L"Enabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\InputPersonalization", L"RestrictImplicitInkCollection", ValType::Dword, 1, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\InputPersonalization", L"RestrictImplicitTextCollection", ValType::Dword, 1, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\InputPersonalization\\TrainedDataStore", L"HarvestContacts", ValType::Dword, 0, nullptr } }, {} },
    };
    return all;
}

}  // namespace privacy
