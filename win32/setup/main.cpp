#include "../dll/register.h"
#include "../dll/util.h"
#include "../ipc/service.h"
#include "userprofile.h"
#include <algorithm>
#include <fstream>
#include <restartmanager.h>
#include <shellapi.h>
#include <shlobj.h>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace ipc = fcitx::win32::ipc;
constexpr wchar_t kClsidKey[] = L"SOFTWARE\\Classes\\CLSID\\{FC3869BA-51E3-"
                                L"4078-8EE2-5FE49493A1F4}\\InprocServer32";

bool administrator() {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID sid = nullptr;
    BOOL member = FALSE;
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &sid)) {
        CheckTokenMembership(nullptr, sid, &member);
        FreeSid(sid);
    }
    return member != FALSE;
}

std::wstring registryString(HKEY root, const wchar_t *key,
                            const wchar_t *name) {
    wchar_t buffer[32768]{};
    DWORD size = sizeof(buffer);
    const auto result =
        RegGetValueW(root, key, name, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
                     nullptr, buffer, &size);
    if (result == ERROR_FILE_NOT_FOUND) {
        return {};
    }
    if (result != ERROR_SUCCESS) {
        ipc::serviceError(result);
    }
    return buffer;
}

bool ownsRegistration(const fs::path &root) {
    const auto path = registryString(HKEY_LOCAL_MACHINE, kClsidKey, nullptr);
    return path.empty() ||
           ipc::sameInstallationPath(path, root / "tsf" / "fcitx5-x86_64.dll");
}

void setRegistryString(const wchar_t *name, const std::wstring &value) {
    HKEY key = nullptr;
    auto result = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE, ipc::kInstallerKey, 0, nullptr, 0,
        KEY_READ | KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) {
        ipc::serviceError(result);
    }
    result = RegSetValueExW(key, name, 0, REG_SZ,
                            reinterpret_cast<const BYTE *>(value.c_str()),
                            static_cast<DWORD>((value.size() + 1) * 2));
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        ipc::serviceError(result);
    }
}

DWORD runProcess(const fs::path &file, const std::wstring &arguments,
                 DWORD timeout = 120000) {
    auto command = L"\"" + file.wstring() + L"\" " + arguments;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(file.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, file.parent_path().c_str(),
                        &startup, &process)) {
        return GetLastError();
    }
    ipc::ServiceHandle thread(process.hThread);
    ipc::ServiceHandle lifetime(process.hProcess);
    if (WaitForSingleObject(lifetime.get(), timeout) != WAIT_OBJECT_0) {
        // Do not terminate a runtime installer or user application on timeout.
        return ERROR_TIMEOUT;
    }
    DWORD result = ERROR_GEN_FAILURE;
    GetExitCodeProcess(lifetime.get(), &result);
    return result;
}

bool belowRoot(const fs::path &path, const fs::path &root) {
    const auto parent = root.lexically_normal().wstring() + L"\\";
    const auto child = path.lexically_normal().wstring();
    return child.size() > parent.size() &&
           CompareStringOrdinal(child.data(), static_cast<int>(parent.size()),
                                parent.data(), static_cast<int>(parent.size()),
                                TRUE) == CSTR_EQUAL;
}

bool pendingDeletion(const fs::path &root) {
    constexpr wchar_t key[] =
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager";
    DWORD size = 0;
    auto result =
        RegGetValueW(HKEY_LOCAL_MACHINE, key, L"PendingFileRenameOperations",
                     RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size);
    if (result == ERROR_FILE_NOT_FOUND) {
        return false;
    }
    if (result != ERROR_SUCCESS || size > 16 * 1024 * 1024) {
        ipc::serviceError(result == ERROR_SUCCESS ? ERROR_INVALID_DATA
                                                  : result);
    }
    std::vector<wchar_t> data(size / sizeof(wchar_t) + 2, L'\0');
    result =
        RegGetValueW(HKEY_LOCAL_MACHINE, key, L"PendingFileRenameOperations",
                     RRF_RT_REG_MULTI_SZ, nullptr, data.data(), &size);
    if (result != ERROR_SUCCESS) {
        ipc::serviceError(result);
    }
    const auto count = size / sizeof(wchar_t);
    // The MULTI_SZ contains source/destination pairs; deletion destinations are
    // empty.
    for (size_t offset = 0; offset < count;) {
        const auto length = wcsnlen(data.data() + offset, count - offset);
        std::wstring path(data.data() + offset, length);
        if (path.starts_with(L"\\??\\")) {
            path.erase(0, 4);
        }
        if (belowRoot(path, root)) {
            return true;
        }
        offset += length + 1;
    }
    return false;
}

