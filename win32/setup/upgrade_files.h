#pragma once

#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fcitx::setup::upgrade {

namespace fs = std::filesystem;

class Handle {
  public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
    ~Handle();
    Handle(Handle &&other) noexcept;
    Handle &operator=(Handle &&other) noexcept;
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    HANDLE get() const { return value_; }
    explicit operator bool() const {
        return value_ && value_ != INVALID_HANDLE_VALUE;
    }

  private:
    HANDLE value_;
};

[[noreturn]] void fail(DWORD error = GetLastError());
std::string utf8(std::wstring_view value);
std::wstring wide(std::string_view value);
fs::path relativePath(std::string_view value);
bool runtimePath(const fs::path &path);
void validatePath(const fs::path &path, bool allowMissing = false);
void validateSecurity(const fs::path &path);
void protectDirectory(const fs::path &path);
std::string sha256(const fs::path &path);
std::string sha256(HANDLE file);
void copyContents(HANDLE source, HANDLE destination);
std::string readText(const fs::path &path, size_t limit = 16 * 1024 * 1024);
void writeText(const fs::path &path, std::string_view value);

// Retains write access and denies new opens until every file has been updated.
// Opening a loaded PE image for write must fail; test_upgrade_files probes it.
class FileGuard {
  public:
    explicit FileGuard(const fs::path &path, bool create = false,
                       fs::path retirement = {});
    ~FileGuard();
    FileGuard(FileGuard &&) noexcept = default;
    FileGuard &operator=(FileGuard &&) = delete;
    void backup(const fs::path &destination);
    void replace(const fs::path &source, bool restoreSecurity = false);
    void remove();
    HANDLE get() const { return handle_.get(); }

  private:
    void retire();
    struct Retired {
        Handle file;
        fs::path path;
    };
    fs::path path_;
    fs::path retirement_;
    Handle handle_;
    std::vector<Retired> retired_;
};

class InstallationLock {
  public:
    explicit InstallationLock(const fs::path &root);

  private:
    Handle file_;
};

} // namespace fcitx::setup::upgrade
