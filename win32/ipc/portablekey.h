#pragma once

#include <string>
#include <windows.h>

namespace fcitx::win32::keys {
enum Modifier : unsigned {
    Shift = 1,
    Control = 2,
    Alt = 4,
    Super = 8,
    Caps = 16
};

inline unsigned modifierForKey(UINT key) {
    switch (key) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        return Shift;
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        return Control;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        return Alt;
    case VK_LWIN:
    case VK_RWIN:
        return Super;
    default:
        return 0;
    }
}

// Match WindowsPipeServer's key symbols and Key::normalize without linking
// Settings to the Core's GNU runtime. Unmapped keys use its existing scan code.
inline std::string portableKey(UINT key, UINT scan, unsigned modifiers,
                               const std::string &unicode = {}) {
    std::string name;
    bool character = false;
    if (!unicode.empty()) {
        name = unicode;
        character = true;
    } else if (key >= 'A' && key <= 'Z') {
        const bool upper = bool(modifiers & Shift) != bool(modifiers & Caps);
        name = char((upper ? 'A' : 'a') + key - 'A');
        character = true;
    } else if (key >= '0' && key <= '9') {
        name = char(key);
        character = true;
    } else {
        switch (key) {
        case VK_SPACE:
            name = "space";
            break;
        case VK_RETURN:
            name = "Return";
            break;
        case VK_BACK:
            name = "BackSpace";
            break;
        case VK_TAB:
            name = "Tab";
            break;
        case VK_ESCAPE:
            name = "Escape";
            break;
        case VK_LEFT:
            name = "Left";
            break;
        case VK_RIGHT:
            name = "Right";
            break;
        case VK_UP:
            name = "Up";
            break;
        case VK_DOWN:
            name = "Down";
            break;
        case VK_DELETE:
            name = "Delete";
            break;
        case VK_HOME:
            name = "Home";
            break;
        case VK_END:
            name = "End";
            break;
        case VK_PRIOR:
            name = "Page_Up";
            break;
        case VK_NEXT:
            name = "Page_Down";
            break;
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            name = key == VK_RSHIFT || scan == 0x36 ? "Shift_R" : "Shift_L";
            break;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            name = key == VK_RCONTROL || (scan & 0x100) ? "Control_R"
                                                        : "Control_L";
            break;
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            name = key == VK_RMENU || (scan & 0x100) ? "Alt_R" : "Alt_L";
            break;
        case VK_LWIN:
            name = "Super_L";
            break;
        case VK_RWIN:
            name = "Super_R";
            break;
        default:
            if (!scan)
                return {};
            name = "<" + std::to_string(scan) + ">";
            break;
        }
    }
    modifiers &= Shift | Control | Alt | Super;
    if (character) {
        const bool letter =
            name.size() == 1 && ((name[0] >= 'a' && name[0] <= 'z') ||
                                 (name[0] >= 'A' && name[0] <= 'Z'));
        if (letter) {
            if (modifiers && modifiers != Shift && name[0] >= 'a' &&
                name[0] <= 'z')
                name[0] -= 'a' - 'A';
            if (modifiers == Shift)
                modifiers = 0;
        } else if (name != " " && !unicode.empty()) {
            modifiers &= ~Shift;
        }
        if (name.size() == 1 && static_cast<unsigned char>(name[0]) < 0x7f) {
            constexpr struct {
                char character;
                const char *name;
            } names[] = {{' ', "space"},       {'!', "exclam"},
                         {'\"', "quotedbl"},   {'#', "numbersign"},
                         {'$', "dollar"},      {'%', "percent"},
                         {'&', "ampersand"},   {'\'', "apostrophe"},
                         {'(', "parenleft"},   {')', "parenright"},
                         {'*', "asterisk"},    {'+', "plus"},
                         {',', "comma"},       {'-', "minus"},
                         {'.', "period"},      {'/', "slash"},
                         {':', "colon"},       {';', "semicolon"},
                         {'<', "less"},        {'=', "equal"},
                         {'>', "greater"},     {'?', "question"},
                         {'@', "at"},          {'[', "bracketleft"},
                         {'\\', "backslash"},  {']', "bracketright"},
                         {'^', "asciicircum"}, {'_', "underscore"},
                         {'`', "grave"},       {'{', "braceleft"},
                         {'|', "bar"},         {'}', "braceright"},
                         {'~', "asciitilde"}};
            for (const auto &item : names) {
                if (name[0] == item.character) {
                    name = item.name;
                    break;
                }
            }
        } else {
            if (!scan)
                return {};
            name = "<" + std::to_string(scan) + ">";
        }
    }
    // A modifier key carries its own modifier implicitly.
    modifiers &= ~modifierForKey(key);
    std::string result;
    if (modifiers & Control)
        result += "Control+";
    if (modifiers & Alt)
        result += "Alt+";
    if (modifiers & Shift)
        result += "Shift+";
    if (modifiers & Super)
        result += "Super+";
    return result + name;
}

} // namespace fcitx::win32::keys
