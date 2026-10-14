#pragma once

#include <filesystem>
#include <memory>
#include <shellapi.h>
#include <shlobj.h>

namespace fcitx::win32::ipc {

inline DWORD openUserDataFolder() {
    const auto initialized = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized))
        return HRESULT_CODE(initialized);
    struct ComLifetime {
        ~ComLifetime() { CoUninitialize(); }
    } lifetime;
    PWSTR raw = nullptr;
    const auto result =
        SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw);
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owner(
        raw, &CoTaskMemFree);
    if (FAILED(result))
        return HRESULT_CODE(result);
    const auto path = std::filesystem::path(raw) / L"Fcitx5";
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error)
        return static_cast<DWORD>(error.value());
    SHELLEXECUTEINFOW execute{sizeof(execute)};
    execute.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    execute.lpVerb = L"open";
    execute.lpFile = path.c_str();
    execute.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&execute) ? ERROR_SUCCESS : GetLastError();
}

} // namespace fcitx::win32::ipc
