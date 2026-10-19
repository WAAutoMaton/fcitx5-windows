#pragma once

#include "portablekey.h"
#include "protocol.h"
#include <algorithm>
#include <charconv>
#include <sstream>

namespace fcitx::win32::ipc {

struct ShortcutKey {
    UINT key = 0;
    UINT scan = 0;
    unsigned modifiers = 0;

    bool matches(UINT actualKey, UINT actualScan,
                 unsigned actualModifiers) const {
        if (scan) {
            if (actualScan != scan)
                return false;
        } else {
            // TSF may report the generic virtual key for a sided modifier.
            if (actualKey == VK_SHIFT)
                actualKey = actualScan == 0x36 ? VK_RSHIFT : VK_LSHIFT;
            if (actualKey == VK_CONTROL)
                actualKey = actualScan & 0x100 ? VK_RCONTROL : VK_LCONTROL;
            if (actualKey == VK_MENU)
                actualKey = actualScan & 0x100 ? VK_RMENU : VK_LMENU;
            if (key != actualKey)
                return false;
        }
        return (actualModifiers & 15) == modifiers;
    }
};

inline bool parseShortcut(std::string_view text, ShortcutKey &result) {
    ShortcutKey parsed;
    for (auto separator = text.find('+'); separator != text.npos;
         separator = text.find('+')) {
        const auto modifier = text.substr(0, separator);
        const unsigned bit = modifier == "Control" ? keys::Control
                             : modifier == "Alt"   ? keys::Alt
                             : modifier == "Shift" ? keys::Shift
                             : modifier == "Super" ? keys::Super
                                                   : 0;
        if (!bit || (parsed.modifiers & bit))
            return false;
        parsed.modifiers |= bit;
        text.remove_prefix(separator + 1);
    }
    if (text.size() == 1 && ((text[0] >= 'a' && text[0] <= 'z') ||
                             (text[0] >= 'A' && text[0] <= 'Z') ||
                             (text[0] >= '0' && text[0] <= '9'))) {
        parsed.key = text[0];
        if (parsed.key >= 'a' && parsed.key <= 'z')
            parsed.key -= 'a' - 'A';
        else if (parsed.key >= 'A' && parsed.key <= 'Z' && !parsed.modifiers)
            parsed.modifiers |= keys::Shift;
    } else if (text.size() >= 3 && text.front() == '<' && text.back() == '>') {
        const auto digits = text.substr(1, text.size() - 2);
        const auto value = std::from_chars(
            digits.data(), digits.data() + digits.size(), parsed.scan);
        if (value.ec != std::errc{} ||
            value.ptr != digits.data() + digits.size() || !parsed.scan ||
            parsed.scan > 0x1ff)
            return false;
    } else {
        constexpr struct {
            std::string_view name;
            UINT key;
        } names[] = {{"space", VK_SPACE},        {"Return", VK_RETURN},
                     {"BackSpace", VK_BACK},     {"Tab", VK_TAB},
                     {"Escape", VK_ESCAPE},      {"Left", VK_LEFT},
                     {"Right", VK_RIGHT},        {"Up", VK_UP},
                     {"Down", VK_DOWN},          {"Delete", VK_DELETE},
                     {"Home", VK_HOME},          {"End", VK_END},
                     {"Page_Up", VK_PRIOR},      {"Page_Down", VK_NEXT},
                     {"Shift_L", VK_LSHIFT},     {"Shift_R", VK_RSHIFT},
                     {"Control_L", VK_LCONTROL}, {"Control_R", VK_RCONTROL},
                     {"Alt_L", VK_LMENU},        {"Alt_R", VK_RMENU},
                     {"Super_L", VK_LWIN},       {"Super_R", VK_RWIN}};
        for (const auto &item : names)
            if (text == item.name)
                parsed.key = item.key;
        if (!parsed.key && text.starts_with('F')) {
            unsigned number = 0;
            const auto value = std::from_chars(
                text.data() + 1, text.data() + text.size(), number);
            if (value.ec == std::errc{} &&
                value.ptr == text.data() + text.size() && number >= 1 &&
                number <= 24)
                parsed.key = VK_F1 + number - 1;
        }
        // Character names produced by the existing recorder. Resolve them in
        // the host keyboard layout; non-ASCII recordings use scan codes.
        if (!parsed.key) {
            constexpr std::string_view characters =
                "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
            constexpr std::string_view characterNames[] = {
                "exclam",       "quotedbl",    "numbersign",  "dollar",
                "percent",      "ampersand",   "apostrophe",  "parenleft",
                "parenright",   "asterisk",    "plus",        "comma",
                "minus",        "period",      "slash",       "colon",
                "semicolon",    "less",        "equal",       "greater",
                "question",     "at",          "bracketleft", "backslash",
                "bracketright", "asciicircum", "underscore",  "grave",
                "braceleft",    "bar",         "braceright",  "asciitilde"};
            for (size_t i = 0; i < std::size(characterNames); ++i) {
                if (text != characterNames[i])
                    continue;
                const auto mapped =
                    VkKeyScanExW(characters[i], GetKeyboardLayout(0));
                if (mapped == -1)
                    return false;
                parsed.key = LOBYTE(mapped);
                parsed.modifiers |= HIBYTE(mapped) & 7;
            }
        }
        if (!parsed.key)
            return false;
        parsed.modifiers |= keys::modifierForKey(parsed.key);
    }
    result = parsed;
    return true;
}

inline std::vector<std::string> shortcutLines(const std::string &value) {
    std::vector<std::string> result;
    std::istringstream stream(value);
    std::string line;
    while (std::getline(stream, line))
        if (!line.empty())
            result.push_back(std::move(line));
    return result;
}

inline UINT shortcutVirtualKey(const ShortcutKey &shortcut) {
    return shortcut.scan
               ? MapVirtualKeyExW((shortcut.scan & 0xff) |
                                      ((shortcut.scan & 0x100) ? 0xe000 : 0),
                                  MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0))
               : shortcut.key;
}

