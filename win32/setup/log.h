#pragma once

#include <Windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <shlobj.h>
#include <string>
#include <string_view>

namespace fcitx::setup {

inline void logMessage(std::string_view message) noexcept {
    const auto lastError = GetLastError();
    PWSTR folder = nullptr;
    try {
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,
                                           KF_FLAG_CREATE, nullptr, &folder))) {
            const auto directory = std::filesystem::path(folder) / "fcitx5";
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            std::ofstream log(directory / "installer-helper.log",
                              std::ios::app);
            SYSTEMTIME now{};
            GetLocalTime(&now);
            char prefix[128]{};
            snprintf(prefix, sizeof(prefix),
                     "%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu tid=%lu ",
                     now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                     now.wSecond, now.wMilliseconds, GetCurrentProcessId(),
                     GetCurrentThreadId());
            log << prefix << message << '\n';
        }
    } catch (...) {
        // Diagnostic failures must not affect installation or its error code.
    }
    CoTaskMemFree(folder);
    SetLastError(lastError);
}

inline void logStep(std::string_view step, DWORD result) noexcept {
    const auto lastError = GetLastError();
    char code[64]{};
    snprintf(code, sizeof(code), " result=%lu (0x%08lX)", result, result);
    try {
        logMessage(std::string(step) + code);
    } catch (...) {
    }
    SetLastError(lastError);
}

inline void logMessage(std::wstring_view message) noexcept {
    const auto lastError = GetLastError();
    try {
        const auto size = WideCharToMultiByte(CP_UTF8, 0, message.data(),
                                              static_cast<int>(message.size()),
                                              nullptr, 0, nullptr, nullptr);
        if (size > 0) {
            std::string utf8(size, '\0');
            if (WideCharToMultiByte(CP_UTF8, 0, message.data(),
                                    static_cast<int>(message.size()),
                                    utf8.data(), size, nullptr,
                                    nullptr) == size) {
                logMessage(utf8);
            }
        }
    } catch (...) {
    }
    SetLastError(lastError);
}

} // namespace fcitx::setup