DWORD inspectLocks(const fs::path &root) {
    if (!fs::exists(root)) {
        return ERROR_SUCCESS;
    }
    std::vector<std::wstring> files;
    for (const auto &entry : fs::recursive_directory_iterator(root)) {
        const auto attributes = GetFileAttributesW(entry.path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            return ERROR_ACCESS_DENIED;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto extension = entry.path().extension().wstring();
        if ((extension == L".dll" || extension == L".exe") &&
            entry.path().filename() != L"Fcitx5SetupHelper.exe" &&
            !entry.path().filename().wstring().starts_with(L"unins")) {
            files.push_back(entry.path().wstring());
        }
    }
    if (files.empty()) {
        return ERROR_SUCCESS;
    }
    DWORD session = 0;
    wchar_t key[CCH_RM_SESSION_KEY + 1]{};
    auto result = RmStartSession(&session, 0, key);
    if (result != ERROR_SUCCESS) {
        return result;
    }
    struct Lifetime {
        DWORD session;
        ~Lifetime() { RmEndSession(session); }
    } lifetime{session};
    std::vector<const wchar_t *> paths;
    for (const auto &file : files) {
        paths.push_back(file.c_str());
    }
    result = RmRegisterResources(session, static_cast<UINT>(paths.size()),
                                 paths.data(), 0, nullptr, 0, nullptr);
    if (result != ERROR_SUCCESS) {
        return result;
    }
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        UINT needed = 0, count = 0;
        DWORD reasons = 0;
        result = RmGetList(session, &needed, &count, nullptr, &reasons);
        if (result == ERROR_SUCCESS && !needed) {
            return ERROR_SUCCESS;
        }
        if (result != ERROR_MORE_DATA || needed > 4096) {
            return result;
        }
        std::vector<RM_PROCESS_INFO> processes(needed);
        count = needed;
        result =
            RmGetList(session, &needed, &count, processes.data(), &reasons);
        if (result == ERROR_SUCCESS) {
            return count ? ERROR_SHARING_VIOLATION : ERROR_SUCCESS;
        }
        if (result != ERROR_MORE_DATA) {
            return result;
        }
    }
    return ERROR_RETRY;
}

DWORD registration(const fs::path &root, bool uninstall) {
    if (!administrator()) {
        return ERROR_ACCESS_DENIED;
    }
    if (!ownsRegistration(root)) {
        return ERROR_ALREADY_EXISTS;
    }
    const auto dll = root / "tsf" / "fcitx5-x86_64.dll";
    if (uninstall && !fs::exists(dll)) {
        const auto profiles = fcitx::UnregisterCategoriesAndProfiles();
        const auto server = fcitx::UnregisterServer();
        return static_cast<DWORD>(FAILED(profiles) ? profiles : server);
    }
    const auto module = LoadLibraryExW(dll.c_str(), nullptr,
                                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                           LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) {
        return GetLastError();
    }
    const auto method = reinterpret_cast<HRESULT(WINAPI *)()>(GetProcAddress(
        module, uninstall ? "DllUnregisterServer" : "DllRegisterServer"));
    const auto result =
        method ? method() : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    FreeLibrary(module);
    return static_cast<DWORD>(result);
}

