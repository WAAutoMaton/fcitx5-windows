#include <xkbcommon/xkbcommon.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcitx-utils/key.h>
#include <fcitx-utils/utf8.h>
#include <string>

extern "C" {

int xkb_keysym_get_name(xkb_keysym_t keysym, char *buffer, size_t size) {
    std::string name;
    if (keysym == XKB_KEY_NoSymbol) {
        name = "NoSymbol";
    } else {
        name = fcitx::Key::keySymToString(static_cast<fcitx::KeySym>(keysym),
                                          fcitx::KeyStringFormat::Portable);
        if (name.empty() && keysym >= 0x01000100u && keysym <= 0x0110ffffu) {
            char unicodeName[16];
            std::snprintf(unicodeName, sizeof(unicodeName), "U%04X",
                          keysym - 0x01000000u);
            name = unicodeName;
        }
    }
    if (name.empty()) {
        if (buffer && size > 0) {
            buffer[0] = '\0';
        }
        return -1;
    }
    if (buffer && size > 0) {
        const size_t copyLen =
            name.size() < size - 1 ? name.size() : size - 1;
        std::memcpy(buffer, name.data(), copyLen);
        buffer[copyLen] = '\0';
    }
    return static_cast<int>(name.size());
}

xkb_keysym_t xkb_keysym_from_name(const char *name,
                                  enum xkb_keysym_flags /*flags*/) {
    if (!name || !name[0]) {
        return XKB_KEY_NoSymbol;
    }
    if (std::strcmp(name, "NoSymbol") == 0) {
        return XKB_KEY_NoSymbol;
    }
    const auto sym = fcitx::Key::keySymFromString(name);
    if (sym != FcitxKey_None) {
        return static_cast<xkb_keysym_t>(sym);
    }
    if ((name[0] == 'U' || name[0] == 'u') && name[1] != '\0') {
        char *end = nullptr;
        const unsigned long cp = std::strtoul(name + 1, &end, 16);
        if (end && *end == '\0' && cp <= 0x10fffful) {
            return static_cast<xkb_keysym_t>(
                fcitx::Key::keySymFromUnicode(static_cast<uint32_t>(cp)));
        }
    }
    if (name[0] == '0' && (name[1] == 'x' || name[1] == 'X') &&
        name[2] != '\0') {
        char *end = nullptr;
        const unsigned long val = std::strtoul(name + 2, &end, 16);
        if (end && *end == '\0' && val <= 0x1fffffffu) {
            return static_cast<xkb_keysym_t>(val);
        }
    }
    return XKB_KEY_NoSymbol;
}

int xkb_keysym_to_utf8(xkb_keysym_t keysym, char *buffer, size_t size) {
    const uint32_t unicode =
        fcitx::Key::keySymToUnicode(static_cast<fcitx::KeySym>(keysym));
    if (unicode == 0) {
        if (buffer && size > 0) {
            buffer[0] = '\0';
        }
        return 0;
    }
    const std::string utf8 = fcitx::utf8::UCS4ToUTF8(unicode);
    if (utf8.empty()) {
        if (buffer && size > 0) {
            buffer[0] = '\0';
        }
        return 0;
    }
    if (size < utf8.size() + 1) {
        return -1;
    }
    std::memcpy(buffer, utf8.data(), utf8.size());
    buffer[utf8.size()] = '\0';
    return static_cast<int>(utf8.size() + 1);
}

} // extern "C"
