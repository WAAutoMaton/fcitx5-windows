#pragma once

#include <Windows.h>
#include <msctf.h>

namespace fcitx {
extern HINSTANCE dllInstance;

BOOL RegisterServer();
void UnregisterServer();
BOOL RegisterProfiles();
BOOL RegisterCategories();
HRESULT RegisterProfile(ITfInputProcessorProfileMgr *manager, HINSTANCE module);
HRESULT RegisterCategories(ITfCategoryMgr *manager);
void UnregisterCategoriesAndProfiles();
} // namespace fcitx