DWORD setState(const fs::path &root, const std::string &state) {
    if (!administrator() || !ownsRegistration(root)) {
        return ERROR_ACCESS_DENIED;
    }
    if (state != "installed\n" && state != "maintenance\n" &&
        state != "pending\n") {
        return ERROR_INVALID_PARAMETER;
    }
    if (!ipc::replaceFile(root / "setup" / "install-state", state)) {
        return ERROR_WRITE_FAULT;
    }
    setRegistryString(L"InstallRoot", root.wstring());
    setRegistryString(L"State", fcitx::stringToWString(state, CP_UTF8));
    return ERROR_SUCCESS;
}

DWORD configure(const fs::path &root) {
    if (administrator()) {
        return ERROR_BAD_TOKEN_TYPE;
    }
    auto result = runProcess(root / "settings" / "Fcitx5Settings.exe",
                             L"--runtime-check");
    if (result) {
        result = runProcess(root / "setup" / "WindowsAppRuntimeInstall-x64.exe",
                            L"--quiet");
        if (result) {
            return result;
        }
        result = runProcess(root / "settings" / "Fcitx5Settings.exe",
                            L"--runtime-check");
        if (result) {
            return result;
        }
    }
    return fcitx::setup::configureUser(root);
}

DWORD uninstallLauncher(const fs::path &root, bool silent, bool elevated) {
    if (!elevated && !administrator()) {
        const auto prepared = fcitx::setup::removeUser(root, false);
        if (prepared) {
            return prepared;
        }
        std::wstring arguments =
            std::wstring(L"uninstall --elevated --prefix ") + L"\"" +
            root.wstring() + L"\"";
        if (silent) {
            arguments += L" --silent";
        }
        SHELLEXECUTEINFOW execute{sizeof(execute)};
        execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        execute.lpVerb = L"runas";
        execute.lpFile = (root / "setup" / "Fcitx5SetupHelper.exe").c_str();
        execute.lpParameters = arguments.c_str();
        execute.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&execute)) {
            fcitx::setup::configureUser(root);
            return GetLastError();
        }
        ipc::ServiceHandle process(execute.hProcess);
        if (WaitForSingleObject(process.get(), INFINITE) != WAIT_OBJECT_0) {
            return ERROR_TIMEOUT;
        }
        DWORD result = ERROR_GEN_FAILURE;
        GetExitCodeProcess(process.get(), &result);
        return result;
    }
    if (!administrator()) {
        return ERROR_ACCESS_DENIED;
    }
    const auto registered =
        registryString(HKEY_LOCAL_MACHINE, ipc::kInstallerKey, L"InstallRoot");
    if (!ipc::sameInstallationPath(registered, root)) {
        return ERROR_INVALID_DATA;
    }
    auto result = setState(root, "maintenance\n");
    if (result)
        return result;
    Sleep(3500);
    const auto lock = inspectLocks(root);
    if (lock && !silent) {
        MessageBoxW(nullptr,
                    L"Some Fcitx5 files are still in use. The input method "
                    L"will be unregistered; sign out to release this session, "
                    L"or restart Windows to finish file cleanup.",
                    L"Fcitx5", MB_OK | MB_ICONINFORMATION);
    }
    result = registration(root, true);
    if (result)
        return result;
    const auto native = registryString(HKEY_LOCAL_MACHINE, ipc::kInstallerKey,
                                       L"NativeUninstaller");
    if (!belowRoot(native, root) ||
        !fs::path(native).filename().wstring().starts_with(L"unins")) {
        return ERROR_INVALID_DATA;
    }
    const auto uninstallerArgs =
        (silent ? L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /HELPER"
                : L"/NORESTART /HELPER");
    result = runProcess(native, uninstallerArgs);
    if (result == 0 && lock) {
        result = ERROR_SUCCESS_REBOOT_REQUIRED;
    }
    return result;
}

