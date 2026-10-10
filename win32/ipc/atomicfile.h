#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>
#include <windows.h>

namespace fcitx::win32::ipc {

inline bool replaceFile(const std::filesystem::path &path,
                        std::string_view data) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error || data.size() > MAXDWORD) {
        return false;
    }
    static std::atomic_uint64_t sequence = 0;
    auto temporary = path;
    temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
                 std::to_wstring(++sequence);
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool saved =
        WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written,
                  nullptr) &&
        written == data.size() && FlushFileBuffers(file);
    CloseHandle(file);
    const bool replaced = saved && MoveFileExW(temporary.c_str(), path.c_str(),
                                               MOVEFILE_REPLACE_EXISTING |
                                                   MOVEFILE_WRITE_THROUGH);
    if (!replaced) {
        DeleteFileW(temporary.c_str());
    }
    return replaced;
}

} // namespace fcitx::win32::ipc
