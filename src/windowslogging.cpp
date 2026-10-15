#include "windowslogging.h"
#include "../win32/ipc/diagnostic_log.h"
#include <fcitx-utils/log.h>
#include <iostream>

namespace fcitx::win32 {
void applyDiagnosticLogging(bool enabled) {
    // No unaudited addon Debug category is enabled. key_trace stays disabled
    // even if the process inherited a broader logging rule.
    try {
        Log::setLogRule("*=3,key_trace=0,notimedate");
    } catch (...) {
        diagnostic::coreLogger().event(diagnostic::Level::Warning,
                                        "core_log_rule_error");
    }
    diagnostic::coreLogger().setEnabled(enabled);
}

CoreLogStream::CoreLogStream() { Log::setLogStream(stream_); }
CoreLogStream::~CoreLogStream() { Log::setLogStream(std::cerr); }

std::streamsize CoreLogStream::xsputn(const char *data, std::streamsize count) {
    auto &logger = diagnostic::coreLogger();
    if (!logger.enabled())
        return std::cerr.rdbuf()->sputn(data, count);
    if (count > 0) {
        const auto level = data[0] == 'W' ? diagnostic::Level::Warning
                                          : diagnostic::Level::Error;
        logger.event(level, "fcitx_log", {{"message_bytes", count}});
    }
    return count;
}

CoreLogStream::int_type CoreLogStream::overflow(int_type ch) {
    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::not_eof(ch);
    const char value = traits_type::to_char_type(ch);
    return xsputn(&value, 1) == 1 ? ch : traits_type::eof();
}

int CoreLogStream::sync() {
    return diagnostic::coreLogger().enabled() ? 0
                                              : std::cerr.rdbuf()->pubsync();
}
} // namespace fcitx::win32
