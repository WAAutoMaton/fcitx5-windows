#pragma once

#include "../ipc/diagnostic_log.h"
#include <atomic>
#include <cstdlib>
#include <exception>
#include <winrt/base.h>

namespace fcitx::settings::diagnostic {
using win32::diagnostic::Level;
using win32::diagnostic::settingsLogger;

// Only static operation/stage names and numeric codes belong in these events.
// Exception messages can contain user paths or configuration values.
inline void exception(std::string_view operation, bool fatal = false) noexcept {
    const auto current = std::current_exception();
    const char *kind = "no_active_exception";
    uint32_t hr = 0;
    if (current) {
        try {
            std::rethrow_exception(current);
        } catch (const winrt::hresult_error &error) {
            kind = "hresult_error";
            hr = static_cast<uint32_t>(error.code());
        } catch (const std::exception &) {
            kind = "std_exception";
        } catch (...) {
            kind = "unknown_exception";
        }
    }
    auto &logger = settingsLogger();
    logger.event(Level::Error, "settings_exception",
                 {{"operation", operation},
                  {"kind", kind},
                  {"hr", hr},
                  {"fatal", fatal}});
    // A fire_and_forget exception invokes WinRT fail-fast after this catch.
    if (fatal)
        logger.flush(std::chrono::milliseconds(250));
}

class Operation {
  public:
    explicit Operation(std::string_view name) noexcept
        : name_(name), id_(++nextId_), exceptions_(std::uncaught_exceptions()),
          started_(std::chrono::steady_clock::now()) {
        settingsLogger().event(Level::Debug, "settings_operation_begin",
                               {{"operation", name_}, {"operation_id", id_}});
    }
    ~Operation() {
        settingsLogger().event(
            Level::Debug, "settings_operation_end",
            {{"operation", name_},
             {"operation_id", id_},
             {"unwinding", std::uncaught_exceptions() > exceptions_},
             {"elapsed_ms",
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - started_)
                  .count()}});
    }
    Operation(const Operation &) = delete;
    Operation &operator=(const Operation &) = delete;

    void stage(std::string_view name) const noexcept {
        settingsLogger().event(
            Level::Debug, "settings_operation_stage",
            {{"operation", name_}, {"operation_id", id_}, {"stage", name}});
    }

  private:
    inline static std::atomic_uint64_t nextId_ = 0;
    const std::string_view name_;
    const uint64_t id_;
    const int exceptions_;
    const std::chrono::steady_clock::time_point started_;
};

inline int exit(std::string_view reason, int code) noexcept {
    auto &logger = settingsLogger();
    logger.event(code ? Level::Error : Level::Info, "settings_exit",
                 {{"reason", reason}, {"exit_code", code}});
    logger.flush(std::chrono::milliseconds(250));
    logger.setEnabled(false);
    return code;
}

class TerminateHandler {
  public:
    TerminateHandler() noexcept : previous_(std::set_terminate(terminate)) {
        previousHandler_ = previous_;
    }
    ~TerminateHandler() { std::set_terminate(previous_); }
    TerminateHandler(const TerminateHandler &) = delete;
    TerminateHandler &operator=(const TerminateHandler &) = delete;

  private:
    [[noreturn]] static void terminate() noexcept {
        exception("std_terminate", true);
        if (previousHandler_)
            previousHandler_();
        std::abort();
    }
    inline static std::terminate_handler previousHandler_ = nullptr;
    const std::terminate_handler previous_;
};
} // namespace fcitx::settings::diagnostic
