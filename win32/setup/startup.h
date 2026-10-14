#pragma once

#include "../ipc/isolationpolicy.h"
#include <filesystem>

namespace fcitx::setup {

inline std::wstring loginStartupCommand(const std::filesystem::path &root) {
    if (!root.is_absolute())
        return {};
    const auto image =
        (root / L"bin" / L"Fcitx5.exe").lexically_normal().wstring();
    if (image.find(L'\0') != std::wstring::npos ||
        image.find(L'"') != std::wstring::npos)
        return {};
    return L"\"" + image + L"\" --login-start";
}

struct StartupChange {
    DWORD error = ERROR_SUCCESS;
    bool write = false;
    bool remove = false;
};

inline StartupChange planStartupChange(const std::wstring &command,
                                       const std::wstring &previous,
                                       bool enable) {
    if (command.empty())
        return {ERROR_INVALID_PARAMETER};
    const auto matches =
        CompareStringOrdinal(command.c_str(), -1, previous.c_str(), -1, TRUE) ==
        CSTR_EQUAL;
    if (!previous.empty() && !matches)
        return {
            static_cast<DWORD>(enable ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS)};
    return {ERROR_SUCCESS, enable && previous.empty(), !enable && matches};
}

inline DWORD setLoginStartup(const std::filesystem::path &root, bool enable) {
    win32::ipc::requireHostServices();
    constexpr wchar_t key[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t name[] = L"Fcitx5";
    wchar_t value[32768]{};
    DWORD size = sizeof(value);
    const auto query =
        RegGetValueW(HKEY_CURRENT_USER, key, name,
                     RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND,
                     nullptr, value, &size);
    if (query == ERROR_UNSUPPORTED_TYPE)
        return enable ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS;
    if (query != ERROR_SUCCESS && query != ERROR_FILE_NOT_FOUND)
        return query;
    const auto command = loginStartupCommand(root);
    const auto change = planStartupChange(
        command, query == ERROR_SUCCESS ? value : L"", enable);
    if (change.error || (!change.write && !change.remove))
        return change.error;
    HKEY run = nullptr;
    DWORD error = RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0,
                                  KEY_SET_VALUE, nullptr, &run, nullptr);
    if (error != ERROR_SUCCESS)
        return error;
    error =
        change.write
            ? RegSetValueExW(
                  run, name, 0, REG_SZ,
                  reinterpret_cast<const BYTE *>(command.c_str()),
                  static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)))
            : RegDeleteValueW(run, name);
    RegCloseKey(run);
    return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
}

} // namespace fcitx::setup
