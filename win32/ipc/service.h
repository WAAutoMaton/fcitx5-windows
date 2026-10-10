#pragma once

#include "atomicfile.h"
#include "installation.h"
#include "transport.h"
#include <exception>
#include <filesystem>
#include <ntsecapi.h>
#include <stdexcept>
#include <system_error>

namespace fcitx::win32::ipc {

class ServiceHandle {
  public:
    explicit ServiceHandle(HANDLE value = nullptr) : value_(value) {}
    ~ServiceHandle() {
        if (value_ && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }
    ServiceHandle(const ServiceHandle &) = delete;
    ServiceHandle &operator=(const ServiceHandle &) = delete;
    HANDLE get() const { return value_; }

  private:
    HANDLE value_;
};

[[noreturn]] inline void serviceError(DWORD error = GetLastError()) {
    throw std::system_error(static_cast<int>(error), std::system_category());
}

inline std::wstring serviceIdentity() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        serviceError();
    }
    ServiceHandle lifetime(token);
    TOKEN_STATISTICS statistics{};
    DWORD size = 0;
    DWORD session = 0;
    const auto sid = userSid();
    if (sid.empty() ||
        !GetTokenInformation(token, TokenStatistics, &statistics,
                             sizeof(statistics), &size) ||
        !ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
        serviceError();
    }
    PSECURITY_LOGON_SESSION_DATA logon = nullptr;
    const auto status =
        LsaGetLogonSessionData(&statistics.AuthenticationId, &logon);
    if (status || !logon) {
        serviceError(status ? LsaNtStatusToWinError(status)
                            : ERROR_INVALID_DATA);
    }
    const auto logonTime = logon->LogonTime.QuadPart;
    LsaFreeReturnBuffer(logon);
    return L"Fcitx5Service-" + sid + L"-" + std::to_wstring(session) + L"-" +
           std::to_wstring(statistics.AuthenticationId.HighPart) + L"-" +
           std::to_wstring(statistics.AuthenticationId.LowPart) + L"-" +
           std::to_wstring(logonTime);
}

class ServiceSecurity {
  public:
    ServiceSecurity() {
        const auto sid = userSid();
        const auto descriptor = L"D:P(A;;GA;;;" + sid + L")";
        if (sid.empty() ||
            !ConvertStringSecurityDescriptorToSecurityDescriptorW(
                descriptor.c_str(), SDDL_REVISION_1, &descriptor_, nullptr)) {
            serviceError();
        }
        attributes_ = {sizeof(attributes_), descriptor_, FALSE};
    }
    ~ServiceSecurity() { LocalFree(descriptor_); }
    SECURITY_ATTRIBUTES *get() { return &attributes_; }

  private:
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    SECURITY_ATTRIBUTES attributes_{};
};

inline std::filesystem::path serviceStatePath(const std::wstring &identity) {
    std::wstring directory(32768, L'\0');
    const auto size =
        GetEnvironmentVariableW(L"LOCALAPPDATA", directory.data(),
                                static_cast<DWORD>(directory.size()));
    if (!size || size >= directory.size()) {
        serviceError(ERROR_PATH_NOT_FOUND);
    }
    directory.resize(size);
    return std::filesystem::path(directory) / "fcitx5" / (identity + L".state");
}

// Logon time also distinguishes logon LUIDs reused after reboot. The manual
// stop file survives unloading every TSF DLL, but does not affect a new login.
struct ServiceState {
    uint64_t disabled = 0;
    uint64_t lastLaunch = 0;
};

inline ServiceState readServiceState(const std::wstring &identity) {
    ServiceHandle file(
        CreateFileW(serviceStatePath(identity).c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND ||
            GetLastError() == ERROR_PATH_NOT_FOUND) {
            return {};
        }
        serviceError();
    }
    ServiceState state;
    DWORD size = 0;
    if (!ReadFile(file.get(), &state, sizeof(state), &size, nullptr) ||
        size != sizeof(state)) {
        serviceError(ERROR_INVALID_DATA);
    }
    return state;
}

