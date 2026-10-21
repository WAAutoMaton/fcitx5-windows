#include "../dll/register.h"
#include "../ipc/installation.h"
#include "../ipc/isolationpolicy.h"
#include "log.h"
#include "upgrade.h"
#include <atlbase.h>
#include <atlcomcli.h>
#include <boost/property_tree/json_parser.hpp>
#include <iostream>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#include <taskschd.h>
#include <tlhelp32.h>

namespace fcitx::setup::upgrade {
namespace {
namespace ipc = win32::ipc;
using Tree = boost::property_tree::ptree;
constexpr wchar_t kUninstall[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
    L"{0D07698A-591B-44B4-940F-BBD38AA62F0E}_is1";
constexpr wchar_t kClsid[] =
    L"SOFTWARE\\Classes\\CLSID\\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}";
constexpr wchar_t kTip[] =
    L"SOFTWARE\\Microsoft\\CTF\\TIP\\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}";
constexpr wchar_t kEpoch[] = L"SOFTWARE\\Fcitx5\\Installer\\UpgradeEpoch";
constexpr wchar_t kTask[] = L"Fcitx5-CompleteUpgrade";

template <typename Callback>
void loggedPhase(std::string_view name, Callback &&callback) {
    const auto prefix = std::string("upgrade phase=") + std::string(name);
    logMessage(prefix + " begin");
    try {
        callback();
    } catch (const std::system_error &error) {
        logStep(prefix + " failed", DWORD(error.code().value()));
        throw;
    }
    logMessage(prefix + " complete");
}

const char *isolationName(ipc::IsolationKind isolation) {
    switch (isolation) {
    case ipc::IsolationKind::Host:
        return "host";
    case ipc::IsolationKind::Sandboxie:
        return "sandboxie";
    case ipc::IsolationKind::AppContainer:
        return "appcontainer";
    case ipc::IsolationKind::SandboxieAppContainer:
        return "sandboxie-appcontainer";
    case ipc::IsolationKind::Unknown:
        return "unknown";
    }
    return "unknown";
}

void logEnvironment() {
    const auto environment = ipc::clientEnvironment();
    char details[160]{};
    snprintf(
        details, sizeof(details),
        "upgrade environment isolation=%s token-known=%u appcontainer=%u "
        "low-integrity=%u",
        isolationName(environment.isolation), unsigned(environment.tokenKnown),
        unsigned(environment.appContainer), unsigned(environment.lowIntegrity));
    logMessage(details);
}

void hr(HRESULT result) {
    if (FAILED(result))
        fail(DWORD(result));
}
void reg(LSTATUS error) {
    if (error != ERROR_SUCCESS)
        fail(error);
}
struct Key {
    HKEY value = nullptr;
    ~Key() {
        if (value)
            RegCloseKey(value);
    }
};
bool administrator() {
    BYTE sid[SECURITY_MAX_SID_SIZE]{};
    DWORD size = sizeof(sid);
    BOOL member = FALSE;
    return CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sid,
                              &size) &&
           CheckTokenMembership(nullptr, sid, &member) && member;
}
std::wstring regString(const wchar_t *key, const wchar_t *name,
                       DWORD view = RRF_SUBKEY_WOW6464KEY) {
    std::wstring result(32768, L'\0');
    DWORD size = DWORD(result.size() * sizeof(wchar_t));
    reg(RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ | view,
                     nullptr, result.data(), &size));
    result.resize(wcsnlen(result.c_str(), result.size()));
    return result;
}
void setString(const wchar_t *path, const wchar_t *name,
               const std::wstring &value) {
    Key key;
    reg(RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0,
                      KEY_SET_VALUE | KEY_WOW64_64KEY, &key.value));
    reg(RegSetValueExW(key.value, name, 0, REG_SZ,
                       reinterpret_cast<const BYTE *>(value.c_str()),
                       DWORD((value.size() + 1) * sizeof(wchar_t))));
}
Tree load(const fs::path &path) {
    Tree tree;
    std::istringstream stream(readText(path));
    boost::property_tree::read_json(stream, tree);
    return tree;
}
void save(const fs::path &path, const Tree &tree) {
    std::ostringstream stream;
    boost::property_tree::write_json(stream, tree, false);
    writeText(path, stream.str());
}
std::string hex(const std::vector<BYTE> &bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (auto byte : bytes) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}
std::vector<BYTE> unhex(std::string_view text) {
    if (text.size() % 2 || text.size() > 8 * 1024 * 1024)
        fail(ERROR_INVALID_DATA);
    std::vector<BYTE> result;
    auto digit = [](char c) -> BYTE {
        if (c >= '0' && c <= '9')
            return BYTE(c - '0');
        if (c >= 'a' && c <= 'f')
            return BYTE(c - 'a' + 10);
        fail(ERROR_INVALID_DATA);
    };
    for (size_t i = 0; i < text.size(); i += 2)
        result.push_back(BYTE((digit(text[i]) << 4) | digit(text[i + 1])));
    return result;
}

