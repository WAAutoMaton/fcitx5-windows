#include "register.h"
#include "resource.h"
#include "util.h"
#include <atlcomcli.h>
#include <msctf.h>
#include <vector>

#define FCITX5 "Fcitx5"
#define THREADING_MODEL "ThreadingModel"
#define APARTMENT "Apartment"

namespace fcitx {
HINSTANCE dllInstance; // Set by DllMain.

/*
HKEY_CLASSES_ROOT\CLSID\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}: Fcitx5
  - InprocServer32: C:\Windows\system32
    ThreadingModel: Apartment
*/
BOOL RegisterServer() {
    wchar_t dllPath[32768]{};
    const auto size =
        GetModuleFileNameW(dllInstance, dllPath, ARRAYSIZE(dllPath));
    if (!size || size >= ARRAYSIZE(dllPath)) {
        return FALSE;
    }
    HKEY classes = nullptr;
    const auto opened = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes", 0, nullptr, 0,
        KEY_READ | KEY_WRITE | kRegistrationView, nullptr, &classes, nullptr);
    if (opened != ERROR_SUCCESS) {
        return FALSE;
    }
    const auto result = RegisterServerAt(classes, dllPath);
    RegCloseKey(classes);
    return SUCCEEDED(result);
}

HRESULT RegisterServerAt(HKEY root, const std::wstring &dllPath, REGSAM view) {
    if (!root || dllPath.empty() || dllPath.find(L'\0') != std::wstring::npos ||
        (view != KEY_WOW64_32KEY && view != KEY_WOW64_64KEY)) {
        return E_INVALIDARG;
    }
    const auto key =
        L"CLSID\\" + stringToWString(guidToString(FCITX_CLSID), CP_UTF8);
    HKEY clsid = nullptr;
    auto result =
        RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0,
                        KEY_READ | KEY_WRITE | view, nullptr, &clsid, nullptr);
    if (result != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(result);
    }
    constexpr wchar_t name[] = L"Fcitx5";
    result = RegSetValueExW(clsid, nullptr, 0, REG_SZ,
                            reinterpret_cast<const BYTE *>(name), sizeof(name));
    HKEY server = nullptr;
    if (result == ERROR_SUCCESS) {
        result = RegCreateKeyExW(clsid, L"InprocServer32", 0, nullptr, 0,
                                 KEY_WRITE | view, nullptr, &server, nullptr);
    }
    if (result == ERROR_SUCCESS) {
        result = RegSetValueExW(
            server, nullptr, 0, REG_SZ,
            reinterpret_cast<const BYTE *>(dllPath.c_str()),
            static_cast<DWORD>((dllPath.size() + 1) * sizeof(wchar_t)));
    }
    constexpr wchar_t threading[] = L"Apartment";
    if (result == ERROR_SUCCESS) {
        result = RegSetValueExW(server, L"ThreadingModel", 0, REG_SZ,
                                reinterpret_cast<const BYTE *>(threading),
                                sizeof(threading));
    }
    if (server) {
        RegCloseKey(server);
    }
    RegCloseKey(clsid);
    return HRESULT_FROM_WIN32(result);
}

HRESULT UnregisterServer() {
    HKEY classes = nullptr;
    const auto opened =
        RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes", 0,
                      KEY_READ | KEY_WRITE | kRegistrationView, &classes);
    if (opened == ERROR_FILE_NOT_FOUND)
        return S_OK;
    if (opened != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(opened);
    const auto result = UnregisterServerAt(classes);
    RegCloseKey(classes);
    return result;
}

HRESULT UnregisterServerAt(HKEY root, REGSAM view) {
    if (!root || (view != KEY_WOW64_32KEY && view != KEY_WOW64_64KEY))
        return E_INVALIDARG;
    HKEY classes = nullptr;
    const auto opened =
        RegOpenKeyExW(root, L"CLSID", 0, KEY_READ | KEY_WRITE | view, &classes);
    if (opened == ERROR_FILE_NOT_FOUND)
        return S_OK;
    if (opened != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(opened);
    const auto result =
        RegDeleteTreeA(classes, guidToString(FCITX_CLSID).c_str());
    RegCloseKey(classes);
    return result == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(result);
}

/*
HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\CTF\TIP\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}
  - LanguageProfile
    - 0x00000804
      - {9A92B895-29B9-4F19-9627-9F626C9490F2}
        Description: Fcitx5
        Enable: 0x00000001
        IconFile: /path/to/fcitx5-x86_64.dll
        IconIndex: 0x00000000
*/
BOOL RegisterProfiles() {
    CComPtr<ITfInputProcessorProfileMgr> mgr;
    if (FAILED(mgr.CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                    CLSCTX_ALL))) {
        return FALSE;
    }
    return SUCCEEDED(RegisterProfile(mgr, dllInstance));
}

