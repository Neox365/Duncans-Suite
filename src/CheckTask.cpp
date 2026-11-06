#include "CheckTask.h"

#include "SuiteSafety.h"

namespace drvcheck {

namespace {
    const wchar_t* const kTaskName = L"Duncans Driver Updater - Treiber pruefen";

    std::wstring XmlEscape(const std::wstring& s) {
        std::wstring o;
        for (wchar_t c : s) {
            if (c == L'&') o += L"&amp;"; else if (c == L'<') o += L"&lt;"; else if (c == L'>') o += L"&gt;";
            else if (c == L'"') o += L"&quot;"; else o += c;
        }
        return o;
    }

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

const wchar_t* TaskName() { return kTaskName; }

std::wstring CopyPath() {
    const std::wstring dir = suite_safety::DataDir();     // "" bei nicht vertrauenswuerdigem Ordner
    return dir.empty() ? std::wstring() : dir + L"\\watch\\DuncansDriverUpdater.exe";
}

std::wstring TaskXml(const std::wstring& exePath, const std::wstring& user) {
    const std::wstring u = XmlEscape(user);
    return std::wstring(L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n")
        + L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
        + L"  <RegistrationInfo><Description>Duncan's Driver Updater: sucht woechentlich im Hintergrund nach Treiber-Updates und meldet Treffer.</Description></RegistrationInfo>\r\n"
        + L"  <Triggers><CalendarTrigger><StartBoundary>2026-01-05T12:00:00</StartBoundary><Enabled>true</Enabled>"
        + L"<ScheduleByWeek><DaysOfWeek><Monday/></DaysOfWeek><WeeksInterval>1</WeeksInterval></ScheduleByWeek></CalendarTrigger></Triggers>\r\n"
        + L"  <Principals><Principal id=\"Author\"><UserId>" + u + L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>\r\n"
        + L"  <Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
        + L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><StartWhenAvailable>true</StartWhenAvailable><RunOnlyIfNetworkAvailable>true</RunOnlyIfNetworkAvailable>"
        + L"<ExecutionTimeLimit>PT30M</ExecutionTimeLimit><Enabled>true</Enabled></Settings>\r\n"
        + L"  <Actions Context=\"Author\"><Exec><Command>" + XmlEscape(exePath) + L"</Command><Arguments>--check</Arguments></Exec></Actions>\r\n"
        + L"</Task>\r\n";
}

bool TaskExists() {
    DWORD code = 1;
    return RunHidden(SchtasksPath() + L" /Query /TN \"" + kTaskName + L"\"", 15000, code) && code == 0;
}

bool Enable(const std::wstring& currentExe, std::wstring& error) {
    const std::wstring dir = suite_safety::DataDir();
    if (dir.empty()) { error = L"Der Datenordner unter %ProgramData% ist nicht vertrauenswürdig."; return false; }
    const std::wstring watchDir = dir + L"\\watch";
    CreateDirectoryW(watchDir.c_str(), nullptr);
    const std::wstring target = CopyPath();
    if (!suite_safety::EnsureTrustedFile(target)) { error = L"Die Programmkopie im Datenordner ist nicht vertrauenswürdig."; return false; }
    if (_wcsicmp(currentExe.c_str(), target.c_str()) != 0 && !CopyFileW(currentExe.c_str(), target.c_str(), FALSE)) {
        error = L"Die Programmkopie konnte nicht angelegt werden (Fehler " + std::to_wstring(GetLastError()) + L").";
        return false;
    }
    wchar_t user[256] = {}, domain[256] = {};
    GetEnvironmentVariableW(L"USERNAME", user, 256);
    GetEnvironmentVariableW(L"USERDOMAIN", domain, 256);
    const std::wstring xmlPath = watchDir + L"\\check_task.xml";
    {
        const std::wstring xml = TaskXml(target, std::wstring(domain) + L"\\" + user);
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
    suite_safety::Log(L"DriverUpdater", L"Geplante Aufgabe angelegt: " + std::wstring(kTaskName) + L" -> " + target);
    return true;
}

bool Disable(std::wstring& error) {
    if (!TaskExists()) return true;
    DWORD code = 1;
    if (!RunHidden(SchtasksPath() + L" /Delete /TN \"" + kTaskName + L"\" /F", 30000, code) || code != 0) {
        error = L"Die geplante Aufgabe konnte nicht entfernt werden (Code " + std::to_wstring(code) + L").";
        return false;
    }
    suite_safety::Log(L"DriverUpdater", L"Geplante Aufgabe entfernt: " + std::wstring(kTaskName));
    return true;
}

void RefreshCopy(const std::wstring& currentExe) {
    const std::wstring target = CopyPath();
    if (target.empty() || _wcsicmp(currentExe.c_str(), target.c_str()) == 0) return;
    WIN32_FILE_ATTRIBUTE_DATA a{}, b{};
    if (!GetFileAttributesExW(target.c_str(), GetFileExInfoStandard, &a)) return;       // keine Kopie: Aufgabe ist nicht eingerichtet
    if (!GetFileAttributesExW(currentExe.c_str(), GetFileExInfoStandard, &b)) return;
    if (a.nFileSizeLow == b.nFileSizeLow && a.nFileSizeHigh == b.nFileSizeHigh) return;
    if (!suite_safety::EnsureTrustedFile(target)) return;
    CopyFileW(currentExe.c_str(), target.c_str(), FALSE);
}

}  // namespace drvcheck