Tree snapshotKey(HKEY parent, const std::wstring &path, bool deep = true,
                 unsigned depth = 0, REGSAM view = KEY_WOW64_64KEY) {
    if (depth > 32)
        fail(ERROR_INVALID_DATA);
    Tree result, values, children;
    Key key;
    const auto opened =
        RegOpenKeyExW(parent, path.c_str(), 0, KEY_READ | view, &key.value);
    result.put("exists", opened != ERROR_FILE_NOT_FOUND);
    if (opened == ERROR_FILE_NOT_FOUND)
        return result;
    reg(opened);
    DWORD maxName = 0, maxData = 0, count = 0;
    reg(RegQueryInfoKeyW(key.value, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr, &count, &maxName, &maxData, nullptr,
                         nullptr));
    if (count > 4096 || maxData > 4 * 1024 * 1024 || maxName > 32767)
        fail(ERROR_INVALID_DATA);
    for (DWORD index = 0; index < count; ++index) {
        std::wstring name(maxName + 1, L'\0');
        std::vector<BYTE> data(maxData);
        DWORD names = DWORD(name.size()), bytes = DWORD(data.size()), type = 0;
        reg(RegEnumValueW(key.value, index, name.data(), &names, nullptr, &type,
                          data.data(), &bytes));
        name.resize(names);
        data.resize(bytes);
        Tree value;
        value.put("name", utf8(name));
        value.put("type", type);
        value.put("data", hex(data));
        values.push_back({"", value});
    }
    result.add_child("values", values);
    if (deep) {
        for (DWORD index = 0;; ++index) {
            wchar_t name[256]{};
            DWORD size = 256;
            const auto error =
                RegEnumKeyExW(key.value, index, name, &size, nullptr, nullptr,
                              nullptr, nullptr);
            if (error == ERROR_NO_MORE_ITEMS)
                break;
            reg(error);
            Tree child = snapshotKey(key.value, name, true, depth + 1, view);
            child.put("name", utf8(name));
            children.push_back({"", child});
        }
    }
    result.add_child("children", children);
    return result;
}
void restoreKey(HKEY parent, const std::wstring &path, const Tree &snapshot,
                bool deep = true, unsigned depth = 0,
                REGSAM view = KEY_WOW64_64KEY) {
    if (depth > 32)
        fail(ERROR_INVALID_DATA);
    Key key;
    if (deep) {
        const auto opened = RegOpenKeyExW(
            parent, path.c_str(), 0, KEY_READ | KEY_WRITE | view, &key.value);
        if (opened != ERROR_FILE_NOT_FOUND) {
            reg(opened);
            reg(RegDeleteTreeW(key.value, nullptr));
            RegCloseKey(key.value);
            key.value = nullptr;
            reg(RegDeleteKeyExW(parent, path.c_str(), view, 0));
        }
    }
    if (!snapshot.get<bool>("exists"))
        return;
    reg(RegCreateKeyExW(parent, path.c_str(), 0, nullptr, 0,
                        KEY_READ | KEY_WRITE | view, nullptr, &key.value,
                        nullptr));
    if (!deep) {
        wchar_t name[32768]{};
        DWORD size = 32768;
        for (;;) {
            const auto error = RegEnumValueW(key.value, 0, name, &size, nullptr,
                                             nullptr, nullptr, nullptr);
            if (error == ERROR_NO_MORE_ITEMS)
                break;
            reg(error);
            reg(RegDeleteValueW(key.value, name));
            size = 32768;
        }
    }
    for (const auto &[unused, value] : snapshot.get_child("values")) {
        const auto name = wide(value.get<std::string>("name"));
        const auto bytes = unhex(value.get<std::string>("data"));
        reg(RegSetValueExW(key.value, name.c_str(), 0, value.get<DWORD>("type"),
                           bytes.data(), DWORD(bytes.size())));
    }
    if (deep) {
        for (const auto &[unused, child] : snapshot.get_child("children")) {
            const auto name = wide(child.get<std::string>("name"));
            if (name.empty() ||
                name.find_first_of(L"\\/\0", 0, 3) != std::wstring::npos)
                fail(ERROR_INVALID_DATA);
            restoreKey(key.value, name, child, true, depth + 1, view);
        }
    }
}

