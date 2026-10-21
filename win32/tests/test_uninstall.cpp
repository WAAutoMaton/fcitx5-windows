#include "../dll/register.h"
#include "../dll/util.h"
#include "../ipc/service.h"
#include "../setup/log.h"
#include "../setup/userprofile.h"
#include <cassert>
#include <iostream>
#include <map>
#include <restartmanager.h>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int);

namespace {
namespace fs = std::filesystem;
fs::path root;
std::wstring commandLine;
bool admin = false, locked = false, cancelElevation = false;
DWORD nativeResult = 0;
unsigned preparations = 0, registrations = 0, nativeCalls = 0;
unsigned errors = 0, information = 0, restores = 0;
unsigned x86Registrations = 0;
DWORD x86Result = 0;
bool foreignX86 = false;
HRESULT registerResult = S_OK;
std::vector<bool> x86Actions;
std::map<HANDLE, DWORD> exitCodes;

HANDLE completedProcess(DWORD result) {
    const auto handle = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    assert(handle);
    exitCodes[handle] = result;
    return handle;
}

BOOL WINAPI mockCheckTokenMembership(HANDLE, PSID, PBOOL member) {
    *member = admin;
    return TRUE;
}

LSTATUS WINAPI mockRegGetValueW(HKEY, LPCWSTR key, LPCWSTR name, DWORD flags,
                                LPDWORD, PVOID data, LPDWORD size) {
    if (std::wstring_view(key).starts_with(L"SOFTWARE\\Classes\\CLSID")) {
        if (foreignX86 && (flags & RRF_SUBKEY_WOW6432KEY)) {
            constexpr wchar_t other[] = L"C:\\Other\\fcitx5-x86.dll";
            assert(*size >= sizeof(other));
            memcpy(data, other, sizeof(other));
            *size = sizeof(other);
            return ERROR_SUCCESS;
        }
        return ERROR_FILE_NOT_FOUND;
    }
    assert(std::wstring_view(key) == fcitx::win32::ipc::kInstallerKey);
    const auto value = std::wstring_view(name) == L"InstallRoot"
                           ? root.wstring()
                           : (root / "unins000.exe").wstring();
    const auto bytes = (value.size() + 1) * sizeof(wchar_t);
    assert(*size >= bytes);
    memcpy(data, value.c_str(), bytes);
    *size = static_cast<DWORD>(bytes);
    return ERROR_SUCCESS;
}

LSTATUS WINAPI mockRegCreateKeyExW(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM,
                                   LPSECURITY_ATTRIBUTES, PHKEY key, LPDWORD) {
    *key = reinterpret_cast<HKEY>(1);
    return ERROR_SUCCESS;
}
LSTATUS WINAPI mockRegSetValueExW(HKEY, LPCWSTR, DWORD, DWORD, const BYTE *,
                                  DWORD) {
    return ERROR_SUCCESS;
}
LSTATUS WINAPI mockRegCloseKey(HKEY) { return ERROR_SUCCESS; }

HRESULT WINAPI mockUnregister() {
    ++registrations;
    return S_OK;
}
HRESULT WINAPI mockRegister() {
    ++registrations;
    return registerResult;
}
HMODULE WINAPI mockLoadLibraryExW(LPCWSTR path, HANDLE, DWORD) {
    assert(fs::path(path) == root / "tsf" / "fcitx5-x86_64.dll");
    return reinterpret_cast<HMODULE>(1);
}
FARPROC WINAPI mockGetProcAddress(HMODULE, LPCSTR name) {
    if (std::string_view(name) == "DllRegisterServer")
        return reinterpret_cast<FARPROC>(&mockRegister);
    assert(std::string_view(name) == "DllUnregisterServer");
    return reinterpret_cast<FARPROC>(&mockUnregister);
}
BOOL WINAPI mockFreeLibrary(HMODULE) { return TRUE; }

DWORD WINAPI mockRmStartSession(DWORD *session, DWORD, WCHAR *) {
    *session = 1;
    return ERROR_SUCCESS;
}
DWORD WINAPI mockRmRegisterResources(DWORD, UINT count, LPCWSTR files[], UINT,
                                     RM_UNIQUE_PROCESS[], UINT, LPCWSTR[]) {
    const bool dual = fs::exists(root / "tsf/x86/fcitx5-x86.dll");
    assert(count == (dual ? 2u : 1u));
    for (UINT index = 0; index < count; ++index)
        assert(fs::path(files[index]) == root / "tsf/fcitx5-x86_64.dll" ||
               fs::path(files[index]) == root / "tsf/x86/fcitx5-x86.dll");
    return ERROR_SUCCESS;
}
DWORD WINAPI mockRmGetList(DWORD, UINT *needed, UINT *count,
                           RM_PROCESS_INFO *processes, DWORD *) {
    *needed = locked ? 1 : 0;
    if (locked && !processes) {
        return ERROR_MORE_DATA;
    }
    *count = *needed;
    return ERROR_SUCCESS;
}
DWORD WINAPI mockRmEndSession(DWORD) { return ERROR_SUCCESS; }
void WINAPI mockSleep(DWORD) {}
DWORD WINAPI mockWaitForSingleObject(HANDLE, DWORD) { return WAIT_OBJECT_0; }
BOOL WINAPI mockGetExitCodeProcess(HANDLE process, LPDWORD result) {
    *result = exitCodes.at(process);
    return TRUE;
}

BOOL WINAPI mockCreateProcessW(LPCWSTR file, LPWSTR command,
                               LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
                               BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW,
                               LPPROCESS_INFORMATION process) {
    if (fs::path(file).filename() == L"regsvr32.exe") {
        wchar_t directory[MAX_PATH]{};
        assert(GetSystemWow64DirectoryW(directory, MAX_PATH));
        assert(fs::path(file) == fs::path(directory) / L"regsvr32.exe");
        const bool uninstall = std::wstring_view(command).find(L"/s /u ") !=
                               std::wstring_view::npos;
        assert(std::wstring_view(command).ends_with(
            std::wstring(uninstall ? L"/s /u \"" : L"/s \"") +
            (root / "tsf" / "x86" / "fcitx5-x86.dll").wstring() + L"\""));
        x86Actions.push_back(uninstall);
        ++x86Registrations;
        process->hProcess = completedProcess(x86Result);
        process->hThread = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        return TRUE;
    }
    assert(fs::path(file) == root / "unins000.exe");
    const auto silent = commandLine.find(L"--silent") != std::wstring::npos;
    const auto expected =
        L"\"" + fs::path(file).wstring() + L"\" " +
        (silent ? L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /HELPER=1"
                : L"/NORESTART /HELPER=1");
    assert(command == expected);
    ++nativeCalls;
    process->hProcess = completedProcess(nativeResult);
    process->hThread = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    assert(process->hThread);
    return TRUE;
}

BOOL WINAPI mockShellExecuteExW(SHELLEXECUTEINFOW *execute) {
    assert(std::wstring_view(execute->lpVerb) == L"runas");
    assert(fs::path(execute->lpFile) ==
           root / "setup" / "Fcitx5SetupHelper.exe");
    if (cancelElevation) {
        SetLastError(ERROR_CANCELLED);
        return FALSE;
    }
    const auto saved = commandLine;
    commandLine =
        L"\"" + std::wstring(execute->lpFile) + L"\" " + execute->lpParameters;
    assert(commandLine.find(L"--elevated") != std::wstring::npos);
    admin = true;
    const auto result = wWinMain(nullptr, nullptr, nullptr, 0);
    admin = false;
    commandLine = saved;
    execute->hProcess = completedProcess(static_cast<DWORD>(result));
    return TRUE;
}

LPWSTR WINAPI mockGetCommandLineW() { return commandLine.data(); }
int WINAPI mockMessageBoxW(HWND, LPCWSTR text, LPCWSTR, UINT style) {
    if (style & MB_ICONERROR) {
        assert(
            std::wstring_view(text).starts_with(L"Cannot uninstall Fcitx5."));
        ++errors;
    } else {
        assert(style & MB_ICONINFORMATION);
        ++information;
    }
    return IDOK;
}
} // namespace