template <typename Down>
bool pressedShortcut(const std::string &list, unsigned modifiers, Down down,
                     ShortcutKey &result) {
    for (const auto &line : shortcutLines(list)) {
        ShortcutKey shortcut;
        if (!parseShortcut(line, shortcut) ||
            shortcut.modifiers != (modifiers & 15))
            continue;
        const auto key = shortcutVirtualKey(shortcut);
        if (key && down(key)) {
            result = shortcut;
            return true;
        }
    }
    return false;
}

inline bool validShortcuts(const Shortcuts &shortcuts) {
    std::vector<ShortcutKey> used;
    for (const auto &list : shortcuts) {
        if (list.size() > 512)
            return false;
        const auto lines = shortcutLines(list);
        if (lines.size() > 16)
            return false;
        for (const auto &line : lines) {
            ShortcutKey key;
            if (!parseShortcut(line, key))
                return false;
            for (const auto &previous : used)
                if ((previous.key == key.key && previous.scan == key.scan ||
                     (shortcutVirtualKey(key) &&
                      shortcutVirtualKey(previous) ==
                          shortcutVirtualKey(key))) &&
                    previous.modifiers == key.modifiers)
                    return false;
            used.push_back(key);
        }
    }
    return true;
}

inline int shortcutAction(const Shortcuts &shortcuts, UINT key, UINT scan,
                          unsigned modifiers) {
    for (size_t i = 0; i < shortcuts.size(); ++i)
        for (const auto &line : shortcutLines(shortcuts[i])) {
            ShortcutKey shortcut;
            if (parseShortcut(line, shortcut) &&
                shortcut.matches(key, scan, modifiers))
                return static_cast<int>(i);
        }
    return -1;
}

} // namespace fcitx::win32::ipc