fs::path modulePath() {
    std::wstring path(32768, L'\0');
    const auto size =
        GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!size || size >= path.size())
        fail();
    path.resize(size);
    return path;
}
DWORD parentProcess() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot)
        fail();
    PROCESSENTRY32W entry{sizeof(entry)};
    if (!Process32FirstW(snapshot.get(), &entry))
        fail();
    do {
        if (entry.th32ProcessID == GetCurrentProcessId())
            return entry.th32ParentProcessID;
    } while (Process32NextW(snapshot.get(), &entry));
    fail(ERROR_NOT_FOUND);
}

DWORD process(const fs::path &path, const std::wstring &arguments) {
    auto command = L"\"" + path.wstring() + L"\" " + arguments;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, path.parent_path().c_str(),
                        &startup, &info))
        fail();
    Handle thread(info.hThread), child(info.hProcess);
    // Never orphan a still-running dependency installer and resume old files.
    if (WaitForSingleObject(child.get(), INFINITE) != WAIT_OBJECT_0)
        fail();
    DWORD result = ERROR_GEN_FAILURE;
    if (!GetExitCodeProcess(child.get(), &result))
        fail();
    return result;
}

class NativeMachine : public Machine {
  public:
    explicit NativeMachine(fs::path root) : root_(std::move(root)) {}
    void validate() override {
        logEnvironment();
        loggedPhase("host-services policy", [] { ipc::requireHostServices(); });
        loggedPhase("administrator token", [] {
            if (!administrator())
                fail(ERROR_ACCESS_DENIED);
        });
        loggedPhase("non-root installation path", [&] {
            if (root_ == root_.root_path())
                fail(ERROR_ACCESS_DENIED);
        });
        loggedPhase("installation root security",
                    [&] { validateSecurity(root_); });
        loggedPhase("setup directory security",
                    [&] { validateSecurity(root_ / "setup"); });
        loggedPhase("registered installation root", [&] {
            if (!ipc::registeredInstallationRoot(root_))
                fail(ERROR_ALREADY_EXISTS);
        });
        loggedPhase("TSF registration", [&] {
            try {
                if (!ipc::sameInstallationPath(
                        regString((std::wstring(kClsid) + L"\\InprocServer32")
                                      .c_str(),
                                  nullptr),
                        root_ / "tsf" / "fcitx5-x86_64.dll"))
                    fail(ERROR_ALREADY_EXISTS);
            } catch (const std::system_error &error) {
                if (error.code().value() != ERROR_FILE_NOT_FOUND ||
                    !hasTransaction(root_) ||
                    !busyState(transactionState(root_)))
                    throw;
            }
        });
        loggedPhase("x86 TSF registration ownership", [&] {
            try {
                if (!ipc::sameInstallationPath(
                        regString((std::wstring(kClsid) + L"\\InprocServer32")
                                      .c_str(),
                                  nullptr, RRF_SUBKEY_WOW6432KEY),
                        root_ / "tsf/x86/fcitx5-x86.dll"))
                    fail(ERROR_ALREADY_EXISTS);
            } catch (const std::system_error &error) {
                if (error.code().value() != ERROR_FILE_NOT_FOUND)
                    throw;
            }
        });
        loggedPhase("NTFS fixed-volume check", [&] {
            wchar_t filesystem[32]{};
            if (GetDriveTypeW(root_.root_path().c_str()) != DRIVE_FIXED ||
                !GetVolumeInformationW(root_.root_path().c_str(), nullptr, 0,
                                       nullptr, nullptr, nullptr, filesystem,
                                       32) ||
                std::wstring_view(filesystem) != L"NTFS")
                fail(ERROR_NOT_SUPPORTED);
        });
        // Validate every active/control directory before allowing SYSTEM work.
        loggedPhase("installation tree security", [&] {
            size_t index = 0;
            for (const auto &entry : fs::recursive_directory_iterator(root_)) {
                try {
                    validatePath(entry.path());
                    validateSecurity(entry.path());
                } catch (const std::system_error &error) {
                    logStep("upgrade installation tree entry failed index=" +
                                std::to_string(index),
                            DWORD(error.code().value()));
                    throw;
                }
                ++index;
            }
            logMessage("upgrade installation tree entries=" +
                       std::to_string(index));
        });
        loggedPhase("pending restart operations", [&] { pendingOperations(); });
    }
    void gate(std::string_view state) override {
        writeText(root_ / "setup" / "install-state", std::string(state) + "\n");
        setString(win32::ipc::kInstallerKey, L"State", wide(state));
    }
    bool sameBoot() override {
        DWORD value = 0, size = sizeof(value);
        const auto error = RegGetValueW(
            HKEY_LOCAL_MACHINE, kEpoch, L"Prepared",
            RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr, &value, &size);
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            return false;
        reg(error);
        if (value != 1)
            fail(ERROR_INVALID_DATA);
        return true;
    }
    void markBoot(bool present) override {
        const auto error =
            RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kEpoch, KEY_WOW64_64KEY, 0);
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            reg(error);
        if (!present)
            return;
        Key key;
        reg(RegCreateKeyExW(HKEY_LOCAL_MACHINE, kEpoch, 0, nullptr,
                            REG_OPTION_VOLATILE, KEY_WRITE | KEY_WOW64_64KEY,
                            nullptr, &key.value, nullptr));
        DWORD value = 1;
        reg(RegSetValueExW(key.value, L"Prepared", 0, REG_DWORD,
                           reinterpret_cast<const BYTE *>(&value),
                           sizeof(value)));
    }
    void task(bool enable, std::string_view id) override {
        CComPtr<ITaskService> service;
        hr(service.CoCreateInstance(CLSID_TaskScheduler));
        hr(service->Connect(CComVariant(), CComVariant(), CComVariant(),
                            CComVariant()));
        CComPtr<ITaskFolder> folder;
        hr(service->GetFolder(CComBSTR(L"\\"), &folder));
        CComPtr<IRegisteredTask> existing;
        const auto found = folder->GetTask(CComBSTR(kTask), &existing);
        if (SUCCEEDED(found)) {
            CComPtr<ITaskDefinition> old;
            hr(existing->get_Definition(&old));
            CComPtr<IActionCollection> actions;
            hr(old->get_Actions(&actions));
            LONG count = 0;
            hr(actions->get_Count(&count));
            if (count != 1)
                fail(ERROR_ALREADY_EXISTS);
            CComPtr<IAction> action;
            hr(actions->get_Item(1, &action));
            CComPtr<IExecAction> execute;
            hr(action->QueryInterface(IID_PPV_ARGS(&execute)));
            CComBSTR path, arguments;
            hr(execute->get_Path(&path));
            hr(execute->get_Arguments(&arguments));
            const auto expected = L"complete-upgrade --prefix \"" +
                                  root_.wstring() + L"\" --transaction " +
                                  wide(id) + L" --silent";
            if (!path || !arguments ||
                !win32::ipc::sameInstallationPath(
                    std::wstring(path, path.Length()), host()) ||
                std::wstring_view(arguments, arguments.Length()) != expected)
                fail(ERROR_ALREADY_EXISTS);
        } else if (found != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
            hr(found);
        }
        if (!enable) {
            const auto error = folder->DeleteTask(CComBSTR(kTask), 0);
            if (error != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
                hr(error);
            return;
        }
        CComPtr<ITaskDefinition> definition;
        hr(service->NewTask(0, &definition));
        CComPtr<IPrincipal> principal;
        hr(definition->get_Principal(&principal));
        hr(principal->put_UserId(CComBSTR(L"SYSTEM")));
        hr(principal->put_LogonType(TASK_LOGON_SERVICE_ACCOUNT));
        hr(principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST));
        CComPtr<ITaskSettings> settings;
        hr(definition->get_Settings(&settings));
        hr(settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE));
        hr(settings->put_StopIfGoingOnBatteries(VARIANT_FALSE));
        hr(settings->put_AllowHardTerminate(VARIANT_FALSE));
        hr(settings->put_ExecutionTimeLimit(CComBSTR(L"PT0S")));
        hr(settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW));
        hr(settings->put_RestartInterval(CComBSTR(L"PT1M")));
        hr(settings->put_RestartCount(3));
        CComPtr<ITriggerCollection> triggers;
        hr(definition->get_Triggers(&triggers));
        CComPtr<ITrigger> trigger;
        hr(triggers->Create(TASK_TRIGGER_BOOT, &trigger));
        CComPtr<IActionCollection> actions;
        hr(definition->get_Actions(&actions));
        CComPtr<IAction> action;
        hr(actions->Create(TASK_ACTION_EXEC, &action));
        CComPtr<IExecAction> execute;
        hr(action->QueryInterface(IID_PPV_ARGS(&execute)));
        hr(execute->put_Path(CComBSTR(host().c_str())));
        const auto args = L"complete-upgrade --prefix \"" + root_.wstring() +
                          L"\" --transaction " + wide(id) + L" --silent";
        hr(execute->put_Arguments(CComBSTR(args.c_str())));
        CComPtr<IRegisteredTask> registered;
        hr(folder->RegisterTaskDefinition(
            CComBSTR(kTask), definition, TASK_CREATE_OR_UPDATE,
            CComVariant(L"SYSTEM"), CComVariant(), TASK_LOGON_SERVICE_ACCOUNT,
            CComVariant(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)"), &registered));
    }
    void controls(bool install, const fs::path &transaction) override {
        const auto backup = transaction / "controls.json";
        const auto command = L"\"" + host().wstring() +
                             L"\" uninstall --prefix \"" + root_.wstring() +
                             L"\"";
        const auto quiet = L"\"" + host().wstring() +
                           L"\" uninstall --silent --prefix \"" +
                           root_.wstring() + L"\"";
        if (install) {
            if (!fs::exists(backup))
                save(backup, snapshotKey(HKEY_LOCAL_MACHINE, kUninstall));
            setString(kUninstall, L"UninstallString", command);
            setString(kUninstall, L"QuietUninstallString", quiet);
        } else if (fs::exists(backup)) {
            // Cancel only our routing changes; preserve later installer
            // metadata.
            const auto snapshot = load(backup);
            for (const auto &[name, owned] :
                 {std::pair{L"UninstallString", command},
                  std::pair{L"QuietUninstallString", quiet}}) {
                try {
                    if (regString(kUninstall, name) != owned)
                        continue;
                } catch (const std::system_error &error) {
                    if (error.code().value() != ERROR_FILE_NOT_FOUND)
                        throw;
                }
                Key key;
                reg(RegOpenKeyExW(HKEY_LOCAL_MACHINE, kUninstall, 0,
                                  KEY_SET_VALUE | KEY_WOW64_64KEY, &key.value));
                bool restored = false;
                for (const auto &[unused, value] :
                     snapshot.get_child("values")) {
                    if (wide(value.get<std::string>("name")) == name) {
                        const auto bytes =
                            unhex(value.get<std::string>("data"));
                        reg(RegSetValueExW(key.value, name, 0,
                                           value.get<DWORD>("type"),
                                           bytes.data(), DWORD(bytes.size())));
                        restored = true;
                    }
                }
                if (!restored) {
                    const auto removed = RegDeleteValueW(key.value, name);
                    if (removed != ERROR_FILE_NOT_FOUND)
                        reg(removed);
                }
            }
        }
        const auto shortcut = shortcutPath();
        const auto linkBackup = transaction / "uninstall-link.bak";
        if (install && fs::exists(shortcut)) {
            if (!fs::exists(linkBackup))
                fs::copy_file(shortcut, linkBackup);
            CComPtr<IShellLinkW> link;
            hr(link.CoCreateInstance(CLSID_ShellLink));
            hr(link->SetPath(host().c_str()));
            const auto arguments =
                L"uninstall --prefix \"" + root_.wstring() + L"\"";
            hr(link->SetArguments(arguments.c_str()));
            CComPtr<IPersistFile> file;
            hr(link->QueryInterface(IID_PPV_ARGS(&file)));
            hr(file->Save(shortcut.c_str(), TRUE));
        } else if (!install && fs::exists(linkBackup) && fs::exists(shortcut)) {
            CComPtr<IShellLinkW> link;
            hr(link.CoCreateInstance(CLSID_ShellLink));
            CComPtr<IPersistFile> file;
            hr(link->QueryInterface(IID_PPV_ARGS(&file)));
            hr(file->Load(shortcut.c_str(), STGM_READ));
            std::wstring path(32768, L'\0');
            hr(link->GetPath(path.data(), int(path.size()), nullptr,
                             SLGP_RAWPATH));
            path.resize(wcsnlen(path.c_str(), path.size()));
            if (win32::ipc::sameInstallationPath(path, host()))
                fs::copy_file(linkBackup, shortcut,
                              fs::copy_options::overwrite_existing);
        }
    }
    void snapshot(const fs::path &transaction) override {
        Tree snapshot;
        snapshot.add_child("clsid", snapshotKey(HKEY_LOCAL_MACHINE, kClsid));
        snapshot.add_child("tip", snapshotKey(HKEY_LOCAL_MACHINE, kTip));
        snapshot.add_child("clsid32", snapshotKey(HKEY_LOCAL_MACHINE, kClsid,
                                                  true, 0, KEY_WOW64_32KEY));
        snapshot.add_child("tip32", snapshotKey(HKEY_LOCAL_MACHINE, kTip, true,
                                                0, KEY_WOW64_32KEY));
        snapshot.add_child(
            "installer",
            snapshotKey(HKEY_LOCAL_MACHINE, win32::ipc::kInstallerKey, false));
        snapshot.add_child("uninstall",
                           snapshotKey(HKEY_LOCAL_MACHINE, kUninstall));
        save(transaction / "registry.json", snapshot);
    }
    void restore(const fs::path &transaction) override {
        const auto snapshot = load(transaction / "registry.json");
        restoreKey(HKEY_LOCAL_MACHINE, kClsid, snapshot.get_child("clsid"));
        restoreKey(HKEY_LOCAL_MACHINE, kTip, snapshot.get_child("tip"));
        if (const auto key = snapshot.get_child_optional("clsid32"))
            restoreKey(HKEY_LOCAL_MACHINE, kClsid, *key, true, 0,
                       KEY_WOW64_32KEY);
        if (const auto key = snapshot.get_child_optional("tip32"))
            restoreKey(HKEY_LOCAL_MACHINE, kTip, *key, true, 0,
                       KEY_WOW64_32KEY);
        restoreKey(HKEY_LOCAL_MACHINE, win32::ipc::kInstallerKey,
                   snapshot.get_child("installer"), false);
        restoreKey(HKEY_LOCAL_MACHINE, kUninstall,
                   snapshot.get_child("uninstall"));
    }
    void registerVersion(const Manifest &value) override {
        if (std::any_of(value.files.begin(), value.files.end(),
                        [](const File &file) {
                            return file.path == "tsf/x86/fcitx5-x86.dll";
                        })) {
            wchar_t directory[MAX_PATH]{};
            if (!GetSystemWow64DirectoryW(directory, MAX_PATH))
                fail();
            const auto code = process(
                fs::path(directory) / L"regsvr32.exe",
                L"/s \"" + (root_ / "tsf/x86/fcitx5-x86.dll").wstring() +
                    L"\"");
            if (code)
                fail(code);
        }
        const auto dll = root_ / "tsf" / "fcitx5-x86_64.dll";
        auto module = LoadLibraryExW(dll.c_str(), nullptr,
                                     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                         LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module)
            fail();
        const auto method = reinterpret_cast<HRESULT(WINAPI *)()>(
            GetProcAddress(module, "DllRegisterServer"));
        const auto result = method ? method() : E_NOINTERFACE;
        FreeLibrary(module);
        hr(result);
        setString(kUninstall, L"DisplayVersion", wide(value.version));
        verifyVersion(value);
    }
    void verifyVersion(const Manifest &value) override {
        verifyTsf(root_ / "tsf/fcitx5-x86_64.dll", RRF_SUBKEY_WOW6464KEY,
                  KEY_WOW64_64KEY);
        if (std::any_of(value.files.begin(), value.files.end(),
                        [](const File &file) {
                            return file.path == "tsf/x86/fcitx5-x86.dll";
                        }))
            verifyTsf(root_ / "tsf/x86/fcitx5-x86.dll", RRF_SUBKEY_WOW6432KEY,
                      KEY_WOW64_32KEY);
        if (regString(kUninstall, L"DisplayVersion") != wide(value.version))
            fail(ERROR_INVALID_DATA);
    }
    void verifyTsf(const fs::path &dll, DWORD flags, REGSAM view) {
        const auto profile = std::wstring(kTip) +
                             L"\\LanguageProfile\\0x00000804\\"
                             L"{9A92B895-29B9-4F19-9627-9F626C9490F2}";
        if (!win32::ipc::sameInstallationPath(
                regString((std::wstring(kClsid) + L"\\InprocServer32").c_str(),
                          nullptr, flags),
                dll) ||
            regString((std::wstring(kClsid) + L"\\InprocServer32").c_str(),
                      L"ThreadingModel", flags) != L"Apartment")
            fail(ERROR_INVALID_DATA);
        for (const auto &[name, expected] :
             {std::pair{L"Enable", DWORD(1)},
              std::pair{L"IconIndex", DWORD(0)}}) {
            DWORD actual = 0, size = sizeof(actual);
            reg(RegGetValueW(HKEY_LOCAL_MACHINE, profile.c_str(), name,
                             RRF_RT_REG_DWORD | flags, nullptr, &actual,
                             &size));
            if (actual != expected)
                fail(ERROR_INVALID_DATA);
        }
        if (!win32::ipc::sameInstallationPath(
                regString(profile.c_str(), L"IconFile", flags), dll))
            fail(ERROR_INVALID_DATA);
        for (const auto &category : fcitx::RegistrationCategories()) {
            wchar_t name[40]{};
            if (!StringFromGUID2(category, name, 40))
                fail(ERROR_INVALID_DATA);
            constexpr wchar_t clsid[] =
                L"{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}";
            for (const auto &path :
                 {std::wstring(kTip) + L"\\Category\\Category\\" + name +
                      L"\\" + clsid,
                  std::wstring(kTip) + L"\\Category\\Item\\" + clsid + L"\\" +
                      name}) {
                Key key;
                reg(RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0,
                                  KEY_READ | view, &key.value));
            }
        }
    }
    DWORD dependencies(const fs::path &transaction,
                       const Manifest &value) override {
        for (const auto &file : value.dependencies) {
            const auto code = process(
                transaction / "dependencies" / relativePath(file.path),
                file.path == "vc_redist.x64.exe" ? L"/install /quiet /norestart"
                                                 : L"--quiet");
            if (code && !(file.path == "vc_redist.x64.exe" && code == 1638))
                return code;
        }
        return ERROR_SUCCESS;
    }
    fs::path host() const {
        return root_ / "setup" / "maintenance-host" / "Fcitx5Upgrade.exe";
    }
    void installHost() {
        loggedPhase("maintenance host directory", [&] {
            protectDirectory(root_ / "setup" / "maintenance-host");
        });
        loggedPhase("upgrade transaction directory",
                    [&] { protectDirectory(root_ / "setup" / "upgrades"); });
        const auto source = modulePath();
        if (source.filename() != L"Fcitx5Upgrade.exe")
            fail(ERROR_INVALID_FUNCTION);
        loggedPhase("temporary maintenance host security",
                    [&] { validateSecurity(source); });
        if (!fs::exists(host()) || sha256(host()) != sha256(source)) {
            const auto temporary = host().wstring() + L".new";
            validatePath(temporary, true);
            loggedPhase("install maintenance host", [&] {
                fs::copy_file(source, temporary,
                              fs::copy_options::overwrite_existing);
                if (!MoveFileExW(temporary.c_str(), host().c_str(),
                                 MOVEFILE_REPLACE_EXISTING |
                                     MOVEFILE_WRITE_THROUGH))
                    fail();
            });
        }
        loggedPhase("installed maintenance host security",
                    [&] { validateSecurity(host()); });
    }
    void checkTarget(const Manifest &value) {
        const auto current = regString(kUninstall, L"DisplayVersion");
        // Four numeric components, not lexical string ordering.
        auto numbers = [](const std::wstring &text) {
            std::vector<unsigned long> result;
            std::wistringstream stream(text);
            std::wstring part;
            while (std::getline(stream, part, L'.')) {
                if (part.empty() || part.size() > 5 ||
                    part.find_first_not_of(L"0123456789") != std::wstring::npos)
                    fail(ERROR_INVALID_DATA);
                result.push_back(std::stoul(part));
            }
            if (result.size() != 4)
                fail(ERROR_INVALID_DATA);
            return result;
        };
        if (numbers(wide(value.version)) < numbers(current))
            fail(ERROR_REVISION_MISMATCH);
    }

  private:
    fs::path shortcutPath() {
        PWSTR directory = nullptr;
        hr(SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr,
                                &directory));
        const auto path =
            fs::path(directory) / "Fcitx5" / "Uninstall Fcitx5.lnk";
        CoTaskMemFree(directory);
        validatePath(path, true);
        return path;
    }
    void pendingOperations() {
        constexpr wchar_t path[] =
            L"SYSTEM\\CurrentControlSet\\Control\\Session Manager";
        DWORD size = 0;
        const auto first = RegGetValueW(
            HKEY_LOCAL_MACHINE, path, L"PendingFileRenameOperations",
            RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size);
        if (first == ERROR_FILE_NOT_FOUND)
            return;
        reg(first);
        if (size > 16 * 1024 * 1024 || size % sizeof(wchar_t))
            fail(ERROR_INVALID_DATA);
        std::vector<wchar_t> data(size / sizeof(wchar_t) + 2, 0);
        reg(RegGetValueW(HKEY_LOCAL_MACHINE, path,
                         L"PendingFileRenameOperations", RRF_RT_REG_MULTI_SZ,
                         nullptr, data.data(), &size));
        const auto prefix =
            root_.lexically_normal().make_preferred().wstring() + L"\\";
        for (size_t i = 0; i < size / sizeof(wchar_t);) {
            auto text = std::wstring(
                data.data() + i,
                wcsnlen(data.data() + i, size / sizeof(wchar_t) - i));
            i += text.size() + 1;
            if (text.starts_with(L"!"))
                text.erase(0, 1);
            if (text.starts_with(L"\\??\\"))
                text.erase(0, 4);
            if (win32::ipc::sameInstallationPath(text, root_) ||
                (text.size() >= prefix.size() &&
                 CompareStringOrdinal(text.data(), int(prefix.size()),
                                      prefix.data(), int(prefix.size()),
                                      TRUE) == CSTR_EQUAL))
                fail(ERROR_SUCCESS_REBOOT_REQUIRED);
        }
    }
    fs::path root_;
};
} // namespace