namespace fcitx::setup {
DWORD mockRemoveUser(const fs::path &path, bool rollback) {
    assert(path == root && !rollback);
    ++preparations;
    return ERROR_SUCCESS;
}
DWORD mockConfigureUser(const fs::path &) {
    ++restores;
    return ERROR_SUCCESS;
}
void mockLogMessage(std::string_view) noexcept {}
void mockLogMessage(std::wstring_view) noexcept {}
void mockLogStep(std::string_view, DWORD) noexcept {}
} // namespace fcitx::setup

// Run the production launcher and entry point with all system mutations mocked.
#define CheckTokenMembership mockCheckTokenMembership
#define RegGetValueW mockRegGetValueW
#define RegCreateKeyExW mockRegCreateKeyExW
#define RegSetValueExW mockRegSetValueExW
#define RegCloseKey mockRegCloseKey
#define LoadLibraryExW mockLoadLibraryExW
#define GetProcAddress mockGetProcAddress
#define FreeLibrary mockFreeLibrary
#define RmStartSession mockRmStartSession
#define RmRegisterResources mockRmRegisterResources
#define RmGetList mockRmGetList
#define RmEndSession mockRmEndSession
#define Sleep mockSleep
#define WaitForSingleObject mockWaitForSingleObject
#define GetExitCodeProcess mockGetExitCodeProcess
#define CreateProcessW mockCreateProcessW
#define ShellExecuteExW mockShellExecuteExW
#define GetCommandLineW mockGetCommandLineW
#define MessageBoxW mockMessageBoxW
#define removeUser mockRemoveUser
#define configureUser mockConfigureUser
#define logMessage mockLogMessage
#define logStep mockLogStep
#include "../setup/main.cpp"