inline bool serviceAutoStartAllowed() {
    try {
        return currentInstallationAllowsStart() &&
               !readServiceState(serviceIdentity()).disabled;
    } catch (...) {
        return false;
    }
}

inline std::wstring settingsActivationBase() {
    auto name = pipeName();
    if (name.empty()) {
        serviceError(ERROR_INVALID_DATA);
    }
    for (auto &letter : name) {
        if (letter == L'\\') {
            letter = L'_';
        }
    }
    return L"Local\\Fcitx5Settings-" + name;
}

class ServiceProcess {
  public:
    explicit ServiceProcess(const wchar_t *role,
                            const std::wstring &identity = serviceIdentity())
        : name_(L"Local\\" + identity + L"-" + role),
          owner_(createMutex(name_ + L"-owner")) {}
    ~ServiceProcess() {
        closeStopEvent();
        if (pid_) {
            UnmapViewOfFile(pid_);
        }
        if (mapping_) {
            CloseHandle(mapping_);
        }
        if (owned_) {
            ReleaseMutex(owner_.get());
        }
    }
    bool acquire() {
        const auto result = WaitForSingleObject(owner_.get(), 0);
        if (result == WAIT_TIMEOUT) {
            return false;
        }
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            serviceError();
        }
        owned_ = true;
        ServiceSecurity security;
        stop_ = CreateEventW(security.get(), TRUE, FALSE,
                             (name_ + L"-stop").c_str());
        if (!stop_ || !::ResetEvent(stop_)) {
            serviceError();
        }
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, security.get(),
                                      PAGE_READWRITE, 0, sizeof(DWORD),
                                      (name_ + L"-pid").c_str());
        if (!mapping_) {
            serviceError();
        }
        pid_ = static_cast<DWORD *>(
            MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, sizeof(DWORD)));
        if (!pid_) {
            serviceError();
        }
        *pid_ = GetCurrentProcessId();
        return true;
    }
    bool stopping() const {
        return !currentInstallationAllowsStart() ||
               (stop_ && WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0);
    }
    // Called before the owner mutex is released, after all process work ends.
    void closeStopEvent() {
        if (stop_) {
            CloseHandle(stop_);
            stop_ = nullptr;
        }
    }

  private:
    static HANDLE createMutex(const std::wstring &name) {
        ServiceSecurity security;
        const auto mutex = CreateMutexW(security.get(), FALSE, name.c_str());
        if (!mutex) {
            serviceError();
        }
        return mutex;
    }
    std::wstring name_;
    ServiceHandle owner_;
    HANDLE stop_ = nullptr;
    HANDLE mapping_ = nullptr;
    DWORD *pid_ = nullptr;
    bool owned_ = false;
};

inline bool isServiceProcess(const wchar_t *role) {
    const auto name = L"Local\\" + serviceIdentity() + L"-" + role + L"-pid";
    ServiceHandle mapping(OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str()));
    if (!mapping.get()) {
        return false;
    }
    const auto *pid = static_cast<const DWORD *>(
        MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, sizeof(DWORD)));
    if (!pid) {
        serviceError();
    }
    const auto result = *pid == GetCurrentProcessId();
    UnmapViewOfFile(pid);
    return result;
}

