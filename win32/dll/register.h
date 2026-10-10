#pragma once

#include <Windows.h>
#include <msctf.h>
#include <string>

namespace fcitx {
extern HINSTANCE dllInstance;

BOOL RegisterServer();
HRESULT RegisterServerAt(HKEY root, const std::wstring &dllPath);
HRESULT UnregisterServer();
BOOL RegisterProfiles();
BOOL RegisterCategories();
HRESULT RegisterProfile(ITfInputProcessorProfileMgr *manager, HINSTANCE module);
HRESULT RegisterCategories(ITfCategoryMgr *manager);
HRESULT UnregisterCategoriesAndProfiles();
} // namespace fcitx