int main() {
    root = fs::temp_directory_path() /
           (L"fcitx5-uninstall-test-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(root / "setup");
    fs::create_directories(root / "tsf");
    std::ofstream(root / "tsf" / "fcitx5-x86_64.dll") << "fixture";
    const auto run = [](bool elevated, bool silent, DWORD result, bool inUse) {
        admin = elevated;
        locked = inUse;
        nativeResult = result;
        preparations = registrations = nativeCalls = errors = information = 0;
        exitCodes.clear();
        commandLine =
            L"helper.exe uninstall --prefix \"" + root.wstring() + L"\"";
        if (elevated)
            commandLine += L" --elevated";
        if (silent)
            commandLine += L" --silent";
        std::ofstream(root / "setup" / "install-state") << "maintenance\n";
        const auto outcome =
            static_cast<DWORD>(wWinMain(nullptr, nullptr, nullptr, 0));
        if (outcome == ERROR_SUCCESS ||
            outcome == ERROR_SUCCESS_REBOOT_REQUIRED)
            assert(!fs::exists(root / "setup" / "install-state"));
        else
            assert(fs::exists(root / "setup" / "install-state"));
        return outcome;
    };
    for (const bool elevated : {false, true}) {
        for (const bool silent : {false, true}) {
            assert(run(elevated, silent, 0, false) == ERROR_SUCCESS);
            assert(!errors && !information);
            assert(preparations == (elevated ? 0u : 1u));
            assert(registrations == 1 && nativeCalls == 1);
            assert(run(elevated, silent, 0, true) ==
                   ERROR_SUCCESS_REBOOT_REQUIRED);
            assert(!errors && information == (silent ? 0u : 1u));
            assert(registrations == 1 && nativeCalls == 1);
            assert(run(elevated, silent, ERROR_SUCCESS_REBOOT_REQUIRED,
                       false) == ERROR_SUCCESS_REBOOT_REQUIRED);
            assert(!errors);
            assert(run(elevated, silent, ERROR_ACCESS_DENIED, true) ==
                   ERROR_ACCESS_DENIED);
            assert(errors == (silent ? 0u : elevated ? 1u : 2u));
            assert(run(elevated, silent, ERROR_CANCELLED, false) ==
                   ERROR_CANCELLED);
            assert(!errors);
        }
    }
    cancelElevation = true;
    assert(run(false, false, 0, false) == ERROR_CANCELLED);
    assert(!errors && !registrations && !nativeCalls && restores == 1);
    cancelElevation = false;
    fs::create_directories(root / "tsf/x86");
    std::ofstream(root / "tsf/x86/fcitx5-x86.dll") << "fixture";
    assert(run(true, true, 0, false) == 0);
    assert(x86Registrations == 1 && registrations == 1 && nativeCalls == 1);
    x86Result = ERROR_ACCESS_DENIED;
    assert(run(true, true, 0, false) == ERROR_ACCESS_DENIED);
    assert(x86Registrations == 2 && !registrations && !nativeCalls);
    foreignX86 = true;
    assert(run(true, true, 0, false) == ERROR_ACCESS_DENIED);
    assert(x86Registrations == 2 && !registrations && !nativeCalls);
    foreignX86 = false;
    x86Result = 0;
    registerResult = E_ACCESSDENIED;
    x86Actions.clear();
    assert(registration(root, false) == static_cast<DWORD>(E_ACCESSDENIED));
    assert((x86Actions == std::vector<bool>{false, true}));
    registerResult = S_OK;
    x86Actions.clear();
    assert(registration(root, false) == 0);
    assert((x86Actions == std::vector<bool>{false}));
    fs::remove_all(root);
    std::cout << "Uninstall reboot results, elevation, helper mode, silence "
                 "and failures passed.\n";
}