std::unique_ptr<Machine> nativeMachine(const fs::path &root) {
    return std::make_unique<NativeMachine>(root);
}
bool upgradeCommand(std::wstring_view command) {
    return command == L"begin-stage" || command == L"seal-stage" ||
           command == L"cancel-upgrade" || command == L"complete-upgrade" ||
           command == L"recover-upgrade" || command == L"cleanup-upgrade" ||
           command == L"query-upgrade";
}
DWORD runUpgrade(const fs::path &root, std::wstring_view command,
                 const fs::path &source, std::string_view transaction,
                 bool preview, bool silent) {
    logEnvironment();
    loggedPhase("host-services policy", [] { ipc::requireHostServices(); });
    loggedPhase("installation root argument", [&] {
        if (!root.is_absolute() || root == root.root_path())
            fail(ERROR_INVALID_NAME);
    });
    auto machine = nativeMachine(root);
    Engine engine(root, *machine, parentProcess());
    if (command == L"query-upgrade") {
        logMessage("upgrade phase=query state begin");
        const auto value = engine.query();
        logMessage("upgrade phase=query state complete");
        std::cout << value;
        if (!silent)
            MessageBoxW(nullptr, wide(value).c_str(), L"Fcitx5 upgrade",
                        MB_OK | MB_ICONINFORMATION);
        return ERROR_SUCCESS;
    }
    loggedPhase("machine validation", [&] { machine->validate(); });
    if (preview)
        return ERROR_SUCCESS;
    logMessage("upgrade phase=installation lock begin");
    InstallationLock lock(root);
    logMessage("upgrade phase=installation lock complete");
    if (!transaction.empty() && hasTransaction(root) &&
        load(root / "setup" / "upgrades" / "current" / "plan.json")
                .get<std::string>("id") != transaction)
        fail(ERROR_INVALID_DATA);
    if (command == L"begin-stage") {
        auto &native = static_cast<NativeMachine &>(*machine);
        loggedPhase("target package validation",
                    [&] { native.checkTarget(manifest(source)); });
        loggedPhase("current installation state", [&] {
            if (readText(root / "setup" / "install-state", 64) != "installed\n")
                fail(ERROR_INVALID_STATE);
        });
        if (hasTransaction(root) && (transactionState(root) == "committed" ||
                                     transactionState(root) == "rolled-back"))
            loggedPhase("old transaction cleanup", [&] { engine.cleanup(); });
        if (!hasTransaction(root))
            native.installHost();
        DWORD result = ERROR_GEN_FAILURE;
        loggedPhase("create upgrade transaction",
                    [&] { result = engine.begin(source); });
        return result;
    }
    if (command == L"seal-stage")
        return engine.seal();
    if (command == L"cancel-upgrade")
        return engine.cancel();
    if (command == L"complete-upgrade" || command == L"recover-upgrade") {
        if (!hasTransaction(root) && !transaction.empty())
            machine->task(false, transaction);
        return engine.complete(command == L"recover-upgrade");
    }
    if (command == L"cleanup-upgrade")
        return engine.cleanup();
    fail(ERROR_INVALID_PARAMETER);
}
} // namespace fcitx::setup::upgrade
