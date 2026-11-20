#include "windowslogging.h"
#include "../win32/ipc/diagnostic_log.h"
#include <algorithm>
#include <fcitx-utils/log.h>
#include <iostream>
#include <string>

namespace fcitx::win32 {
namespace {
thread_local std::string engineMessage;

diagnostic::Level diagnosticLevel(char prefix) {
    switch (prefix) {
    case 'D':
        return diagnostic::Level::Debug;
    case 'I':
        return diagnostic::Level::Info;
    case 'W':
        return diagnostic::Level::Warning;
    default:
        return diagnostic::Level::Error;
    }
}
} // namespace

void applyDiagnosticLogging(bool enabled) {
    auto &logger = diagnostic::coreLogger();
    if (logger.enabled() == enabled)
        return;
    logger.setEnabled(enabled);
    try {
        Log::setLogRule(enabled ? "*=5,key_trace=0" : "*=4,key_trace=0");
    } catch (...) {
        logger.event(diagnostic::Level::Warning, "core_log_rule_error");
    }
}

CoreLogStream::CoreLogStream() {
    Log::setLogStream(stream_);
    try {
        Log::setLogRule("*=4,key_trace=0,notimedate");
    } catch (...) {
        diagnostic::coreLogger().event(diagnostic::Level::Warning,
                                       "core_log_rule_error");
    }
    diagnostic::engineLogger().maintain();
}

CoreLogStream::~CoreLogStream() {
    Log::setLogStream(std::cerr);
    diagnostic::engineLogger().flush(std::chrono::milliseconds(250));
}

std::streamsize CoreLogStream::xsputn(const char *data, std::streamsize count) {
    if (count <= 0)
        return 0;
    try {
        // Keep one extra byte so the writer can mark a truncated message.
        const auto remaining =
            diagnostic::maxEngineMessageBytes + 1 - engineMessage.size();
        engineMessage.append(data,
                             std::min(static_cast<size_t>(count), remaining));
    } catch (...) {
    }
    if (!diagnostic::coreLogger().enabled())
        std::cerr.rdbuf()->sputn(data, count);
    return count;
}

CoreLogStream::int_type CoreLogStream::overflow(int_type ch) {
    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::not_eof(ch);
    const char value = traits_type::to_char_type(ch);
    return xsputn(&value, 1) == 1 ? ch : traits_type::eof();
}

int CoreLogStream::sync() {
    if (!engineMessage.empty()) {
        const auto level = diagnosticLevel(engineMessage.front());
        auto &logger = diagnostic::engineLogger();
        logger.engineMessage(level, engineMessage);
        diagnostic::coreLogger().event(
            level, "fcitx_log", {{"message_bytes", engineMessage.size()}});
        const bool fatal = engineMessage.front() == 'F';
        engineMessage.clear();
        if (fatal)
            logger.flush(std::chrono::milliseconds(250));
    }
    return diagnostic::coreLogger().enabled() ? 0
                                              : std::cerr.rdbuf()->pubsync();
}
} // namespace fcitx::win32
