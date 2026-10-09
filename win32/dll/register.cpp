#include "register.h"
#include "resource.h"
#include "util.h"
#include <atlcomcli.h>
#include <msctf.h>

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
    DWORD dw;
    HKEY hKey = nullptr;
    HKEY hSubKey = nullptr;
    WCHAR dllPath[MAX_PATH];
    auto achIMEKey = "CLSID\\" + guidToString(FCITX_CLSID);
    BOOL ret = RegCreateKeyExA(HKEY_CLASSES_ROOT, achIMEKey.c_str(), 0, nullptr,
                               REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                               &hKey, &dw);
    ret |=
        RegSetValueExA(hKey, nullptr, 0, REG_SZ,
                       reinterpret_cast<const BYTE *>(FCITX5), sizeof FCITX5);
    ret |= RegCreateKeyExA(hKey, "InprocServer32", 0, nullptr,
                           REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                           &hSubKey, &dw);
    auto hr = GetModuleFileNameW(dllInstance, dllPath, MAX_PATH);
    ret |= RegSetValueExW(hSubKey, nullptr, 0, REG_SZ,
                          reinterpret_cast<const BYTE *>(dllPath),
                          hr * sizeof(WCHAR));
    ret |= RegSetValueExA(hSubKey, THREADING_MODEL, 0, REG_SZ,
                          reinterpret_cast<const BYTE *>(APARTMENT),
                          sizeof APARTMENT);
    RegCloseKey(hSubKey);
    RegCloseKey(hKey);
    return ret == ERROR_SUCCESS;
}

void UnregisterServer() {
    auto achIMEKey = "CLSID\\" + guidToString(FCITX_CLSID);
    RegDeleteTreeA(HKEY_CLASSES_ROOT, achIMEKey.c_str());
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

// Desktop input-indicator support does not declare Windows Store compatibility.
const GUID Categories[] = {GUID_TFCAT_TIP_KEYBOARD,
                           GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
                           GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT};

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
    for (const auto &guid : Categories) {
        const auto result =
            manager->RegisterCategory(FCITX_CLSID, guid, FCITX_CLSID);
        if (FAILED(result)) {
            return result;
        }
    }
    return S_OK;
}

void UnregisterCategoriesAndProfiles() {
    auto key = "SOFTWARE\\Microsoft\\CTF\\TIP\\" + guidToString(FCITX_CLSID);
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, key.c_str());
}
} // namespace fcitx
