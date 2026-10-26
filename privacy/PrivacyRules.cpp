#include "PrivacyRules.h"

#include <shlobj.h>
#include <winioctl.h>

#include "../src/SuiteSafety.h"

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
        const std::wstring bp = BackupPath();
        if (bp.empty()) return false;
        GetPrivateProfileStringW(L"Backup", id.c_str(), L"\x01", buf, 2048, bp.c_str());
        if (buf[0] == L'\x01' && buf[1] == 0) return false;
        out = buf;
        return true;
    }

    bool WriteBackup(const std::wstring& id, const std::wstring& value) {
        const std::wstring bp = BackupPath();
        return !bp.empty() && WritePrivateProfileStringW(L"Backup", id.c_str(), value.c_str(), bp.c_str()) != 0;
    }

    void DropBackup(const std::wstring& id) { const std::wstring bp = BackupPath(); if (!bp.empty()) WritePrivateProfileStringW(L"Backup", id.c_str(), nullptr, bp.c_str()); }

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

    // Beendet einen Dienst; laufende abhaengige Dienste werden zuerst beendet (sonst lehnt Windows das Beenden ab)
    void StopService(const wchar_t* name) {
        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm) return;
        if (SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS)) {
            SERVICE_STATUS st{};
            for (int round = 0; round < 20; ++round) {
                DWORD need = 0, count = 0;
                EnumDependentServicesW(svc, SERVICE_ACTIVE, nullptr, 0, &need, &count);
                if (!need) break;   // keine laufenden abhaengigen Dienste (mehr)
                std::vector<BYTE> buf(need);
                auto* list = reinterpret_cast<ENUM_SERVICE_STATUSW*>(buf.data());
                if (!EnumDependentServicesW(svc, SERVICE_ACTIVE, list, need, &need, &count) || count == 0) break;
                for (DWORD i = 0; i < count; ++i)
                    if (SC_HANDLE dep = OpenServiceW(scm, list[i].lpServiceName, SERVICE_STOP)) {
                        ControlService(dep, SERVICE_CONTROL_STOP, &st);
                        CloseServiceHandle(dep);
                    }
                Sleep(150);
            }
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

    // ---- Dienste ueber die Dienstverwaltung (Service Control Manager) ----
    struct SvcState {
        bool exists = false;
        DWORD start = 0;       // SERVICE_AUTO_START, SERVICE_DEMAND_START, SERVICE_DISABLED ...
        bool delayed = false;  // verzoegerter Autostart
    };

    SvcState ReadService(const wchar_t* name) {
        SvcState st;
        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm) return st;
        if (SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_QUERY_CONFIG)) {
            DWORD need = 0;
            QueryServiceConfigW(svc, nullptr, 0, &need);
            if (need) {
                std::vector<BYTE> buf(need);
                auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data());
                if (QueryServiceConfigW(svc, cfg, need, &need)) { st.exists = true; st.start = cfg->dwStartType; }
            }
            if (st.exists && st.start == SERVICE_AUTO_START) {
                DWORD n2 = 0;
                QueryServiceConfig2W(svc, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, nullptr, 0, &n2);
                if (n2) {
                    std::vector<BYTE> b2(n2);
                    if (QueryServiceConfig2W(svc, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, b2.data(), n2, &n2))
                        st.delayed = reinterpret_cast<SERVICE_DELAYED_AUTO_START_INFO*>(b2.data())->fDelayedAutostart != 0;
                }
            }
            CloseServiceHandle(svc);
        }
        CloseServiceHandle(scm);
        return st;
    }

    bool WriteService(const wchar_t* name, DWORD start, bool delayed, std::wstring& error) {
        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm) { error = std::wstring(L"Dienstverwaltung nicht erreichbar (Fehler ") + std::to_wstring(GetLastError()) + L")"; return false; }
        bool ok = false;
        if (SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_CHANGE_CONFIG)) {
            ok = ChangeServiceConfigW(svc, SERVICE_NO_CHANGE, start, SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) != 0;
            if (!ok) error = std::wstring(L"Starttyp von ") + name + L" nicht änderbar (Fehler " + std::to_wstring(GetLastError()) + L")";
            else if (start == SERVICE_AUTO_START) {   // verzoegerten Autostart so wiederherstellen, wie er war
                SERVICE_DELAYED_AUTO_START_INFO info{ delayed ? TRUE : FALSE };
                ChangeServiceConfig2W(svc, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &info);
            }
            CloseServiceHandle(svc);
        }
        else error = std::wstring(L"Dienst ") + name + L" nicht zugänglich (Fehler " + std::to_wstring(GetLastError()) + L")";
        CloseServiceHandle(scm);
        return ok;
    }

    std::wstring SvcId(const ServiceOp& op) { return std::wstring(L"SVC\\") + op.name; }
    std::wstring EncodeSvc(const SvcState& st) { return st.exists ? L"svc:" + std::to_wstring(st.start) + L":" + (st.delayed ? L"1" : L"0") : L"absent"; }
    bool DecodeSvc(const std::wstring& s, SvcState& st) {
        st = SvcState{};
        if (s == L"absent") return true;
        if (s.rfind(L"svc:", 0) != 0) return false;
        st.exists = true;
        st.start = static_cast<DWORD>(wcstoul(s.c_str() + 4, nullptr, 10));
        const size_t colon = s.find(L':', 4);
        st.delayed = colon != std::wstring::npos && s.compare(colon + 1, 1, L"1") == 0;
        return true;
    }

    std::wstring ServiceNameFromKey(const wchar_t* key) {
        const wchar_t* p = wcsstr(key, L"\\Services\\");
        return p ? std::wstring(p + 10) : L"";
    }
}


