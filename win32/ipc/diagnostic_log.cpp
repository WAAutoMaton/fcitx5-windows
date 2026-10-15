#include "diagnostic_log.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <shlobj.h>
#include <stdexcept>
#include <vector>
#include <windows.h>

#ifndef FCITX5_DIAGNOSTIC_BUILD
#define FCITX5_DIAGNOSTIC_BUILD __DATE__ " " __TIME__
#endif

namespace fcitx::win32::diagnostic {
namespace {
constexpr size_t kMaxLine = 4096;
constexpr auto kDrainTimeout = std::chrono::milliseconds(250);

std::filesystem::path logDirectory() {
#ifdef FCITX5_UI_TEST
    wchar_t testDirectory[32768]{};
    const auto length = GetEnvironmentVariableW(
        L"FCITX5_DIAGNOSTIC_TEST_DIRECTORY", testDirectory,
        static_cast<DWORD>(std::size(testDirectory)));
    if (length && length < std::size(testDirectory)) {
        const std::filesystem::path path(testDirectory);
        if (path.is_absolute())
            return path;
        throw std::runtime_error("Diagnostic test directory must be absolute");
    }
#endif
    PWSTR value = nullptr;
    const auto result = SHGetKnownFolderPath(
        FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &value);
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owner(value,
                                                             CoTaskMemFree);
    if (FAILED(result) || !value)
        throw std::runtime_error("Cannot locate diagnostic directory");
    return std::filesystem::path(value) / L"Fcitx5" / L"logs";
}

bool digits(std::string_view value) noexcept {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(),
                       [](char c) { return c >= '0' && c <= '9'; });
}

void quoted(std::string &out, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    out += '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 0x20 || c >= 0x7f) {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else {
            out += static_cast<char>(c);
        }
    }
    out += '"';
}

std::string timeString(FILETIME time, bool dateOnly = false) {
    SYSTEMTIME system{};
    FileTimeToSystemTime(&time, &system);
    char buffer[64]{};
    if (dateOnly)
        snprintf(buffer, sizeof(buffer), "%04u%02u%02u", system.wYear,
                 system.wMonth, system.wDay);
    else
        snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                 system.wYear, system.wMonth, system.wDay, system.wHour,
                 system.wMinute, system.wSecond, system.wMilliseconds);
    return buffer;
}

const char *levelName(Level level) {
    switch (level) {
    case Level::Debug:
        return "debug";
    case Level::Info:
        return "info";
    case Level::Warning:
        return "warning";
    case Level::Error:
        return "error";
    }
    return "info";
}
} // namespace

bool ownedLogName(std::string_view name) noexcept {
    if (name.starts_with("core-")) {
        name.remove_prefix(5);
    } else {
        if (name.starts_with("tsf-"))
            name.remove_prefix(4);
        else if (name.starts_with("settings-"))
            name.remove_prefix(9);
        else
            return false;
        const auto separator = name.find('-');
        if (separator == name.npos || !digits(name.substr(0, separator)))
            return false;
        name.remove_prefix(separator + 1);
    }
    if (name.size() < 12 || !digits(name.substr(0, 8)) ||
        name.substr(8, 4) != ".log")
        return false;
    name.remove_prefix(12);
    return name.empty() || (name.front() == '.' && digits(name.substr(1)));
}

struct Logger::Impl : std::enable_shared_from_this<Impl> {
    struct Record {
        FILETIME time{};
        DWORD tid = 0;
        Level level = Level::Info;
        std::string fields;
    };
    struct Task {
        std::shared_ptr<Impl> state;
        HMODULE module;
    };
    Impl(std::string roleValue, std::string buildValue, Options optionsValue)
        : role(std::move(roleValue)), build(std::move(buildValue)),
          options(std::move(optionsValue)), pid(GetCurrentProcessId()) {
        if (role != "core" && role != "tsf" && role != "settings")
            throw std::invalid_argument("Invalid diagnostic role");
        prefix =
            role + (role == "core" ? "-" : "-" + std::to_string(pid) + "-");
        FILETIME created{}, exited{}, kernel{}, user{};
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
        session = std::to_string(
            (static_cast<uint64_t>(created.dwHighDateTime) << 32) |
            created.dwLowDateTime);
    }

