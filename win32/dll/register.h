#pragma once

#include <Windows.h>
#include <msctf.h>
#include <span>
#include <string>

namespace fcitx {
extern HINSTANCE dllInstance;

BOOL RegisterServer();
inline constexpr REGSAM kRegistrationView =
    sizeof(void *) == 4 ? KEY_WOW64_32KEY : KEY_WOW64_64KEY;
HRESULT RegisterServerAt(HKEY root, const std::wstring &dllPath,
                         REGSAM view = kRegistrationView);
HRESULT UnregisterServerAt(HKEY root, REGSAM view = kRegistrationView);
HRESULT UnregisterServer();
BOOL RegisterProfiles();
BOOL RegisterCategories();
std::span<const GUID> RegistrationCategories();
HRESULT RegisterProfile(ITfInputProcessorProfileMgr *manager, HINSTANCE module);
HRESULT RegisterCategories(ITfCategoryMgr *manager);
HRESULT UnregisterCategories(ITfCategoryMgr *manager);
HRESULT UnregisterCategoriesAndProfiles();
} // namespace fcitx