std::wstring BackupPath() {
    if (!g_backupPath.empty()) return g_backupPath;
    // %ProgramData%\DuncansSuite\Backups\privacy_backup.ini. Ordner muss von Administratoren stammen (sonst ""): ein Benutzer
    // koennte sonst Sicherungswerte unterschieben, die beim Wiederherstellen mit Adminrechten in die Registry geschrieben werden.
    const std::wstring dir = suite_safety::BackupsDir();
    if (dir.empty()) return L"";
    const std::wstring path = dir + L"\\privacy_backup.ini";
    if (!suite_safety::EnsureTrustedFile(path)) return L"";    // fremde/verknuepfte Datei konnte nicht beiseite geschoben werden
    g_backupPath = path;
    // Einmalige Uebernahme der Sicherung aus frueheren Versionen (%LOCALAPPDATA%\DuncansPrivacyShield\backup.ini)
    if (GetFileAttributesW(g_backupPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wchar_t local[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local))) {
            const std::wstring old = std::wstring(local) + L"\\DuncansPrivacyShield\\backup.ini";
            if (suite_safety::IsTrustedFile(old) && CopyFileW(old.c_str(), g_backupPath.c_str(), TRUE)) {   // nur Dateien, die ein Administrator-Prozess angelegt hat
                MoveFileExW(old.c_str(), (old + L".migrated").c_str(), MOVEFILE_REPLACE_EXISTING);
                suite_safety::Log(L"PrivacyShield", L"Sicherung aus früherer Version übernommen: " + old + L" -> " + g_backupPath);
            }
        }
    }
    return g_backupPath;
}


bool HasBackup() {
    wchar_t buf[64] = {};
    const std::wstring bp = BackupPath();
    if (bp.empty()) return false;
    GetPrivateProfileStringW(L"Backup", nullptr, L"", buf, 64, bp.c_str());   // Schluesselliste; leer = keine Eintraege
    return buf[0] != 0;
}

bool SystemDriveIsSsd(bool& known) {
    known = false;
    wchar_t win[MAX_PATH] = {};
    GetWindowsDirectoryW(win, MAX_PATH);
    wchar_t path[] = L"\\\\.\\X:";
    path[4] = win[0];
    HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceSeekPenaltyProperty;
    q.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR d{};
    DWORD ret = 0;
    const BOOL ok = DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, &d, sizeof d, &ret, nullptr);
    CloseHandle(h);
    if (!ok || ret < sizeof d) return false;
    known = true;
    return d.IncursSeekPenalty == FALSE;
}

bool IsApplied(const Setting& s) {
    for (const RegOp& op : s.ops)
        if (!op.optional && !Equal(ReadValue(op), Target(op))) return false;
    for (const ServiceOp& so : s.services) {
        const SvcState st = ReadService(so.name);
        if (st.exists && st.start != so.startType) return false;   // fehlt der Dienst, gibt es nichts abzuschalten
    }
    return true;
}

