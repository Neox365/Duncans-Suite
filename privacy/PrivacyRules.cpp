#include "PrivacyRules.h"

#include <shlobj.h>
#include <taskschd.h>
#include <winioctl.h>

#include "../src/SuiteSafety.h"

#include <algorithm>
#include <cwchar>
#include <map>
#include <sstream>

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
            error = (r == ERROR_ACCESS_DENIED ? std::wstring(L"Windows schützt diesen Wert vor Änderungen (Zugriff verweigert, auch für Administratoren; Fehler 5): ")
                                              : std::wstring(L"Schreiben nicht möglich (Fehler ") + std::to_wstring(r) + L"): ") + OpId(op);
            return false;
        }
        if (v.type == ValType::Dword)
            r = RegSetValueExW(k, op.name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v.dword), sizeof(DWORD));
        else
            r = RegSetValueExW(k, op.name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.str.c_str()),
                               static_cast<DWORD>((v.str.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
        if (r != ERROR_SUCCESS) {
            error = (r == ERROR_ACCESS_DENIED ? std::wstring(L"Windows schützt diesen Wert vor Änderungen (Zugriff verweigert, auch für Administratoren; Fehler 5): ")
                                              : std::wstring(L"Schreiben nicht möglich (Fehler ") + std::to_wstring(r) + L"): ") + OpId(op);
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

    // ---- Geplante Aufgaben (Aufgabenplanung per COM; unabhaengig von der Windows-Sprache) ----
    struct TaskState {
        bool exists = false;
        bool enabled = false;
    };

    struct ComScope {
        bool ok;
        ComScope() { ok = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }   // schon anders initialisiert (RPC_E_CHANGED_MODE): COM bleibt trotzdem nutzbar
        ~ComScope() { if (ok) CoUninitialize(); }
    };

    bool OpenTask(const TaskOp& t, IRegisteredTask** task) {
        *task = nullptr;
        ITaskService* svc = nullptr;
        if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService, reinterpret_cast<void**>(&svc)))) return false;
        VARIANT none;
        VariantInit(&none);
        bool ok = false;
        if (SUCCEEDED(svc->Connect(none, none, none, none))) {
            ITaskFolder* folder = nullptr;
            BSTR bf = SysAllocString(t.folder);
            if (SUCCEEDED(svc->GetFolder(bf, &folder))) {
                BSTR bn = SysAllocString(t.name);
                ok = SUCCEEDED(folder->GetTask(bn, task));
                SysFreeString(bn);
                folder->Release();
            }
            SysFreeString(bf);
        }
        svc->Release();
        return ok;
    }

    TaskState ReadTask(const TaskOp& t) {
        ComScope com;
        TaskState st;
        IRegisteredTask* task = nullptr;
        if (OpenTask(t, &task)) {
            VARIANT_BOOL en = VARIANT_FALSE;
            if (SUCCEEDED(task->get_Enabled(&en))) { st.exists = true; st.enabled = en != VARIANT_FALSE; }
            task->Release();
        }
        return st;
    }

    bool WriteTask(const TaskOp& t, bool enabled, std::wstring& error) {
        ComScope com;
        IRegisteredTask* task = nullptr;
        if (!OpenTask(t, &task)) { error = std::wstring(L"Geplante Aufgabe nicht zugänglich: ") + t.name; return false; }
        const HRESULT hr = task->put_Enabled(enabled ? VARIANT_TRUE : VARIANT_FALSE);
        task->Release();
        if (FAILED(hr)) {
            wchar_t code[16];
            swprintf(code, 16, L"0x%08X", static_cast<unsigned>(hr));
            error = std::wstring(L"Geplante Aufgabe \"") + t.name + L"\" lässt sich nicht " + (enabled ? L"einschalten" : L"abschalten") + L" (Fehler " + code + L"; Administratorrechte nötig)";
            return false;
        }
        return true;
    }

    std::wstring TaskId(const TaskOp& t) { return std::wstring(L"TASK\\") + t.folder + L"\\" + t.name; }
    std::wstring EncodeTask(const TaskState& st) { return st.exists ? (st.enabled ? L"task:1" : L"task:0") : L"absent"; }
    bool DecodeTask(const std::wstring& s, TaskState& st) {
        st = TaskState{};
        if (s == L"absent") return true;
        if (s != L"task:1" && s != L"task:0") return false;
        st.exists = true;
        st.enabled = s == L"task:1";
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
    for (const TaskOp& t : s.tasks)
        if (ReadTask(t).enabled) return false;                      // fehlt die Aufgabe, gibt es nichts abzuschalten
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
    for (const TaskOp& t : s.tasks) {   // und den Zustand der geplanten Aufgaben
        std::wstring ignore;
        if (ReadBackup(TaskId(t), ignore)) continue;
        const std::wstring before = EncodeTask(ReadTask(t));
        if (!WriteBackup(TaskId(t), before)) { error = L"Die Sicherungsdatei kann nicht geschrieben werden oder der Ordner ist nicht vertrauenswürdig (%ProgramData%\\DuncansSuite\\Backups muss Administratoren gehören)."; return false; }
        suite_safety::Log(L"PrivacyShield", L"Sicherung " + TaskId(t) + L" = " + before);
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
    for (const TaskOp& t : s.tasks) {            // geplante Aufgaben abschalten
        const TaskState st = ReadTask(t);
        if (!st.exists) { suite_safety::Log(L"PrivacyShield", std::wstring(L"Aufgabe nicht vorhanden: ") + t.name); continue; }
        if (!st.enabled) continue;
        if (WriteTask(t, false, error)) suite_safety::Log(L"PrivacyShield", L"Aufgabe abgeschaltet: " + TaskId(t) + L" (ok)");
        else {
            suite_safety::Log(L"PrivacyShield", L"FEHLER beim Abschalten der Aufgabe " + TaskId(t) + L": " + error + L" – Einstellung wird zurückgenommen");
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
    for (const TaskOp& t : s.tasks) {
        std::wstring enc;
        TaskState old;
        if (!ReadBackup(TaskId(t), enc) || !DecodeTask(enc, old)) continue;  // nie angewendet
        const TaskState now = ReadTask(t);
        if (!old.exists || !now.exists || now.enabled == old.enabled) { DropBackup(TaskId(t)); continue; }
        std::wstring e2;
        const bool done = WriteTask(t, old.enabled, e2);
        suite_safety::Log(L"PrivacyShield", L"Wiederhergestellt " + TaskId(t) + L" = " + enc + (done ? L" (ok)" : L" FEHLER: " + e2));
        if (!done) { ok = false; error = e2; continue; }
        DropBackup(TaskId(t));
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
        for (const TaskOp& t : s.tasks) { std::wstring e; if (ReadBackup(TaskId(t), e)) had = true; }
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
        { 31, L"Telemetrie & Diagnosedaten", L"Geplante Telemetrie-Aufgaben abschalten",
          L"Schaltet die Windows-Aufgaben ab, die auch bei minimierter Telemetrie weiter Daten sammeln: Kompatibilitätsprüfung (Appraiser), Programm-Datenupdater, Programm-Erfahrungsverbesserung (CEIP), Feedback-Erinnerungen und die Datenträger-Diagnosesammlung.",
          {}, {}, {}, false, false,
          { { L"\\Microsoft\\Windows\\Application Experience", L"Microsoft Compatibility Appraiser" },
            { L"\\Microsoft\\Windows\\Application Experience", L"ProgramDataUpdater" },
            { L"\\Microsoft\\Windows\\Application Experience", L"StartupAppTask" },
            { L"\\Microsoft\\Windows\\Application Experience", L"Microsoft Compatibility Appraiser Exp" },
            { L"\\Microsoft\\Windows\\Application Experience", L"MareBackup" },
            { L"\\Microsoft\\Windows\\Customer Experience Improvement Program", L"Consolidator" },
            { L"\\Microsoft\\Windows\\Customer Experience Improvement Program", L"UsbCeip" },
            { L"\\Microsoft\\Windows\\Customer Experience Improvement Program", L"KernelCeipTask" },
            { L"\\Microsoft\\Windows\\Feedback\\Siuf", L"DmClient" },
            { L"\\Microsoft\\Windows\\Feedback\\Siuf", L"DmClientOnScenarioDownload" },
            { L"\\Microsoft\\Windows\\DiskDiagnostic", L"Microsoft-Windows-DiskDiagnosticDataCollector" },
            { L"\\Microsoft\\Windows\\Autochk", L"Proxy" } } },
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
        { 25, L"Werbung & Vorschläge", L"Vorinstallierte Werbe-Apps verhindern",
          L"Windows installiert nach Updates keine Werbe-Apps (z. B. Candy Crush, TikTok) und keine „vorgeschlagenen“ Apps mehr nach. Schon installierte Apps bleiben unverändert.",
          { { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\CloudContent", L"DisableWindowsConsumerFeatures", ValType::Dword, 1, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"SilentInstalledAppsEnabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"PreInstalledAppsEnabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"OemPreInstalledAppsEnabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"ContentDeliveryAllowed", ValType::Dword, 0, nullptr } }, {} },
        { 26, L"Werbung & Vorschläge", L"Copilot-Schaltfläche & systemweite Copilot-Richtlinie",
          L"Blendet die Copilot-Schaltfläche in der Taskleiste aus und setzt die Copilot-Richtlinie zusätzlich für alle Benutzer des PCs.",
          { { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsCopilot", L"TurnOffWindowsCopilot", ValType::Dword, 1, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"ShowCopilotButton", ValType::Dword, 0, nullptr, true } }, {} },
        { 27, L"Werbung & Vorschläge", L"Werbung auf Sperrbildschirm & Startmenü aus",
          L"Schaltet Windows Spotlight mit Tipps und „Fun Facts“ auf dem Sperrbildschirm sowie vorgeschlagene Apps und Kontohinweise im Startmenü ab.",
          { { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"RotatingLockScreenOverlayEnabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"SubscribedContent-338387Enabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"SubscribedContent-338389Enabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"SubscribedContent-88000326Enabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"Start_IrisRecommendations", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"Start_AccountNotifications", ValType::Dword, 0, nullptr } }, {} },
        { 28, L"Werbung & Vorschläge", L"Vorschläge in Einstellungen & „Gerät fertig einrichten“ aus",
          L"Blendet Werbe-Vorschläge in der Einstellungen-App und die Hinweise „Richte dein Gerät fertig ein“ bzw. den Willkommensbildschirm nach Updates aus.",
          { { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"SubscribedContent-338393Enabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"SubscribedContent-353694Enabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"SubscribedContent-353696Enabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"SystemPaneSuggestionsEnabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"SoftLandingEnabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\UserProfileEngagement", L"ScoobeSystemSettingEnabled", ValType::Dword, 0, nullptr } }, {} },
        { 29, L"Werbung & Vorschläge", L"Werbung im Datei-Explorer aus",
          L"Der Explorer zeigt keine Hinweise und Angebote zu OneDrive und Microsoft 365 mehr an (Anbieter-Benachrichtigungen aus).",
          { { Hive::User, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"ShowSyncProviderNotifications", ValType::Dword, 0, nullptr } }, {} },
        { 30, L"Werbung & Vorschläge", L"Edge: Shopping, Seitenleiste & Rewards aus",
          L"Schaltet den Shopping-Assistenten, die Seitenleiste, Microsoft Rewards, „Folgen“ und die Personalisierungs-Berichte in Microsoft Edge per Richtlinie ab.",
          { { Hive::User, L"SOFTWARE\\Policies\\Microsoft\\Edge", L"EdgeShoppingAssistantEnabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Policies\\Microsoft\\Edge", L"HubsSidebarEnabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Policies\\Microsoft\\Edge", L"ShowMicrosoftRewards", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Policies\\Microsoft\\Edge", L"EdgeFollowEnabled", ValType::Dword, 0, nullptr },
            { Hive::User, L"SOFTWARE\\Policies\\Microsoft\\Edge", L"PersonalizationReportingEnabled", ValType::Dword, 0, nullptr } }, {} },
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
        { 32, L"App-Berechtigungen", L"Kamerazugriff für Apps sperren",
          L"Kein Programm und keine App darf die Kamera benutzen (Richtlinie „Apps dürfen auf die Kamera zugreifen“ = nicht zulassen und globaler Kameraschalter aus). Videoanrufe, Webcam-Programme und Windows Hello brauchen die Kamera.",
          { { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", L"LetAppsAccessCamera", ValType::Dword, 2, nullptr },
            { Hive::Machine, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\webcam", L"Value", ValType::String, 0, L"Deny" } }, {}, {}, true },
        { 33, L"App-Berechtigungen", L"Mikrofonzugriff für Apps sperren",
          L"Kein Programm und keine App darf das Mikrofon benutzen (Richtlinie und globaler Mikrofonschalter). Anrufe, Sprachchat, Diktat und Aufnahmen funktionieren dann nicht.",
          { { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", L"LetAppsAccessMicrophone", ValType::Dword, 2, nullptr },
            { Hive::Machine, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone", L"Value", ValType::String, 0, L"Deny" } }, {}, {}, true },
        { 34, L"App-Berechtigungen", L"Persönliche Daten für Apps sperren",
          L"Apps dürfen Kontakte, Kalender, E-Mail, Nachrichten, Anrufliste und Kontoinformationen nicht mehr lesen (Richtlinien und globale Schalter). Die Store-Apps Mail und Kalender sowie Telefon-Verknüpfung zeigen dann keine Daten.",
          { { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", L"LetAppsAccessContacts", ValType::Dword, 2, nullptr },
            { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", L"LetAppsAccessCalendar", ValType::Dword, 2, nullptr },
            { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", L"LetAppsAccessEmail", ValType::Dword, 2, nullptr },
            { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", L"LetAppsAccessMessaging", ValType::Dword, 2, nullptr },
            { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", L"LetAppsAccessCallHistory", ValType::Dword, 2, nullptr },
            { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", L"LetAppsAccessAccountInfo", ValType::Dword, 2, nullptr },
            { Hive::Machine, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\contacts", L"Value", ValType::String, 0, L"Deny" },
            { Hive::Machine, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\appointments", L"Value", ValType::String, 0, L"Deny" },
            { Hive::Machine, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\email", L"Value", ValType::String, 0, L"Deny" },
            { Hive::Machine, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\chat", L"Value", ValType::String, 0, L"Deny" },
            { Hive::Machine, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\phoneCallHistory", L"Value", ValType::String, 0, L"Deny" },
            { Hive::Machine, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\userAccountInformation", L"Value", ValType::String, 0, L"Deny" } }, {}, {}, true },
        { 35, L"App-Berechtigungen", L"Diagnoseinformationen anderer Apps nicht freigeben",
          L"Apps dürfen nicht auslesen, welche anderen Apps laufen und was sie tun (Richtlinie „Apps dürfen Diagnoseinformationen anderer Apps abrufen“ = nicht zulassen).",
          { { Hive::Machine, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", L"LetAppsGetDiagnosticInfo", ValType::Dword, 2, nullptr },
            { Hive::Machine, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\appDiagnostics", L"Value", ValType::String, 0, L"Deny" } }, {} },
    };
    return all;
}

// ---------- Schutz erneuern ----------
namespace {
    std::wstring g_wantedPath;

    std::wstring WantedPath() {
        if (!g_wantedPath.empty()) return g_wantedPath;
        const std::wstring dir = suite_safety::DataDir();     // "" bei nicht vertrauenswuerdigem Ordner
        if (dir.empty()) return L"";
        const std::wstring path = dir + L"\\privacy_wanted.ini";
        if (!suite_safety::EnsureTrustedFile(path)) return L"";
        g_wantedPath = path;
        return path;
    }

    const wchar_t* const kTaskName = L"Duncans Privacy Shield - Schutz erneuern";

    std::wstring XmlEscape(const std::wstring& s) {
        std::wstring o;
        for (wchar_t c : s) {
            if (c == L'&') o += L"&amp;"; else if (c == L'<') o += L"&lt;"; else if (c == L'>') o += L"&gt;";
            else if (c == L'"') o += L"&quot;"; else o += c;
        }
        return o;
    }

    // Startet ein Programm ohne Fenster und wartet; false, wenn es nicht gestartet werden konnte
    bool RunHidden(const std::wstring& cmdLine, DWORD timeoutMs, DWORD& exitCode) {
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring cmd = cmdLine;
        if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
        const DWORD w = WaitForSingleObject(pi.hProcess, timeoutMs);
        exitCode = 1;
        if (w == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &exitCode);
        else TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return w == WAIT_OBJECT_0;
    }

    std::wstring SchtasksPath() {
        wchar_t sys[MAX_PATH] = {};
        GetSystemDirectoryW(sys, MAX_PATH);
        return std::wstring(L"\"") + sys + L"\\schtasks.exe\"";
    }
}

void SetWantedPathForTest(const std::wstring& path) { g_wantedPath = path; }

bool HasBackupFor(const Setting& s) {
    std::wstring ignore;
    for (const RegOp& op : s.ops) if (ReadBackup(OpId(op), ignore)) return true;
    for (const ServiceOp& so : s.services) if (ReadBackup(SvcId(so), ignore)) return true;
    for (const TaskOp& t : s.tasks) if (ReadBackup(TaskId(t), ignore)) return true;
    return false;
}

bool HasWantedFile() {
    const std::wstring p = WantedPath();
    return !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::vector<int> LoadWanted() {
    std::vector<int> ids;
    const std::wstring p = WantedPath();
    if (p.empty()) return ids;
    wchar_t buf[2048] = {};
    GetPrivateProfileStringW(L"Wanted", L"Ids", L"", buf, 2048, p.c_str());
    std::wstringstream ss(buf);
    std::wstring tok;
    while (std::getline(ss, tok, L',')) {
        const int v = _wtoi(tok.c_str());
        if (v > 0 && std::find(ids.begin(), ids.end(), v) == ids.end()) ids.push_back(v);
    }
    return ids;
}

bool SaveWanted(const std::vector<int>& ids) {
    const std::wstring p = WantedPath();
    if (p.empty()) return false;
    std::wstring v;
    for (int id : ids) v += (v.empty() ? L"" : L",") + std::to_wstring(id);
    return WritePrivateProfileStringW(L"Wanted", L"Ids", v.c_str(), p.c_str()) != FALSE;
}

void MarkWanted(int settingId, bool on) {
    std::vector<int> ids = LoadWanted();
    const auto it = std::find(ids.begin(), ids.end(), settingId);
    if (on && it == ids.end()) ids.push_back(settingId);
    else if (!on && it != ids.end()) ids.erase(it);
    else return;
    SaveWanted(ids);
}

void SeedWantedFromBackup() {
    if (HasWantedFile()) return;
    std::vector<int> ids;
    for (const Setting& s : AllSettings()) if (HasBackupFor(s)) ids.push_back(s.id);
    SaveWanted(ids);
}

std::vector<int> MissingWanted() {
    std::vector<int> missing;
    for (int id : LoadWanted())
        for (const Setting& s : AllSettings())
            if (s.id == id) { if (!IsApplied(s)) missing.push_back(id); break; }
    return missing;
}

int ReapplyMissing(std::vector<std::wstring>& titles, std::vector<std::wstring>& errors) {
    int done = 0;
    for (int id : MissingWanted())
        for (const Setting& s : AllSettings())
            if (s.id == id) {
                std::wstring err;
                if (Apply(s, err)) { ++done; titles.push_back(s.title); }
                else errors.push_back(std::wstring(s.title) + L": " + err);
                break;
            }
    return done;
}

std::wstring WatchExePath() {
    const std::wstring dir = suite_safety::DataDir();
    return dir.empty() ? std::wstring() : dir + L"\\watch\\DuncansPrivacy.exe";
}

std::wstring WatchTaskXml(const std::wstring& exePath, const std::wstring& user) {
    const std::wstring u = XmlEscape(user);
    return std::wstring(L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n")
        + L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
        + L"  <RegistrationInfo><Description>Duncan's Privacy Shield: stellt Datenschutz-Einstellungen wieder her, die Windows zurueckgesetzt hat.</Description></RegistrationInfo>\r\n"
        + L"  <Triggers><LogonTrigger><Enabled>true</Enabled><UserId>" + u + L"</UserId><Delay>PT30S</Delay></LogonTrigger></Triggers>\r\n"
        + L"  <Principals><Principal id=\"Author\"><UserId>" + u + L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>\r\n"
        + L"  <Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
        + L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><ExecutionTimeLimit>PT5M</ExecutionTimeLimit><Enabled>true</Enabled></Settings>\r\n"
        + L"  <Actions Context=\"Author\"><Exec><Command>" + XmlEscape(exePath) + L"</Command><Arguments>--reapply</Arguments></Exec></Actions>\r\n"
        + L"</Task>\r\n";
}

bool WatchTaskExists() {
    DWORD code = 1;
    return RunHidden(SchtasksPath() + L" /Query /TN \"" + kTaskName + L"\"", 15000, code) && code == 0;
}

bool EnableWatchTask(const std::wstring& currentExe, std::wstring& error) {
    const std::wstring dir = suite_safety::DataDir();
    if (dir.empty()) { error = L"Der Datenordner unter %ProgramData% ist nicht vertrauenswürdig."; return false; }
    const std::wstring watchDir = dir + L"\\watch";
    CreateDirectoryW(watchDir.c_str(), nullptr);
    const std::wstring target = WatchExePath();
    if (!suite_safety::EnsureTrustedFile(target)) { error = L"Die Programmkopie im Datenordner ist nicht vertrauenswürdig."; return false; }
    if (_wcsicmp(currentExe.c_str(), target.c_str()) != 0 && !CopyFileW(currentExe.c_str(), target.c_str(), FALSE)) {
        error = L"Die Programmkopie konnte nicht angelegt werden (Fehler " + std::to_wstring(GetLastError()) + L").";
        return false;
    }
    wchar_t user[256] = {}, domain[256] = {};
    GetEnvironmentVariableW(L"USERNAME", user, 256);
    GetEnvironmentVariableW(L"USERDOMAIN", domain, 256);
    const std::wstring who = std::wstring(domain) + L"\\" + user;
    const std::wstring xmlPath = watchDir + L"\\task.xml";
    {
        const std::wstring xml = WatchTaskXml(target, who);
        HANDLE h = CreateFileW(xmlPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) { error = L"Die Aufgabenbeschreibung konnte nicht geschrieben werden."; return false; }
        const WORD bom = 0xFEFF;
        DWORD n = 0;
        const BOOL ok = WriteFile(h, &bom, 2, &n, nullptr) && WriteFile(h, xml.data(), static_cast<DWORD>(xml.size() * sizeof(wchar_t)), &n, nullptr);
        CloseHandle(h);
        if (!ok) { error = L"Die Aufgabenbeschreibung konnte nicht geschrieben werden."; return false; }
    }
    DWORD code = 1;
    const bool ran = RunHidden(SchtasksPath() + L" /Create /TN \"" + kTaskName + L"\" /XML \"" + xmlPath + L"\" /F", 30000, code);
    DeleteFileW(xmlPath.c_str());
    if (!ran || code != 0) { error = L"Die geplante Aufgabe konnte nicht angelegt werden (Code " + std::to_wstring(code) + L")."; return false; }
    suite_safety::Log(L"PrivacyShield", L"Geplante Aufgabe angelegt: " + std::wstring(kTaskName) + L" -> " + target);
    return true;
}

bool DisableWatchTask(std::wstring& error) {
    if (!WatchTaskExists()) return true;
    DWORD code = 1;
    if (!RunHidden(SchtasksPath() + L" /Delete /TN \"" + kTaskName + L"\" /F", 30000, code) || code != 0) {
        error = L"Die geplante Aufgabe konnte nicht entfernt werden (Code " + std::to_wstring(code) + L").";
        return false;
    }
    suite_safety::Log(L"PrivacyShield", L"Geplante Aufgabe entfernt: " + std::wstring(kTaskName));
    return true;
}

void RefreshWatchCopy(const std::wstring& currentExe) {
    const std::wstring target = WatchExePath();
    if (target.empty() || _wcsicmp(currentExe.c_str(), target.c_str()) == 0) return;
    WIN32_FILE_ATTRIBUTE_DATA a{}, b{};
    if (!GetFileAttributesExW(target.c_str(), GetFileExInfoStandard, &a)) return;       // keine Kopie: Aufgabe ist nicht eingerichtet
    if (!GetFileAttributesExW(currentExe.c_str(), GetFileExInfoStandard, &b)) return;
    if (a.nFileSizeLow == b.nFileSizeLow && a.nFileSizeHigh == b.nFileSizeHigh) return;
    if (!suite_safety::EnsureTrustedFile(target)) return;
    CopyFileW(currentExe.c_str(), target.c_str(), FALSE);
}


}  // namespace privacy
