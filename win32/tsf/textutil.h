#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <windows.h>

namespace fcitx {

inline bool utf8ToWide(std::string_view text, std::wstring &result) {
    if (text.empty()) {
        result.clear();
        return true;
    }
    const auto length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return false;
    }
    std::wstring converted(length, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), converted.data(),
                            length) != length) {
        return false;
    }
    result = std::move(converted);
    return true;
}

inline bool preeditToWide(const std::string &text, uint32_t cursor,
                          std::wstring &result, ULONG &wideCursor) {
    if (cursor > text.size() || !utf8ToWide(text, result)) {
        return false;
    }
    std::wstring prefix;
    if (!utf8ToWide(std::string_view(text).substr(0, cursor), prefix)) {
        return false;
    }
    wideCursor = static_cast<ULONG>(prefix.size());
    return true;
}

} // namespace fcitx