    static void CALLBACK callback(PTP_CALLBACK_INSTANCE instance,
                                  void *context) noexcept {
        std::unique_ptr<Task> task(static_cast<Task *>(context));
        // Pin the DLL until the callback has returned, including CRT teardown.
        FreeLibraryWhenCallbackReturns(instance, task->module);
        auto state = task->state;
        state->run();
    }

    void scheduleLocked() noexcept {
        if (running)
            return;
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                reinterpret_cast<LPCWSTR>(&callback),
                                &module)) {
            fail(GetLastError());
            return;
        }
        try {
            auto task =
                std::make_unique<Task>(Task{shared_from_this(), module});
            running = true;
            if (TrySubmitThreadpoolCallback(callback, task.get(), nullptr)) {
                task.release();
                return;
            }
            fail(GetLastError());
        } catch (...) {
            fail(ERROR_NOT_ENOUGH_MEMORY);
        }
        running = false;
        FreeLibrary(module);
    }

    void fail(DWORD value) noexcept {
        error = value ? value : ERROR_WRITE_FAULT;
        lastFailure = error.load();
        state = State::Unavailable;
        const auto now = GetTickCount64();
        retryAfter = now + 60000;
        if (now >= nextWarning.load()) {
            nextWarning = now + 60000;
            reportFailure = true;
            OutputDebugStringW(L"Fcitx5: diagnostic logging unavailable\n");
        }
    }

    std::filesystem::path directory() const {
        return options.directory.empty() ? logDirectory() : options.directory;
    }

    FILETIME now() const {
        FILETIME result{};
        GetSystemTimeAsFileTime(&result);
        if (options.clock) {
            using Ticks =
                std::chrono::duration<int64_t, std::ratio<1, 10000000>>;
            const auto ticks =
                static_cast<uint64_t>(std::chrono::duration_cast<Ticks>(
                                          options.clock().time_since_epoch())
                                          .count() +
                                      116444736000000000LL);
            result.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
            result.dwLowDateTime = static_cast<DWORD>(ticks);
        }
        return result;
    }

    Record notice(std::string_view event) const {
        Record record;
        record.time = now();
        record.tid = GetCurrentThreadId();
        record.fields = ",\"event\":";
        quoted(record.fields, event);
        return record;
    }

    bool prepare(bool create) {
        root = directory();
        std::error_code ec;
        if (create)
            directoryCreated = std::filesystem::create_directories(root, ec) ||
                               directoryCreated;
        if (ec) {
            fail(static_cast<DWORD>(ec.value()));
            return false;
        }
        if (!std::filesystem::exists(root, ec))
            return false;
        if (ec || !std::filesystem::is_directory(root, ec)) {
            fail(ec ? static_cast<DWORD>(ec.value()) : ERROR_DIRECTORY);
            return false;
        }
        return true;
    }

    void cleanup() {
        struct File {
            std::filesystem::path path;
            std::filesystem::file_time_type modified;
            uintmax_t size;
            bool own;
        };
        std::vector<File> files;
        uintmax_t total = 0, ownTotal = 0;
        uint64_t count = 0;
        std::error_code ec;
        const auto now = std::filesystem::file_time_type::clock::now();
        for (std::filesystem::directory_iterator it(root, ec), end;
             !ec && it != end; it.increment(ec)) {
            const auto &entry = *it;
            const auto type = entry.symlink_status(ec);
            if (ec || type.type() != std::filesystem::file_type::regular)
                continue;
            const auto utf8Name = entry.path().filename().u8string();
            const std::string name(utf8Name.begin(), utf8Name.end());
            if (!ownedLogName(name))
                continue;
            const auto modified = entry.last_write_time(ec);
            if (ec)
                continue;
            const auto size = entry.file_size(ec);
            if (ec)
                continue;
            if (entry.path() != currentPath &&
                now - modified > options.retention &&
                std::filesystem::remove(entry.path(), ec)) {
                ++deletedFiles;
                continue;
            }
            // Sharing violations are expected for other processes' active
            // files.
            ec.clear();
            const bool own = name.starts_with(prefix);
            files.push_back({entry.path(), modified, size, own});
            total += size;
            if (own)
                ownTotal += size;
            ++count;
        }
        if (ec)
            fail(static_cast<DWORD>(ec.value()));
        std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) {
            return a.modified < b.modified;
        });
        for (const auto &entry : files) {
            if (entry.path == currentPath ||
                (total <= options.directoryBytes &&
                 (!entry.own || ownTotal <= options.processBytes)))
                continue;
            if (std::filesystem::remove(entry.path, ec)) {
                ++deletedFiles;
                total -= entry.size;
                if (entry.own)
                    ownTotal -= entry.size;
                --count;
            } else if (ec.value() != ERROR_SHARING_VIOLATION &&
                       ec.value() != ERROR_ACCESS_DENIED) {
                fail(static_cast<DWORD>(ec.value()));
            }
            ec.clear();
        }
        fileCount = count;
        totalBytes = total;
    }

    void close() noexcept {
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
        }
        currentPath.clear();
    }

    bool open(const std::string &day, size_t lineBytes) {
        const auto base = root / (prefix + day + ".log");
        for (uint32_t index = 0; index < 100000; ++index) {
            auto path = base;
            if (index)
                path += "." + std::to_string(index);
            const auto handle = CreateFileW(
                path.c_str(), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                const auto value = GetLastError();
                if (value == ERROR_SHARING_VIOLATION)
                    continue;
                fail(value);
                return false;
            }
            LARGE_INTEGER size{};
            if (!GetFileSizeEx(handle, &size)) {
                const auto value = GetLastError();
                CloseHandle(handle);
                fail(value);
                return false;
            }
            if (size.QuadPart &&
                static_cast<uint64_t>(size.QuadPart) + lineBytes >
                    options.rotateBytes) {
                CloseHandle(handle);
                if (index == 0) {
                    std::error_code rotateError;
                    for (unsigned rotated = 8; rotated > 0; --rotated) {
                        const auto from =
                            path.wstring() + L"." + std::to_wstring(rotated);
                        const auto to = path.wstring() + L"." +
                                        std::to_wstring(rotated + 1);
                        std::filesystem::remove(to, rotateError);
                        std::filesystem::rename(from, to, rotateError);
                    }
                    std::filesystem::rename(path, path.wstring() + L".1",
                                            rotateError);
                    fileRotated = true;
                    index = UINT32_MAX;
                    continue;
                }
                continue;
            }
            file = handle;
            fileRotated =
                fileRotated || (!lastPath.empty() && lastPath != path);
            fileCreated = fileCreated || size.QuadPart == 0;
            lastPath = path;
            currentPath = std::move(path);
            currentSize = static_cast<uint64_t>(size.QuadPart);
            currentDay = day;
            return true;
        }
        fail(ERROR_TOO_MANY_OPEN_FILES);
        return false;
    }

    void write(const Record &record) {
        std::string line = "{\"time\":";
        quoted(line, timeString(record.time));
        line += ",\"level\":";
        quoted(line, levelName(record.level));
        line += ",\"role\":";
        quoted(line, role);
        line += ",\"pid\":" + std::to_string(pid) +
                ",\"tid\":" + std::to_string(record.tid) + ",\"session\":";
        quoted(line, session);
        line += ",\"build\":";
        quoted(line, build);
        line += record.fields + "}\n";
        const auto day = timeString(record.time, true);
        if (file != INVALID_HANDLE_VALUE &&
            (day != currentDay ||
             currentSize + line.size() > options.rotateBytes))
            close();
        if (file == INVALID_HANDLE_VALUE && !open(day, line.size()))
            return;
        DWORD written = 0;
        if (!WriteFile(file, line.data(), static_cast<DWORD>(line.size()),
                       &written, nullptr) ||
            written != line.size()) {
            fail(GetLastError());
            close();
            return;
        }
        currentSize += written;
        if (state != State::Unavailable)
            state = enabled ? State::Recording : State::Off;
    }

    void run() noexcept {
        try {
            bool create = false;
            bool maintenance = false;
            {
                std::lock_guard lock(mutex);
                create = createRequested || enabled;
                maintenance = maintenanceRequested;
                createRequested = false;
                maintenanceRequested = false;
            }
            if (prepare(create)) {
                error = 0;
                if (enabled && state == State::Unavailable)
                    state = State::Starting;
                cleanup();
                auto drainDeadline =
                    std::chrono::steady_clock::time_point::max();
                for (;;) {
                    Record record;
                    {
                        std::lock_guard lock(mutex);
                        if (queue.empty())
                            break;
                        if (!enabled &&
                            drainDeadline ==
                                std::chrono::steady_clock::time_point::max())
                            drainDeadline = std::chrono::steady_clock::now() +
                                            kDrainTimeout;
                        if (!enabled &&
                            std::chrono::steady_clock::now() >= drainDeadline) {
                            const auto removed =
                                std::erase_if(queue, [](const auto &item) {
                                    return item.level == Level::Debug ||
                                           item.level == Level::Info;
                                });
                            dropped += removed;
                            if (queue.empty())
                                break;
                        }
                        record = std::move(queue.front());
                        queue.pop_front();
                        queued = queue.size();
                    }
                    write(record);
                }
                const auto lost = dropped.load();
                if (enabled && lost != reportedDropped) {
                    auto record = notice("log_queue_dropped");
                    record.level = Level::Warning;
                    record.fields += ",\"count\":" + std::to_string(lost);
                    write(record);
                    reportedDropped = lost;
                }
                if (enabled) {
                    if (directoryCreated)
                        write(notice("log_directory_created"));
                    if (fileRotated)
                        write(notice("log_rotation"));
                    else if (fileCreated)
                        write(notice("log_file_open"));
                    if (maintenance || directoryCreated || fileRotated ||
                        deletedFiles != reportedDeleted) {
                        auto record = notice("log_cleanup");
                        record.fields += ",\"file_count\":" +
                                         std::to_string(fileCount.load()) +
                                         ",\"total_bytes\":" +
                                         std::to_string(totalBytes.load()) +
                                         ",\"deleted_files\":" +
                                         std::to_string(deletedFiles);
                        write(record);
                        reportedDeleted = deletedFiles;
                    }
                    if (reportFailure.exchange(false)) {
                        auto record = notice("log_failure");
                        record.level = Level::Warning;
                        record.fields +=
                            ",\"error\":" + std::to_string(lastFailure.load());
                        write(record);
                    }
                    directoryCreated = fileRotated = fileCreated = false;
                }
                close();
                cleanup();
            } else {
                std::lock_guard lock(mutex);
                dropped += queue.size();
                queue.clear();
            }
        } catch (...) {
            fail(ERROR_WRITE_FAULT);
            close();
            std::lock_guard lock(mutex);
            dropped += queue.size();
            queue.clear();
        }
        std::lock_guard lock(mutex);
        queued = queue.size();
        running = false;
        if (!enabled)
            state = State::Off;
        if (!queue.empty() || maintenanceRequested)
            scheduleLocked();
        done.notify_all();
    }

    std::string role, build, prefix, session;
    const Options options;
    const DWORD pid;
    std::mutex mutex;
    std::condition_variable done;
    std::deque<Record> queue;
    bool running = false, maintenanceRequested = false, createRequested = false;
    std::atomic_bool enabled = false;
    std::atomic<State> state = State::Off;
    std::atomic_size_t queued = 0;
    std::atomic_uint64_t dropped = 0, fileCount = 0, totalBytes = 0,
                         nextWarning = 0, retryAfter = 0;
    std::atomic<DWORD> error = 0;
    std::atomic<DWORD> lastFailure = 0;
    std::atomic_bool reportFailure = false;
    uint64_t reportedDropped = 0;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::filesystem::path root, currentPath, lastPath;
    bool directoryCreated = false, fileRotated = false, fileCreated = false;
    uint64_t deletedFiles = 0, reportedDeleted = 0;
    std::string currentDay;
    uint64_t currentSize = 0;
};