void logResult(const std::wstring &command, DWORD result) {
    PWSTR folder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE,
                                       nullptr, &folder))) {
        const auto directory = fs::path(folder) / "fcitx5";
        CoTaskMemFree(folder);
        std::error_code error;
        fs::create_directories(directory, error);
        std::ofstream log(directory / "installer-helper.log", std::ios::app);
        log << fcitx::guidToString(GUID_NULL) << ' '
            << std::string(command.begin(), command.end())
            << " result=" << result << '\n';
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) {
        return static_cast<int>(initialized);
    }
    struct ComLifetime {
        ~ComLifetime() { CoUninitialize(); }
    } com;
    int count = 0;
    auto **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments || count < 2) {
        LocalFree(arguments);
        return ERROR_INVALID_PARAMETER;
    }
    std::vector<std::wstring> args(arguments + 1, arguments + count);
    LocalFree(arguments);
    const auto command = args.front();
    auto root = ipc::installationRootForAddress(
        reinterpret_cast<const void *>(&wWinMain));
    bool preview = false, silent = false, elevated = false;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == L"--prefix" && i + 1 < args.size()) {
            root = args[++i];
        } else if (args[i] == L"--what-if") {
            preview = true;
        } else if (args[i] == L"--silent") {
            silent = true;
        } else if (args[i] == L"--elevated") {
            elevated = true;
        } else {
            return ERROR_INVALID_PARAMETER;
        }
    }
    const std::vector<std::wstring> commands{
        L"preflight-machine", L"register-machine",  L"unregister-machine",
        L"configure-user",    L"rollback-user",     L"prepare-uninstall-user",
        L"set-maintenance",   L"clear-maintenance", L"mark-pending",
        L"pause-user",        L"resume-user",       L"inspect-locks",
        L"uninstall"};
    if (!root.is_absolute() || root == root.root_path() ||
        std::find(commands.begin(), commands.end(), command) ==
            commands.end()) {
        return ERROR_INVALID_PARAMETER;
    }
    if (preview) {
        return ERROR_SUCCESS;
    }
    DWORD result = ERROR_GEN_FAILURE;
    try {
        if (command == L"preflight-machine") {
            result = pendingDeletion(root)     ? ERROR_SUCCESS_REBOOT_REQUIRED
                     : !ownsRegistration(root) ? ERROR_ALREADY_EXISTS
                                               : ERROR_SUCCESS;
        } else if (command == L"register-machine" ||
                   command == L"unregister-machine") {
            result = registration(root, command == L"unregister-machine");
        } else if (command == L"configure-user") {
            result = configure(root);
        } else if (command == L"pause-user" || command == L"resume-user") {
            if (administrator()) {
                result = ERROR_BAD_TOKEN_TYPE;
            } else if (command == L"pause-user") {
                result = fcitx::setup::removeUser(root, false);
            } else {
                result = fcitx::setup::configureUser(root);
            }
        } else if (command == L"rollback-user" ||
                   command == L"prepare-uninstall-user") {
            if (administrator()) {
                result = ERROR_BAD_TOKEN_TYPE;
            } else {
                result =
                    fcitx::setup::removeUser(root, command == L"rollback-user");
            }
        } else if (command == L"inspect-locks") {
            result = inspectLocks(root);
        } else if (command == L"uninstall") {
            result = uninstallLauncher(root, silent, elevated);
        } else {
            result =
                setState(root, command == L"clear-maintenance" ? "installed\n"
                               : command == L"mark-pending" ? "pending\n"
                                                            : "maintenance\n");
        }
    } catch (const std::system_error &error) {
        result = static_cast<DWORD>(error.code().value());
    } catch (...) {
        result = ERROR_GEN_FAILURE;
    }
    logResult(command, result);
    if (result && command == L"uninstall" && !silent &&
        result != ERROR_CANCELLED) {
        const auto message = L"Cannot uninstall Fcitx5. Windows error: " +
                             std::to_wstring(result);
        MessageBoxW(nullptr, message.c_str(), L"Fcitx5", MB_OK | MB_ICONERROR);
    }
    return static_cast<int>(result);
}
