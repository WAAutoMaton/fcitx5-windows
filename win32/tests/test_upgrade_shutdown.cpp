#include "../setup/upgrade.h"
#include <cassert>
#include <iostream>
#include <sddl.h>
#include <winsvc.h>

namespace {
bool exists = false, secured = false, running = false;
std::wstring command;
DWORD starts = 0, stops = 0, removals = 0, timeout = 0;
LPHANDLER_FUNCTION_EX handler = nullptr;
unsigned waits = 0;
bool cancelled = false;
unsigned suspends = 0, resumes = 0;
SC_HANDLE managerHandle = reinterpret_cast<SC_HANDLE>(1);
SC_HANDLE serviceHandle = reinterpret_cast<SC_HANDLE>(2);

SC_HANDLE WINAPI openManager(LPCWSTR, LPCWSTR, DWORD access) {
    assert(access & SC_MANAGER_CONNECT);
    return managerHandle;
}
SC_HANDLE WINAPI openService(SC_HANDLE manager, LPCWSTR name, DWORD) {
    assert(manager == managerHandle &&
           std::wstring_view(name) == L"Fcitx5-UpgradeShutdown");
    SetLastError(exists ? ERROR_SUCCESS : ERROR_SERVICE_DOES_NOT_EXIST);
    return exists ? serviceHandle : nullptr;
}
SC_HANDLE WINAPI createService(SC_HANDLE, LPCWSTR, LPCWSTR, DWORD, DWORD type,
                               DWORD start, DWORD, LPCWSTR binary, LPCWSTR,
                               LPDWORD, LPCWSTR dependencies, LPCWSTR user,
                               LPCWSTR password) {
    assert(type == SERVICE_WIN32_OWN_PROCESS && start == SERVICE_AUTO_START);
    assert(std::wstring_view(user) == L"LocalSystem" && !password &&
           !dependencies);
    command = binary;
    exists = true;
    return serviceHandle;
}
BOOL WINAPI closeService(SC_HANDLE) { return TRUE; }
BOOL WINAPI queryConfig(SC_HANDLE, LPQUERY_SERVICE_CONFIGW value, DWORD size,
                        LPDWORD bytes) {
    *bytes = sizeof(QUERY_SERVICE_CONFIGW);
    if (size < *bytes) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    value->dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    value->dwStartType = SERVICE_AUTO_START;
    value->lpBinaryPathName = command.data();
    value->lpServiceStartName = const_cast<LPWSTR>(L"LocalSystem");
    return TRUE;
}
BOOL WINAPI secureService(SC_HANDLE, SECURITY_INFORMATION info,
                          PSECURITY_DESCRIPTOR descriptor) {
    assert(info == DACL_SECURITY_INFORMATION);
    BOOL present = FALSE, defaulted = TRUE;
    PACL acl = nullptr;
    assert(GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted));
    assert(present && acl && acl->AceCount == 2);
    for (DWORD index = 0; index < 2; ++index) {
        void *raw = nullptr;
        assert(GetAce(acl, index, &raw));
        auto *ace = static_cast<ACCESS_ALLOWED_ACE *>(raw);
        const auto sid = const_cast<DWORD *>(&ace->SidStart);
        assert(IsWellKnownSid(sid, WinLocalSystemSid) ||
               IsWellKnownSid(sid, WinBuiltinAdministratorsSid));
    }
    secured = true;
    return TRUE;
}
BOOL WINAPI changeConfig(SC_HANDLE, DWORD level, LPVOID value) {
    if (level == SERVICE_CONFIG_PRESHUTDOWN_INFO)
        timeout = static_cast<SERVICE_PRESHUTDOWN_INFO *>(value)
                      ->dwPreshutdownTimeout;
    else {
        assert(level == SERVICE_CONFIG_FAILURE_ACTIONS);
        const auto *recovery = static_cast<SERVICE_FAILURE_ACTIONSW *>(value);
        assert(recovery->cActions == 3 &&
               recovery->lpsaActions[0].Type == SC_ACTION_RESTART);
        assert(recovery->lpsaActions[2].Type == SC_ACTION_NONE);
    }
    return TRUE;
}
BOOL WINAPI startService(SC_HANDLE, DWORD, LPCWSTR *) {
    ++starts;
    if (running) {
        SetLastError(ERROR_SERVICE_ALREADY_RUNNING);
        return FALSE;
    }
    running = true;
    return TRUE;
}
BOOL WINAPI queryStatus(SC_HANDLE, SC_STATUS_TYPE, LPBYTE bytes, DWORD,
                        LPDWORD needed) {
    auto *value = reinterpret_cast<SERVICE_STATUS_PROCESS *>(bytes);
    value->dwCurrentState = running ? SERVICE_RUNNING : SERVICE_STOPPED;
    *needed = sizeof(*value);
    return TRUE;
}
BOOL WINAPI stopService(SC_HANDLE, DWORD code, LPSERVICE_STATUS) {
    assert(code == SERVICE_CONTROL_STOP);
    ++stops;
    running = false;
    return TRUE;
}
BOOL WINAPI deleteService(SC_HANDLE) {
    ++removals;
    exists = false;
    return TRUE;
}
SERVICE_STATUS_HANDLE WINAPI registerHandler(LPCWSTR,
                                             LPHANDLER_FUNCTION_EX callback,
                                             LPVOID) {
    handler = callback;
    return reinterpret_cast<SERVICE_STATUS_HANDLE>(3);
}
BOOL WINAPI reportStatus(SERVICE_STATUS_HANDLE, LPSERVICE_STATUS value) {
    if (value->dwCurrentState == SERVICE_RUNNING)
        assert(value->dwControlsAccepted ==
               (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN));
    return TRUE;
}
BOOL WINAPI dispatch(const SERVICE_TABLE_ENTRYW *entries) {
    entries[0].lpServiceProc(0, nullptr);
    return TRUE;
}
DWORD WINAPI waitEvent(HANDLE, DWORD) {
    if (cancelled && waits++ < 2)
        return WAIT_TIMEOUT;
    assert(
        handler(cancelled ? SERVICE_CONTROL_STOP : SERVICE_CONTROL_PRESHUTDOWN,
                0, nullptr, nullptr) == 0);
    return WAIT_OBJECT_0;
}
ULONGLONG WINAPI ticks() { return waits == 1 ? 1000 : 62000; }
int WINAPI metrics(int index) {
    assert(index == SM_SHUTTINGDOWN);
    return 0;
}
} // namespace