Logger::Logger(std::string role, std::string build, Options options) noexcept {
    try {
        impl_ = std::make_shared<Impl>(std::move(role),
                                       build.empty() ? FCITX5_DIAGNOSTIC_BUILD
                                                     : std::move(build),
                                       std::move(options));
    } catch (...) {
    }
}
Logger::~Logger() { setEnabled(false); }

void Logger::setEnabled(bool value) noexcept {
    if (!impl_)
        return;
    const auto previous = impl_->enabled.exchange(value);
    if (previous == value)
        return;
    impl_->state = value ? State::Starting : State::Off;
    if (value) {
        impl_->error = 0;
        event(Level::Info, "logging_enabled");
    }
}

bool Logger::enabled() const noexcept { return impl_ && impl_->enabled; }

void Logger::event(Level level, std::string_view name,
                   std::initializer_list<Field> fields) noexcept {
    if (!impl_ || !impl_->enabled)
        return;
    if (impl_->state == State::Unavailable &&
        GetTickCount64() < impl_->retryAfter) {
        ++impl_->dropped;
        return;
    }
    try {
        Impl::Record record;
        record.time = impl_->now();
        record.tid = GetCurrentThreadId();
        record.level = level;
        record.fields = ",\"event\":";
        quoted(record.fields, name.substr(0, 96));
        for (const auto &[key, value] : fields) {
            const auto valueBytes = std::visit(
                [](const auto &item) -> size_t {
                    if constexpr (std::same_as<std::decay_t<decltype(item)>,
                                               std::string_view>)
                        return std::min<size_t>(item.size(), 256) * 6 + 2;
                    else
                        return 32;
                },
                value);
            if (record.fields.size() + key.size() * 6 + valueBytes + 8 >
                kMaxLine)
                break;
            record.fields += ',';
            quoted(record.fields, key);
            record.fields += ':';
            std::visit(
                [&](const auto &item) {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::same_as<T, std::string_view>) {
                        quoted(record.fields, item.substr(0, 256));
                    } else {
                        char number[32]{};
                        const auto result = std::to_chars(
                            std::begin(number), std::end(number), item);
                        record.fields.append(number, result.ptr);
                    }
                },
                value);
        }
        std::unique_lock lock(impl_->mutex, std::try_to_lock);
        if (!lock || impl_->queue.size() >= impl_->options.queueLimit) {
            ++impl_->dropped;
            return;
        }
        if (!impl_->enabled)
            return;
        impl_->queue.push_back(std::move(record));
        impl_->queued = impl_->queue.size();
        impl_->scheduleLocked();
    } catch (...) {
        ++impl_->dropped;
    }
}

