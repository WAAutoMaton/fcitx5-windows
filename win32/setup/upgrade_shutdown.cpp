#include "log.h"
#include "upgrade.h"
#include <atomic>
#include <sddl.h>
#include <winsvc.h>

namespace fcitx::setup::upgrade {
namespace {
constexpr wchar_t kShutdownService[] = L"Fcitx5-UpgradeShutdown";

class ServiceHandle {
  public:
    explicit ServiceHandle(SC_HANDLE value) : value_(value) {}
    ~ServiceHandle() {
        if (value_)
            CloseServiceHandle(value_);
    }
    operator SC_HANDLE() const { return value_; }

  private:
    SC_HANDLE value_;
};

std::wstring serviceCommand(const fs::path &root, std::string_view id) {
    GUID guid{};
    if (id.size() != 38 || FAILED(CLSIDFromString(wide(id).c_str(), &guid)))
        fail(ERROR_INVALID_DATA);
    return L"\"" +
           (root / "setup/maintenance-host/Fcitx5Upgrade.exe").wstring() +
           L"\" upgrade-shutdown-service --prefix \"" + root.wstring() +
           L"\" --transaction " + wide(id) + L" --silent";
}

void checkService(SC_HANDLE service, const std::wstring &command) {
    DWORD bytes = 0;
    QueryServiceConfigW(service, nullptr, 0, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes > 64 * 1024)
        fail(ERROR_INVALID_DATA);
    std::vector<BYTE> buffer(bytes);
    auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data());
    if (!QueryServiceConfigW(service, config, bytes, &bytes))
        fail();
    if (config->dwServiceType != SERVICE_WIN32_OWN_PROCESS ||
        config->dwStartType != SERVICE_AUTO_START ||
        std::wstring_view(config->lpBinaryPathName) != command ||
        _wcsicmp(config->lpServiceStartName, L"LocalSystem") != 0)
        fail(ERROR_ALREADY_EXISTS);
}

SERVICE_STATUS_HANDLE statusHandle = nullptr;
Handle controlEvent;
std::atomic<DWORD> stopReason{0};
fs::path serviceRoot;
std::string serviceTransaction;

void status(DWORD state, DWORD error = ERROR_SUCCESS) {
    SERVICE_STATUS value{};
    value.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    value.dwCurrentState = state;
    value.dwControlsAccepted =
        state == SERVICE_RUNNING
            ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN
            : 0;
    value.dwWin32ExitCode = error;
    value.dwCheckPoint = state == SERVICE_STOP_PENDING ? 1 : 0;
    value.dwWaitHint = state == SERVICE_STOP_PENDING ? 60000 : 0;
    SetServiceStatus(statusHandle, &value);
}

DWORD WINAPI control(DWORD code, DWORD, void *, void *) {
    if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_PRESHUTDOWN) {
        DWORD expected = 0;
        stopReason.compare_exchange_strong(expected, code);
        SetEvent(controlEvent.get());
        return ERROR_SUCCESS;
    }
    return code == SERVICE_CONTROL_INTERROGATE ? ERROR_SUCCESS
                                               : ERROR_CALL_NOT_IMPLEMENTED;
}

void WINAPI serviceMain(DWORD, LPWSTR *) {
    statusHandle =
        RegisterServiceCtrlHandlerExW(kShutdownService, control, nullptr);
    if (!statusHandle)
        return;
    controlEvent = Handle(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!controlEvent) {
        status(SERVICE_STOPPED, GetLastError());
        return;
    }
    status(SERVICE_RUNNING);
    DWORD error = ERROR_SUCCESS;
    ULONGLONG suspendedAt = 0;
    for (;;) {
        const auto waited = WaitForSingleObject(controlEvent.get(), 1000);
        if (waited == WAIT_OBJECT_0)
            break;
        if (waited != WAIT_TIMEOUT) {
            error = GetLastError();
            break;
        }
        // The immediate-restart choice may be cancelled by another application.
        // Restore activation if Windows never enters shutdown; the staged
        // payload and observer remain available for a later restart.
        try {
            const auto state = transactionState(serviceRoot);
            if (state != "suspended" && state != "suspending") {
                suspendedAt = 0;
                continue;
            }
            const auto now = GetTickCount64();
            if (!suspendedAt)
                suspendedAt = now;
            if (now - suspendedAt >= 60000 &&
                !GetSystemMetrics(SM_SHUTTINGDOWN)) {
                suspendedAt = now;
                const auto restored =
                    runUpgrade(serviceRoot, L"resume-upgrade", {},
                               serviceTransaction, false, true);
                logStep("upgrade restart cancelled restore activation",
                        restored);
                suspendedAt = 0;
            }
        } catch (const std::system_error &failure) {
            logStep("upgrade shutdown observer", DWORD(failure.code().value()));
        } catch (...) {
            logStep("upgrade shutdown observer", ERROR_INSTALL_FAILURE);
        }
    }
    status(SERVICE_STOP_PENDING);
    if (!error && stopReason == SERVICE_CONTROL_PRESHUTDOWN) {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initialized)) {
            error = DWORD(initialized);
        } else {
            try {
                logMessage("upgrade shutdown notification begin");
                error = runUpgrade(serviceRoot, L"suspend-upgrade", {},
                                   serviceTransaction, false, true);
            } catch (const std::system_error &failure) {
                error = DWORD(failure.code().value());
            } catch (...) {
                error = ERROR_INSTALL_FAILURE;
            }
            CoUninitialize();
        }
        logStep("upgrade shutdown notification complete", error);
    }
    status(SERVICE_STOPPED, error);
}
} // namespace

