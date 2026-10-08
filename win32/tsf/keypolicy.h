#pragma once

#include <cstdint>
#include <windows.h>

namespace fcitx {

inline bool routesKey(uint32_t key, uint32_t modifiers, bool enabled,
                      bool composing) {
    if (key == VK_SPACE && (modifiers & 0x0f) == 2) {
        return true;
    }
    if (!enabled || (modifiers & 0x0e)) {
        return false;
    }
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') ||
        key == VK_SPACE || (key >= VK_OEM_1 && key <= VK_OEM_3) ||
        (key >= VK_OEM_4 && key <= VK_OEM_8) || key == VK_OEM_102) {
        return true;
    }
    if (!composing) {
        return false;
    }
    switch (key) {
    case VK_BACK:
    case VK_DELETE:
    case VK_RETURN:
    case VK_ESCAPE:
    case VK_TAB:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
        return true;
    default:
        return false;
    }
}

} // namespace fcitx