bool Apply(const Setting& s, std::wstring& error) {
    if (s.requiresSsd) {   // z. B. SysMain: auf einer Festplatte wuerde das Abschalten das Arbeiten spuerbar verlangsamen
        bool known = false;
        const bool ssd = SystemDriveIsSsd(known);
        if (known && !ssd) {
            error = L"Das Systemlaufwerk ist eine Festplatte (keine SSD). Der Dienst beschleunigt dort das Laden von Programmen und bleibt deshalb an.";
            suite_safety::Log(L"PrivacyShield", L"Abgelehnt (keine SSD): " + std::wstring(s.title));
            return false;
        }
    }
    // 1) alle Originalwerte sichern (nur, wenn noch keine Sicherung existiert - sonst wuerde der Originalzustand ueberschrieben)
    for (const RegOp& op : s.ops) {
        std::wstring ignore;
        if (ReadBackup(OpId(op), ignore)) continue;
        const std::wstring before = Encode(ReadValue(op));
        if (!WriteBackup(OpId(op), before)) { error = L"Die Sicherungsdatei kann nicht geschrieben werden oder der Ordner ist nicht vertrauenswürdig (%ProgramData%\\DuncansSuite\\Backups muss Administratoren gehören)."; suite_safety::Log(L"PrivacyShield", L"FEHLER Sicherung: " + OpId(op) + L" -> " + BackupPath()); return false; }
        suite_safety::Log(L"PrivacyShield", L"Sicherung " + OpId(op) + L" = " + before);
    }
    for (const ServiceOp& so : s.services) {   // auch den Starttyp der Dienste sichern
        std::wstring ignore;
        if (ReadBackup(SvcId(so), ignore)) continue;
        const std::wstring before = EncodeSvc(ReadService(so.name));
        if (!WriteBackup(SvcId(so), before)) { error = L"Die Sicherungsdatei kann nicht geschrieben werden oder der Ordner ist nicht vertrauenswürdig (%ProgramData%\\DuncansSuite\\Backups muss Administratoren gehören)."; return false; }
        suite_safety::Log(L"PrivacyShield", L"Sicherung " + SvcId(so) + L" = " + before);
    }
    // 2) Zielwerte schreiben; bei einem Fehler mittendrin alles wieder zuruecknehmen (keine halb angewendete Einstellung)
    for (const RegOp& op : s.ops)
        if (WriteValue(op, Target(op), error)) suite_safety::Log(L"PrivacyShield", L"Gesetzt " + OpId(op) + L" = " + Encode(Target(op)) + L" (ok)");
        else if (op.optional) { suite_safety::Log(L"PrivacyShield", L"Hinweis: optionaler Wert nicht gesetzt " + OpId(op) + L": " + error); error.clear(); }
        else {
            suite_safety::Log(L"PrivacyShield", L"FEHLER beim Setzen " + OpId(op) + L": " + error + L" – Einstellung wird zurückgenommen");
            std::wstring ignore;
            Revert(s, ignore);
            return false;
        }
    for (const ServiceOp& so : s.services) {      // Starttyp per Dienstverwaltung setzen
        const SvcState st = ReadService(so.name);
        if (!st.exists) { suite_safety::Log(L"PrivacyShield", std::wstring(L"Dienst nicht vorhanden: ") + so.name); continue; }
        if (WriteService(so.name, so.startType, false, error)) suite_safety::Log(L"PrivacyShield", L"Dienst " + std::wstring(so.name) + L": Starttyp " + std::to_wstring(st.start) + L" -> " + std::to_wstring(so.startType) + L" (ok)");
        else {
            suite_safety::Log(L"PrivacyShield", L"FEHLER beim Ändern des Dienstes " + std::wstring(so.name) + L": " + error + L" – Einstellung wird zurückgenommen");
            std::wstring ignore;
            Revert(s, ignore);
            return false;
        }
    }
    // 3) Dienste beenden
    for (const wchar_t* svc : s.stopServices) StopService(svc);
    for (const ServiceOp& so : s.services)
        if (so.startType == SERVICE_DISABLED) StopService(so.name);
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
        suite_safety::Log(L"PrivacyShield", std::wstring(L"Wiederhergestellt ") + OpId(op) + L" = " + enc + (done ? L" (ok)" : L" FEHLER: " + error));
        if (!done) { ok = false; continue; }                               // Sicherung behalten, damit ein neuer Versuch moeglich bleibt
        DropBackup(OpId(op));
        if (old.exists && old.type == ValType::Dword && old.dword == 2) {  // Dienst war automatisch gestartet: wieder starten
            const std::wstring svc = ServiceNameFromKey(op.key);
            if (!svc.empty() && _wcsicmp(op.name, L"Start") == 0) StartServiceIfAuto(svc.c_str());
        }
    }
    for (const ServiceOp& so : s.services) {
        std::wstring enc;
        SvcState old;
        if (!ReadBackup(SvcId(so), enc) || !DecodeSvc(enc, old)) continue;   // nie angewendet
        const SvcState now = ReadService(so.name);
        if (!old.exists || !now.exists || (now.start == old.start && now.delayed == old.delayed)) { DropBackup(SvcId(so)); continue; }
        std::wstring e2;
        const bool done = WriteService(so.name, old.start, old.delayed, e2);
        suite_safety::Log(L"PrivacyShield", L"Wiederhergestellt " + SvcId(so) + L" = " + enc + (done ? L" (ok)" : L" FEHLER: " + e2));
        if (!done) { ok = false; error = e2; continue; }
        DropBackup(SvcId(so));
        if (old.start == SERVICE_AUTO_START) StartServiceIfAuto(so.name);   // war automatisch gestartet: wieder starten
    }
    return ok;
}