void configureShutdownService(const fs::path &root, bool enable,
                              std::string_view transaction) {
    const auto command = serviceCommand(root, transaction);
    ServiceHandle manager(OpenSCManagerW(
        nullptr, nullptr,
        SC_MANAGER_CONNECT | (enable ? SC_MANAGER_CREATE_SERVICE : 0)));
    if (!manager)
        fail();
    const DWORD access = SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS |
                         SERVICE_START | SERVICE_STOP | SERVICE_CHANGE_CONFIG |
                         DELETE | WRITE_DAC;
    ServiceHandle service(OpenServiceW(manager, kShutdownService, access));
    if (!service) {
        if (GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST)
            fail();
        if (!enable)
            return;
        ServiceHandle created(CreateServiceW(
            manager, kShutdownService,
            L"Fcitx5 pending upgrade shutdown preparation", access,
            SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
            command.c_str(), nullptr, nullptr, nullptr, L"LocalSystem",
            nullptr));
        if (!created)
            fail();
    }
    // Reopen after creation; all mutations are restricted to our exact action,
    // SYSTEM identity and temporary service.
    ServiceHandle opened(OpenServiceW(manager, kShutdownService, access));
    if (!opened)
        fail();
    checkService(opened, command);
    if (!enable) {
        SERVICE_STATUS_PROCESS current{};
        DWORD bytes = 0;
        if (!QueryServiceStatusEx(opened, SC_STATUS_PROCESS_INFO,
                                  reinterpret_cast<BYTE *>(&current),
                                  sizeof(current), &bytes))
            fail();
        if (current.dwCurrentState != SERVICE_STOPPED) {
            SERVICE_STATUS ignored{};
            if (!ControlService(opened, SERVICE_CONTROL_STOP, &ignored) &&
                GetLastError() != ERROR_SERVICE_NOT_ACTIVE)
                fail();
            for (unsigned attempt = 0; attempt < 100; ++attempt) {
                if (!QueryServiceStatusEx(opened, SC_STATUS_PROCESS_INFO,
                                          reinterpret_cast<BYTE *>(&current),
                                          sizeof(current), &bytes))
                    fail();
                if (current.dwCurrentState == SERVICE_STOPPED)
                    break;
                Sleep(100);
            }
            if (current.dwCurrentState != SERVICE_STOPPED)
                fail(ERROR_SERVICE_REQUEST_TIMEOUT);
        }
        if (!DeleteService(opened))
            fail();
        return;
    }
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1, &descriptor,
            nullptr))
        fail();
    const auto secured =
        SetServiceObjectSecurity(opened, DACL_SECURITY_INFORMATION, descriptor);
    const auto securityError = secured ? ERROR_SUCCESS : GetLastError();
    LocalFree(descriptor);
    if (securityError)
        fail(securityError);
    SERVICE_PRESHUTDOWN_INFO timeout{60000};
    if (!ChangeServiceConfig2W(opened, SERVICE_CONFIG_PRESHUTDOWN_INFO,
                               &timeout))
        fail();
    SC_ACTION restart[] = {{SC_ACTION_RESTART, 5000},
                           {SC_ACTION_RESTART, 5000},
                           {SC_ACTION_NONE, 0}};
    SERVICE_FAILURE_ACTIONSW recovery{86400, nullptr, nullptr, 3, restart};
    if (!ChangeServiceConfig2W(opened, SERVICE_CONFIG_FAILURE_ACTIONS,
                               &recovery))
        fail();
    if (!StartServiceW(opened, 0, nullptr) &&
        GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
        fail();
}

DWORD runShutdownService(const fs::path &root, std::string_view transaction) {
    // Validate argument shape before entering the SCM dispatcher. Actual
    // transaction identity and machine ownership are checked at suspension.
    serviceCommand(root, transaction);
    serviceRoot = root;
    serviceTransaction = transaction;
    SERVICE_TABLE_ENTRYW entries[] = {
        {const_cast<LPWSTR>(kShutdownService), serviceMain},
        {nullptr, nullptr}};
    return StartServiceCtrlDispatcherW(entries) ? ERROR_SUCCESS
                                                : GetLastError();
}
} // namespace fcitx::setup::upgrade
