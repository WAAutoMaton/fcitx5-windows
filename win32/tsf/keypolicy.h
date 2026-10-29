#pragma once

#include <cstdint>
#include <windows.h>

namespace fcitx {

constexpr uint32_t kModifierShift = 1U << 0;
constexpr uint32_t kModifierControl = 1U << 1;
constexpr uint32_t kModifierAlt = 1U << 2;
constexpr uint32_t kModifierSuper = 1U << 3;

inline bool routesKey(uint32_t key, uint32_t modifiers, bool enabled,
                      bool composing, bool codeInput = false) {
    if (!enabled ||
        (modifiers & (kModifierControl | kModifierAlt | kModifierSuper))) {
        return false;
    }
    if (key >= 'A' && key <= 'Z') {
        return true;
    }
    // Idle Pinyin still needs punctuation conversion. Shifted digits also
    // produce punctuation (e.g. ! and parentheses); Core decides consumption.
    if (((composing || codeInput || (modifiers & kModifierShift)) &&
         key >= '0' && key <= '9') ||
        ((composing || codeInput) && key == VK_SPACE) ||
        (key >= VK_OEM_1 && key <= VK_OEM_3) ||
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