Status Logger::status() const noexcept {
    if (!impl_) {
        Status result;
        result.state = State::Unavailable;
        result.error = ERROR_NOT_ENOUGH_MEMORY;
        return result;
    }
    return {impl_->state.load(),      impl_->queued.load(),
            impl_->dropped.load(),    impl_->fileCount.load(),
            impl_->totalBytes.load(), impl_->error.load()};
}

void Logger::maintain(bool createDirectory) noexcept {
    if (!impl_)
        return;
    try {
        std::unique_lock lock(impl_->mutex, std::try_to_lock);
        if (!lock)
            return;
        impl_->maintenanceRequested = true;
        impl_->createRequested = impl_->createRequested || createDirectory;
        impl_->scheduleLocked();
    } catch (...) {
        impl_->fail(ERROR_NOT_ENOUGH_MEMORY);
    }
}

std::filesystem::path Logger::directory() const {
    return impl_ ? impl_->directory() : std::filesystem::path{};
}

bool Logger::flush(std::chrono::milliseconds timeout) noexcept {
    if (!impl_)
        return true;
    try {
        std::unique_lock lock(impl_->mutex);
        return impl_->done.wait_for(lock, timeout, [&] {
            return !impl_->running && impl_->queue.empty();
        });
    } catch (...) {
        return false;
    }
}

Logger &coreLogger() {
    static Logger logger("core");
    return logger;
}
Logger &settingsLogger() {
    static Logger logger("settings");
    return logger;
}
Logger &tsfLogger() {
    static Logger logger("tsf");
    return logger;
}
} // namespace fcitx::win32::diagnostic