bool RevertAll(int& restored, std::wstring& error) {
    restored = 0;
    bool ok = true;
    for (const Setting& s : AllSettings()) {
        bool had = false;
        for (const RegOp& op : s.ops) { std::wstring e; if (ReadBackup(OpId(op), e)) had = true; }
        for (const ServiceOp& so : s.services) { std::wstring e; if (ReadBackup(SvcId(so), e)) had = true; }
        if (!had) continue;
        if (Revert(s, error)) ++restored; else ok = false;
    }
    return ok;
}

bool CreateRestorePoint(const std::wstring& /*description*/, std::wstring& error) {
    // gemeinsame Implementierung (Name "Duncan's Suite Pre-Optimization", Typ MODIFY_SETTINGS, mit Log)
    return suite_safety::CreateRestorePoint(12, error);
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
        { 11, L"Telemetrie & Diagnosedaten", L"Windows-Tipps & vorgeschlagene Inhalte aus",
          L"Verhindert Popup-Hinweise wie „Nutzen Sie Edge“ oder Tipps zur Systemeinstellung.",
          { { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"SubscribedContent-338388Enabled", ValType::Dword, 0, nullptr } }, {} },
        { 13, L"Telemetrie & Diagnosedaten", L"Treiber-Updates über Windows Update verhindern",
          L"Windows sucht und installiert Gerätetreiber nicht mehr automatisch über Windows Update (Richtlinie SearchOrderConfig = 0).",
          { { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DriverSearching", L"SearchOrderConfig", ValType::Dword, 0, nullptr } }, {} },
        { 5, L"Startmenü-Suche", L"Web- und Bing-Ergebnisse im Startmenü aus",
          L"Die Suche im Startmenü zeigt nur noch lokale Treffer (Apps, Dateien, Einstellungen) und sendet keine Eingaben an Bing.",
          { { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Search", L"BingSearchEnabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Search", L"CortanaConsent", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Explorer", L"DisableSearchBoxSuggestions", ValType::Dword, 1, nullptr } }, {} },
        { 10, L"Startmenü-Suche", L"Zuletzt geöffnete Dateien im Startmenü verbergen",
          L"Blendet den Bereich „Empfohlen“ und zuletzt geöffnete Dateien im Startmenü/Explorer für besseren Sichtschutz aus.",
          { { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"Start_TrackDocs", ValType::Dword, 0, nullptr } }, {} },
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
        { 9, L"System-Tracking", L"Windows Copilot & KI-Analyse aus",
          L"Deaktiviert die tief integrierte Copilot-Assistentenfunktion und die Hintergrund-Analyse.",
          { { Hive::User, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsCopilot", L"TurnOffWindowsCopilot", ValType::Dword, 1, nullptr } }, {} },
        { 12, L"System-Tracking", L"Aktivitätsverlauf (Timeline) deaktivieren",
          L"Windows speichert nicht mehr, welche Apps und Dateien du nutzt, und synchronisiert keinen Aktivitätsverlauf.",
          { { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"EnableActivityFeed", ValType::Dword, 0, nullptr } }, {} },
        { 14, L"Hintergrund & Netzwerk", L"Edge nicht im Hintergrund starten",
          L"Microsoft Edge startet nicht mehr vorab beim Anmelden und läuft nach dem Schließen nicht im Hintergrund weiter (Startbeschleunigung und Hintergrundmodus aus).",
          { { Hive::User, L"SOFTWARE\\Policies\\Microsoft\\Edge", L"AllowPrelaunch", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Policies\\Microsoft\\Edge", L"StartupBoostEnabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Policies\\Microsoft\\Edge", L"BackgroundModeEnabled", ValType::Dword, 0, nullptr } }, {} },
        { 15, L"Hintergrund & Netzwerk", L"Update-Weitergabe an andere PCs aus (Delivery Optimization)",
          L"Windows lädt Updates nur noch direkt von Microsoft und stellt heruntergeladene Teile nicht mehr anderen PCs im Netzwerk oder im Internet zur Verfügung (Modus „nur HTTP“).",
          { { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DeliveryOptimization", L"DODownloadMode", ValType::Dword, 0, nullptr } }, {} },
        { 16, L"KI & Sprache", L"Windows Recall / Bildschirm-Analyse aus",
          L"Windows speichert keine Schnappschüsse deiner Bildschirmaktivität und wertet sie nicht mit KI aus (Richtlinie DisableAIDataAnalysis). Betrifft vor allem Windows 11 auf Copilot+-PCs.",
          { { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsAI", L"DisableAIDataAnalysis", ValType::Dword, 1, nullptr } }, {} },
        { 17, L"KI & Sprache", L"Online-Spracherkennung aus",
          L"Sprachfunktionen senden keine Aufnahmen mehr an Microsoft-Server zur Auswertung; die Online-Spracherkennung wird nicht mehr akzeptiert.",
          { { Hive::User, L"Software\\Microsoft\\Speech_OneCore\\Settings\\OnlineSpeechPrivacy", L"HasAccepted", ValType::Dword, 0, nullptr } }, {} },
        { 18, L"Oberfläche & Hintergrund-Apps", L"Widgets & News-Feed in der Taskleiste aus",
          L"Blendet die Windows-Widgets (Wetter, Nachrichten) in der Taskleiste aus und beendet ihre Hintergrundaktivität.",
          { { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Dsh", L"AllowNewsAndInterests", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"TaskbarDa", ValType::Dword, 0, nullptr, true } }, {} },
        { 19, L"Oberfläche & Hintergrund-Apps", L"Xbox-Bildschirmaufzeichnung (Game DVR) aus",
          L"Schaltet die Hintergrundaufzeichnung der Xbox Game Bar aus, die im Spiel Leistung kostet.",
          { { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR", L"AllowGameDVR", ValType::Dword, 0, nullptr },
            { Hive::User, L"System\\GameConfigStore", L"GameDVR_Enabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled", ValType::Dword, 0, nullptr } }, {} },
        { 20, L"Oberfläche & Hintergrund-Apps", L"Hintergrund-Apps verbieten",
          L"Microsoft-Store-Apps (UWP) dürfen nicht mehr im Hintergrund laufen (globaler Zugriff „Aus“). Spart Strom und Arbeitsspeicher.",
          { { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications", L"GlobalUserDisabled", ValType::Dword, 1, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Search", L"BackgroundAppGlobalToggle", ValType::Dword, 0, nullptr } }, {}, {}, true },
        { 21, L"Windows-Dienste (Leistung)", L"Druckerwarteschlange (Print Spooler) deaktivieren",
          L"Deaktiviert und beendet den Dienst „Druckwarteschlange“. Nur für PCs ohne Drucker: Danach geht auch das Drucken in PDF nicht mehr.",
          {}, {}, { { L"Spooler", SERVICE_DISABLED } }, true },
        { 22, L"Windows-Dienste (Leistung)", L"SysMain (Superfetch) deaktivieren",
          L"Deaktiviert und beendet SysMain, das Programme vorab in den Speicher lädt. Nur bei SSD-Systemlaufwerken erlaubt, dort bringt der Dienst kaum etwas.",
          {}, {}, { { L"SysMain", SERVICE_DISABLED } }, true, true },
        { 23, L"Windows-Dienste (Leistung)", L"Remoteregistrierung deaktivieren",
          L"Deaktiviert und beendet den Dienst „Remoteregistrierung“, der Registry-Zugriffe von anderen PCs im Netzwerk erlaubt. Auf Heim-PCs nie nötig.",
          {}, {}, { { L"RemoteRegistry", SERVICE_DISABLED } }, true },
        { 24, L"Windows-Dienste (Leistung)", L"Windows-Fehlerberichterstattung (WerSvc) deaktivieren",
          L"Deaktiviert und beendet den Dienst, der Absturzberichte an Microsoft sendet. Absturzdiagnosen von Windows entfallen.",
          {}, {}, { { L"WerSvc", SERVICE_DISABLED } }, true },
    };
    return all;
}

}  // namespace privacy
