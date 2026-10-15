#pragma once

#include "../ipc/diagnostic_log.h"
#include <cwchar>
#include <windows.h>

namespace fcitx {
inline void diagnosticResult(const wchar_t *stage, HRESULT result) {
    auto &logger = win32::diagnostic::tsfLogger();
    if (logger.enabled()) {
        char name[96]{};
        size_t i = 0;
        for (; stage[i] && i + 1 < sizeof(name); ++i)
            name[i] = stage[i] < 0x80 ? static_cast<char>(stage[i]) : '_';
        logger.event(FAILED(result) ? win32::diagnostic::Level::Warning
                                    : win32::diagnostic::Level::Debug,
                     "tsf_operation",
                     {{"stage", name}, {"hr", static_cast<uint32_t>(result)}});
    }
}
} // namespace fcitx
