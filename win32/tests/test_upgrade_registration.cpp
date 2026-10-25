#include "../dll/register.h"
#include "../ipc/installation.h"
#include "../ipc/isolationpolicy.h"
#include "../setup/log.h"
#include "../setup/upgrade.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <set>

namespace {
namespace fs = std::filesystem;
using Value = std::pair<std::wstring, std::wstring>;
std::map<Value, std::wstring> strings;
std::map<Value, DWORD> numbers;
std::set<std::wstring> categories;
constexpr wchar_t clsidKey[] = L"SOFTWARE\\Classes\\CLSID\\{FC3869BA-51E3-4078-"
                               L"8EE2-5FE49493A1F4}\\InprocServer32";
constexpr wchar_t tipKey[] =
    L"SOFTWARE\\Microsoft\\CTF\\TIP\\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}";
const auto profileKey = std::wstring(tipKey) +
                        L"\\LanguageProfile\\0x00000804\\"
                        L"{9A92B895-29B9-4F19-9627-9F626C9490F2}";
constexpr wchar_t uninstallKey[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
    L"{0D07698A-591B-44B4-940F-BBD38AA62F0E}_is1";

Value registryValue(LPCWSTR key, LPCWSTR name, DWORD flags) {
    std::wstring path = key;
    if (path == clsidKey) {
        path += flags & RRF_SUBKEY_WOW6432KEY ? L"/x86" : L"/x64";
    } else {
        // Profile/category metadata must be checked once through the shared
        // key.
        assert(flags & RRF_SUBKEY_WOW6464KEY);
    }
    return {path, name ? name : L""};
}
LSTATUS WINAPI readValue(HKEY root, LPCWSTR key, LPCWSTR name, DWORD flags,
                         LPDWORD, PVOID data, LPDWORD size) {
    assert(root == HKEY_LOCAL_MACHINE);
    const auto value = registryValue(key, name, flags);
    if (flags & RRF_RT_REG_DWORD) {
        if (!numbers.contains(value))
            return ERROR_FILE_NOT_FOUND;
        assert(*size >= sizeof(DWORD));
        memcpy(data, &numbers.at(value), sizeof(DWORD));
        *size = sizeof(DWORD);
    } else {
        assert(flags & RRF_RT_REG_SZ);
        if (!strings.contains(value))
            return ERROR_FILE_NOT_FOUND;
        const auto &text = strings.at(value);
        const auto bytes = (text.size() + 1) * sizeof(wchar_t);
        assert(*size >= bytes);
        memcpy(data, text.c_str(), bytes);
        *size = static_cast<DWORD>(bytes);
    }
    return ERROR_SUCCESS;
}
LSTATUS WINAPI openKey(HKEY root, LPCWSTR path, DWORD, REGSAM access,
                       PHKEY key) {
    assert(root == HKEY_LOCAL_MACHINE);
    assert(access == (KEY_READ | KEY_WOW64_64KEY));
    *key = nullptr;
    if (!categories.contains(path))
        return ERROR_FILE_NOT_FOUND;
    *key = reinterpret_cast<HKEY>(1);
    return ERROR_SUCCESS;
}
LSTATUS WINAPI closeKey(HKEY key) {
    assert(key == reinterpret_cast<HKEY>(1));
    return ERROR_SUCCESS;
}
} // namespace

#define RegGetValueW readValue
#define RegOpenKeyExW openKey
#define RegCloseKey closeKey
#include "../setup/upgrade_system.cpp"

int main() {
    namespace upgrade = fcitx::setup::upgrade;
    const fs::path root = L"C:\\Fcitx5 install";
    const auto x64 = root / "tsf/fcitx5-x86_64.dll";
    const auto x86 = root / "tsf/x86/fcitx5-x86.dll";
    const auto server64 = std::wstring(clsidKey) + L"/x64";
    const auto server32 = std::wstring(clsidKey) + L"/x86";
    strings[{server64, L""}] = x64.wstring();
    strings[{server32, L""}] = x86.wstring();
    strings[{server64, L"ThreadingModel"}] = L"Apartment";
    strings[{server32, L"ThreadingModel"}] = L"Apartment";
    strings[{uninstallKey, L"DisplayVersion"}] = L"1.2.3";
    numbers[{profileKey, L"Enable"}] = 1;
    numbers[{profileKey, L"IconIndex"}] = 0;
    for (const auto &category : fcitx::RegistrationCategories()) {
        wchar_t guid[40]{};
        assert(StringFromGUID2(category, guid, 40));
        constexpr wchar_t clsid[] = L"{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}";
        categories.insert(std::wstring(tipKey) + L"\\Category\\Category\\" +
                          guid + L"\\" + clsid);
        categories.insert(std::wstring(tipKey) + L"\\Category\\Item\\" + clsid +
                          L"\\" + guid);
    }
    upgrade::Manifest single;
    single.version = "1.2.3";
    single.files.push_back({"tsf/fcitx5-x86_64.dll"});
    auto dual = single;
    dual.files.push_back({"tsf/x86/fcitx5-x86.dll"});
    const auto machine = upgrade::nativeMachine(root);
    const auto reject = [&](const upgrade::Manifest &manifest,
                            DWORD expected = ERROR_INVALID_DATA) {
        bool failed = false;
        try {
            machine->verifyVersion(manifest);
        } catch (const std::system_error &error) {
            assert(error.code().value() == expected);
            failed = true;
        }
        assert(failed);
    };
    strings[{profileKey, L"IconFile"}] = x64.wstring();
    machine->verifyVersion(single);
    machine->verifyVersion(dual);
    strings[{profileKey, L"IconFile"}] = x86.wstring();
    machine->verifyVersion(dual);
    reject(single);
    strings[{profileKey, L"IconFile"}] = L"C:\\Other\\fcitx5-x86.dll";
    reject(dual);
    strings[{profileKey, L"IconFile"}] = x64.wstring();
    strings[{server32, L""}] = x64.wstring();
    reject(dual);
    strings[{server32, L""}] = x86.wstring();
    strings[{server64, L"ThreadingModel"}] = L"Both";
    reject(dual);
    strings[{server64, L"ThreadingModel"}] = L"Apartment";
    numbers[{profileKey, L"Enable"}] = 0;
    reject(dual);
    numbers[{profileKey, L"Enable"}] = 1;
    numbers[{profileKey, L"IconIndex"}] = 1;
    reject(dual);
    numbers[{profileKey, L"IconIndex"}] = 0;
    const auto saved = categories;
    for (const auto &category : saved) {
        categories.erase(category);
        reject(dual, ERROR_FILE_NOT_FOUND);
        categories.insert(category);
    }
    strings[{uninstallKey, L"DisplayVersion"}] = L"old";
    reject(dual);
    std::cout
        << "PASS: upgrade verifies both COM views and shared TSF metadata "
           "without system registration\n";
}
