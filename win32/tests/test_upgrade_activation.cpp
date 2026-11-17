#include "../setup/upgrade.h"
#include <cassert>
#include <iostream>

namespace {
std::wstring registryRoot;
bool failRestore32 = false;
std::wstring redirected(LPCWSTR path, DWORD view) {
    return registryRoot + (view & KEY_WOW64_32KEY ? L"\\x86\\" : L"\\x64\\") +
           path;
}
LSTATUS WINAPI openIsolated(HKEY parent, LPCWSTR path, DWORD options,
                            REGSAM access, PHKEY key) {
    if (access & KEY_SET_VALUE)
        assert(access & KEY_QUERY_VALUE);
    if (parent == HKEY_LOCAL_MACHINE) {
        const auto name = redirected(path, access);
        return RegOpenKeyExW(HKEY_CURRENT_USER, name.c_str(), options, access,
                             key);
    }
    return RegOpenKeyExW(parent, path, options, access, key);
}
LSTATUS WINAPI createIsolated(HKEY parent, LPCWSTR path, DWORD reserved,
                              LPWSTR cls, DWORD options, REGSAM access,
                              LPSECURITY_ATTRIBUTES security, PHKEY key,
                              LPDWORD disposition) {
    if (parent == HKEY_LOCAL_MACHINE) {
        if (failRestore32 && (access & KEY_WOW64_32KEY))
            return ERROR_ACCESS_DENIED;
        const auto name = redirected(path, access);
        return RegCreateKeyExW(HKEY_CURRENT_USER, name.c_str(), reserved, cls,
                               options, access, security, key, disposition);
    }
    return RegCreateKeyExW(parent, path, reserved, cls, options, access,
                           security, key, disposition);
}
LSTATUS WINAPI deleteIsolated(HKEY parent, LPCWSTR path, REGSAM view,
                              DWORD reserved) {
    if (parent == HKEY_LOCAL_MACHINE) {
        const auto name = redirected(path, view);
        return RegDeleteKeyExW(HKEY_CURRENT_USER, name.c_str(), view, reserved);
    }
    return RegDeleteKeyExW(parent, path, view, reserved);
}
} // namespace

#define RegOpenKeyExW openIsolated
#define RegCreateKeyExW createIsolated
#define RegDeleteKeyExW deleteIsolated
#include "../setup/upgrade_system.cpp"
#undef RegOpenKeyExW
#undef RegCreateKeyExW
#undef RegDeleteKeyExW

int main() {
    namespace upgrade = fcitx::setup::upgrade;
    registryRoot = L"Software\\Fcitx5-UpgradeActivation-Test-" +
                   std::to_wstring(GetCurrentProcessId());
    const auto transaction =
        std::filesystem::temp_directory_path() /
        ("fcitx5-activation-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(transaction);
    const std::wstring clsid = L"CLSID\\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}";
    const auto server = clsid + L"\\InprocServer32";
    for (auto view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        HKEY key = nullptr;
        const auto name = redirected(server.c_str(), view);
        assert(RegCreateKeyExW(HKEY_CURRENT_USER, name.c_str(), 0, nullptr, 0,
                               KEY_ALL_ACCESS, nullptr, &key, nullptr) == 0);
        const std::wstring path = view == KEY_WOW64_32KEY
                                      ? L"C:\\Fcitx5\\tsf\\x86\\fcitx5-x86.dll"
                                      : L"C:\\Fcitx5\\tsf\\fcitx5-x86_64.dll";
        assert(RegSetValueExW(key, nullptr, 0, REG_SZ,
                              reinterpret_cast<const BYTE *>(path.c_str()),
                              DWORD((path.size() + 1) * sizeof(wchar_t))) == 0);
        const wchar_t threading[] = L"Apartment";
        assert(RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ,
                              reinterpret_cast<const BYTE *>(threading),
                              sizeof(threading)) == 0);
        RegCloseKey(key);
    }
    const auto original64 = upgrade::snapshotKey(HKEY_LOCAL_MACHINE, clsid);
    const auto original32 = upgrade::snapshotKey(HKEY_LOCAL_MACHINE, clsid,
                                                 true, 0, KEY_WOW64_32KEY);
    const auto matches = [&](const auto &left, const auto &right) {
        std::ostringstream a, b;
        boost::property_tree::write_json(a, left);
        boost::property_tree::write_json(b, right);
        return a.str() == b.str();
    };
    upgrade::suspendComActivation(HKEY_LOCAL_MACHINE, clsid, transaction);
    for (auto view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        HKEY key = nullptr;
        assert(openIsolated(HKEY_LOCAL_MACHINE, server.c_str(), 0,
                            KEY_READ | view, &key) == 0);
        DWORD bytes = 0;
        assert(RegQueryValueExW(key, nullptr, nullptr, nullptr, nullptr,
                                &bytes) == ERROR_FILE_NOT_FOUND);
        assert(RegQueryValueExW(key, L"ThreadingModel", nullptr, nullptr,
                                nullptr, &bytes) == 0);
        RegCloseKey(key);
    }
    // Repeated suspension must retain the original paths, including after a
    // crash between removal of the two views.
    upgrade::suspendComActivation(HKEY_LOCAL_MACHINE, clsid, transaction);
    assert(matches(original64,
                   upgrade::classSnapshot(HKEY_LOCAL_MACHINE, clsid,
                                          transaction, KEY_WOW64_64KEY)));
    assert(matches(original32,
                   upgrade::classSnapshot(HKEY_LOCAL_MACHINE, clsid,
                                          transaction, KEY_WOW64_32KEY)));
    failRestore32 = true;
    try {
        upgrade::resumeComActivation(HKEY_LOCAL_MACHINE, clsid, transaction);
        assert(false);
    } catch (const std::system_error &error) {
        assert(error.code().value() == ERROR_ACCESS_DENIED);
    }
    assert(std::filesystem::exists(transaction / "activation.json"));
    failRestore32 = false;
    upgrade::resumeComActivation(HKEY_LOCAL_MACHINE, clsid, transaction);
    assert(
        matches(original64, upgrade::snapshotKey(HKEY_LOCAL_MACHINE, clsid)));
    assert(matches(original32, upgrade::snapshotKey(HKEY_LOCAL_MACHINE, clsid,
                                                    true, 0, KEY_WOW64_32KEY)));
    assert(!std::filesystem::exists(transaction / "activation.json"));
    upgrade::resumeComActivation(HKEY_LOCAL_MACHINE, clsid, transaction);
    assert(RegDeleteTreeW(HKEY_CURRENT_USER, registryRoot.c_str()) == 0);
    RegDeleteKeyW(HKEY_CURRENT_USER, registryRoot.c_str());
    std::filesystem::remove_all(transaction);
    std::cout
        << "PASS: isolated COM path suspension, both views, preserved "
           "metadata, original rollback snapshot and interrupted restore\n";
}
