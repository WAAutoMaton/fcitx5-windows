#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

namespace fcitx::win32::diagnostic {

enum class Level : uint8_t { Debug, Info, Warning, Error };
enum class State : uint8_t { Off, Starting, Recording, Unavailable };

struct Options {
    std::filesystem::path directory;
    size_t queueLimit = 4096;
    uint64_t rotateBytes = 8U * 1024U * 1024U;
    uint64_t processBytes = 64U * 1024U * 1024U;
    uint64_t directoryBytes = 256U * 1024U * 1024U;
    std::chrono::hours retention{24 * 30};
    std::function<std::chrono::system_clock::time_point()> clock;
};

struct Status {
    State state = State::Off;
    size_t queued = 0;
    uint64_t dropped = 0;
    uint64_t fileCount = 0;
    uint64_t totalBytes = 0;
    uint32_t error = 0;
};

struct Field {
    std::string_view key;
    std::variant<std::string_view, int64_t, uint64_t> value;
    Field(std::string_view name, std::string_view text)
        : key(name), value(text) {}
    template <std::integral T>
    Field(std::string_view name, T number) : key(name) {
        if constexpr (std::signed_integral<T>)
            value = static_cast<int64_t>(number);
        else
            value = static_cast<uint64_t>(number);
    }
};

class Logger {
  public:
    explicit Logger(std::string role, std::string build = {},
                    Options options = {}) noexcept;
    ~Logger();
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    // These calls never wait for a worker or perform filesystem operations.
    void setEnabled(bool enabled) noexcept;
    bool enabled() const noexcept;
    void event(Level level, std::string_view name,
               std::initializer_list<Field> fields = {}) noexcept;
    Status status() const noexcept;
    void maintain(bool createDirectory = false) noexcept;

    // Only use from background tasks or explicit process shutdown/test code.
    std::filesystem::path directory() const;
    bool flush(std::chrono::milliseconds timeout) noexcept;

  private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

bool ownedLogName(std::string_view name) noexcept;
Logger &coreLogger();
Logger &settingsLogger();
Logger &tsfLogger();
} // namespace fcitx::win32::diagnostic