HRESULT RegisterProfile(ITfInputProcessorProfileMgr *manager,
                        HINSTANCE module) {
    if (!manager || !module) {
        return E_INVALIDARG;
    }
    if (!FindResourceA(module, MAKEINTRESOURCEA(IDI_FCITX5), RT_GROUP_ICON)) {
        return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
    }
    WCHAR dllPath[32768]{};
    const auto length = GetModuleFileNameW(module, dllPath, ARRAYSIZE(dllPath));
    if (!length) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (length >= ARRAYSIZE(dllPath)) {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    constexpr WCHAR description[] = L"Fcitx5";
    // The DLL contains one group icon; profile indices are zero-based.
    return manager->RegisterProfile(
        FCITX_CLSID, TEXTSERVICE_LANGID_HANS, PROFILE_GUID, description,
        ARRAYSIZE(description) - 1, dllPath, length, 0, nullptr, 0, TRUE, 0);
}

// Modern categories are enabled explicitly until signed system acceptance.
const GUID Categories[] = {
    GUID_TFCAT_TIP_KEYBOARD, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT
#ifdef FCITX5_MODERN_TSF_CATEGORIES
    ,
    GUID_TFCAT_TIPCAP_UIELEMENTENABLED, GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT
#endif
};

std::span<const GUID> RegistrationCategories() { return Categories; }

/*
HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\CTF\TIP\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}
  - Category
    - Category
      - GUID of categories
        - {FC3869BA-51E3-4078-8EE2-5FE49493A1F4}
      - ...
    - Item
      - {FC3869BA-51E3-4078-8EE2-5FE49493A1F4}
        - GUID of categories
        - ...
*/
BOOL RegisterCategories() {
    CComPtr<ITfCategoryMgr> mgr;
    if (FAILED(mgr.CoCreateInstance(CLSID_TF_CategoryMgr))) {
        return FALSE;
    }
    return SUCCEEDED(RegisterCategories(mgr));
}

HRESULT RegisterCategories(ITfCategoryMgr *manager) {
    if (!manager) {
        return E_INVALIDARG;
    }
    size_t completed = 0;
    for (const auto &guid : Categories) {
        const auto result =
            manager->RegisterCategory(FCITX_CLSID, guid, FCITX_CLSID);
        if (FAILED(result)) {
            while (completed) {
                const auto &registered = Categories[--completed];
                manager->UnregisterCategory(FCITX_CLSID, registered,
                                            FCITX_CLSID);
            }
            return result;
        }
        ++completed;
    }
    return S_OK;
}

HRESULT UnregisterCategories(ITfCategoryMgr *manager) {
    if (!manager)
        return E_INVALIDARG;
    CComPtr<IEnumGUID> categories;
    auto result = manager->EnumCategoriesInItem(FCITX_CLSID, &categories);
    if (FAILED(result) || !categories)
        return FAILED(result) ? result : E_FAIL;
    HRESULT firstError = S_OK;
    std::vector<GUID> registered;
    GUID guid{};
    ULONG fetched = 0;
    while ((result = categories->Next(1, &guid, &fetched)) == S_OK && fetched) {
        registered.push_back(guid);
    }
    if (FAILED(result))
        return result;
    for (const auto &guid : registered) {
        const auto removed =
            manager->UnregisterCategory(FCITX_CLSID, guid, FCITX_CLSID);
        if (FAILED(removed) && SUCCEEDED(firstError))
            firstError = removed;
    }
    return FAILED(firstError) ? firstError : FAILED(result) ? result : S_OK;
}

HRESULT UnregisterCategoriesAndProfiles() {
    const auto key =
        "SOFTWARE\\Microsoft\\CTF\\TIP\\" + guidToString(FCITX_CLSID);
    HKEY existing = nullptr;
    const auto opened = RegOpenKeyExA(HKEY_LOCAL_MACHINE, key.c_str(), 0,
                                      KEY_READ | kRegistrationView, &existing);
    if (opened == ERROR_FILE_NOT_FOUND) {
        return S_OK;
    }
    if (opened != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(opened);
    }
    RegCloseKey(existing);
    CComPtr<ITfCategoryMgr> categories;
    auto result = categories.CoCreateInstance(CLSID_TF_CategoryMgr);
    if (FAILED(result)) {
        return result;
    }
    const auto categoryResult = UnregisterCategories(categories);
    CComPtr<ITfInputProcessorProfiles> profiles;
    result = profiles.CoCreateInstance(CLSID_TF_InputProcessorProfiles);
    if (FAILED(result)) {
        return result;
    }
    result = profiles->Unregister(FCITX_CLSID);
    return FAILED(categoryResult) ? categoryResult : result;
}
} // namespace fcitx