namespace fcitx::setup::upgrade {
std::string fakeState(const fs::path &) {
    return resumes ? "staged" : "suspended";
}
DWORD fakeUpgrade(const fs::path &, std::wstring_view action, const fs::path &,
                  std::string_view id, bool preview, bool silent) {
    assert(!id.empty() && !preview && silent);
    if (action == L"suspend-upgrade")
        ++suspends;
    else {
        assert(action == L"resume-upgrade");
        ++resumes;
    }
    return ERROR_SUCCESS;
}
} // namespace fcitx::setup::upgrade

#define OpenSCManagerW openManager
#define OpenServiceW openService
#define CreateServiceW createService
#define CloseServiceHandle closeService
#define QueryServiceConfigW queryConfig
#define SetServiceObjectSecurity secureService
#define ChangeServiceConfig2W changeConfig
#define StartServiceW startService
#define QueryServiceStatusEx queryStatus
#define ControlService stopService
#define DeleteService deleteService
#define RegisterServiceCtrlHandlerExW registerHandler
#define SetServiceStatus reportStatus
#define StartServiceCtrlDispatcherW dispatch
#define WaitForSingleObject waitEvent
#define GetTickCount64 ticks
#define GetSystemMetrics metrics
#define transactionState fakeState
#define runUpgrade fakeUpgrade
#include "../setup/upgrade_shutdown.cpp"

int main() {
    namespace upgrade = fcitx::setup::upgrade;
    const upgrade::fs::path root = L"C:\\Fcitx5 test";
    const std::string id = "{37FD1918-C8EF-48DD-80E0-361157A011B8}";
    upgrade::configureShutdownService(root, true, id);
    assert(exists && secured && running && starts == 1 && timeout == 60000);
    assert(command.find(L"--transaction " + upgrade::wide(id)) !=
           std::wstring::npos);
    assert(
        command.find(
            L"\"C:\\Fcitx5 test\\setup/maintenance-host/Fcitx5Upgrade.exe\"") ==
        0);
    upgrade::configureShutdownService(root, true, id);
    assert(starts == 2);
    const auto original = command;
    command = L"C:\\Other.exe";
    try {
        upgrade::configureShutdownService(root, false, id);
        assert(false);
    } catch (const std::system_error &error) {
        assert(error.code().value() == ERROR_ALREADY_EXISTS);
    }
    assert(exists && removals == 0);
    command = original;
    upgrade::configureShutdownService(root, false, id);
    assert(!exists && stops == 1 && removals == 1);
    upgrade::configureShutdownService(root, false, id);
    assert(removals == 1);
    assert(upgrade::runShutdownService(root, id) == 0);
    assert(suspends == 1 && resumes == 0);
    cancelled = true;
    upgrade::stopReason = 0;
    assert(upgrade::runShutdownService(root, id) == 0);
    assert(resumes == 1 && suspends == 1);
    std::cout
        << "PASS: temporary SYSTEM service ownership, ACL, preshutdown, "
           "cancelled restart restore and cleanup without SCM mutations\n";
}
