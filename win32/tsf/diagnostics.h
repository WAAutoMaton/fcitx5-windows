#pragma once

#include <cwchar>
#include <windows.h>

namespace fcitx {
inline void diagnosticResult(const wchar_t *stage, HRESULT result) {
    WCHAR text[192]{};
    swprintf_s(text, L"Fcitx5: %s hr=0x%08lx\n", stage,
               static_cast<unsigned long>(result));
    OutputDebugStringW(text);
}
} // namespace fcitx