inline void launchServiceExecutable(const std::filesystem::path &path,
                                    bool background,
                                    const wchar_t *arguments = L"") {
    auto command = L"\"" + path.wstring() + L"\"";
    if (*arguments) {
        command += L" ";
        command += arguments;
    }
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE,
                        background ? CREATE_NO_WINDOW : 0, nullptr,
                        path.parent_path().c_str(), &startup, &process)) {
        serviceError();
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

class ServiceController {
  public:
    explicit ServiceController(std::filesystem::path core,
                               std::wstring identity = serviceIdentity())
        : core_(std::move(core)), identity_(std::move(identity)),
          control_(createControlMutex()) {
        const auto result = WaitForSingleObject(control_.get(), 5000);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            serviceError(result == WAIT_TIMEOUT ? ERROR_TIMEOUT
                                                : GetLastError());
        }
        locked_ = true;
    }
    ~ServiceController() {
        if (locked_) {
            ReleaseMutex(control_.get());
        }
    }
    bool ensureCore(bool explicitStart = false) {
        if (!InstallationGate(core_.parent_path().parent_path())
                 .allowsStart()) {
            return false;
        }
        auto state = readServiceState(identity_);
        if (state.disabled) {
            return false;
        }
        if (running(L"Core")) {
            return true;
        }
        const auto now = GetTickCount64();
        if (!explicitStart && state.lastLaunch && now >= state.lastLaunch &&
            now - state.lastLaunch < 10000) {
            return false;
        }
        state.lastLaunch = now;
        save(state);
        launchServiceExecutable(core_, true);
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            if (running(L"Core")) {
                return true;
            }
            Sleep(20);
        }
        serviceError(ERROR_SERVICE_REQUEST_TIMEOUT);
        return false;
    }
    void stop() {
        auto state = readServiceState(identity_);
        state.disabled = 1;
        save(state);
        std::exception_ptr failure;
        for (const auto role : {L"Settings", L"Core"}) {
            try {
                stopProcess(role);
            } catch (...) {
                if (!failure) {
                    failure = std::current_exception();
                }
            }
        }
        if (failure) {
            std::rethrow_exception(failure);
        }
    }
    bool restart() {
        const auto settingsOpen = running(L"Settings");
        stop();
        auto state = readServiceState(identity_);
        state.disabled = 0;
        state.lastLaunch = 0;
        save(state);
        ensureCore(true);
        return settingsOpen;
    }
    bool running(const wchar_t *role) const {
        ServiceHandle owner(OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE,
                                       objectName(role, L"-owner").c_str()));
        if (!owner.get()) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND) {
                serviceError();
            }
            return false;
        }
        const auto wait = WaitForSingleObject(owner.get(), 0);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
            ReleaseMutex(owner.get());
            return false;
        }
        if (wait != WAIT_TIMEOUT) {
            serviceError();
        }
        return true;
    }

  private:
    std::wstring objectName(const wchar_t *role, const wchar_t *suffix) const {
        return L"Local\\" + identity_ + L"-" + role + suffix;
    }
    HANDLE createControlMutex() {
        ServiceSecurity security;
        const auto mutex =
            CreateMutexW(security.get(), FALSE,
                         (L"Local\\" + identity_ + L"-control").c_str());
        if (!mutex) {
            serviceError();
        }
        return mutex;
    }
    void save(const ServiceState &state) const {
        const auto path = serviceStatePath(identity_);
        if (!replaceFile(
                path, std::string_view(reinterpret_cast<const char *>(&state),
                                       sizeof(state)))) {
            serviceError();
        }
    }
    void stopProcess(const wchar_t *role) const {
        if (!running(role)) {
            return;
        }
        ServiceHandle owner(OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE,
                                       objectName(role, L"-owner").c_str()));
        if (!owner.get()) {
            serviceError();
        }
        const auto end = GetTickCount64() + 3000;
        do {
            // Acquisition publishes ownership just before creating/resetting
            // the event. Re-signal while waiting so startup cannot lose a stop.
            ServiceHandle event(OpenEventW(EVENT_MODIFY_STATE, FALSE,
                                           objectName(role, L"-stop").c_str()));
            if (event.get()) {
                if (!SetEvent(event.get())) {
                    serviceError();
                }
            } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
                serviceError();
            }
            const auto wait = WaitForSingleObject(owner.get(), 20);
            if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
                ReleaseMutex(owner.get());
                return;
            }
            if (wait != WAIT_TIMEOUT) {
                serviceError();
            }
        } while (GetTickCount64() < end);
        serviceError(ERROR_TIMEOUT);
    }
    std::filesystem::path core_;
    std::wstring identity_;
    ServiceHandle control_;
    bool locked_ = false;
};

} // namespace fcitx::win32::ipc
