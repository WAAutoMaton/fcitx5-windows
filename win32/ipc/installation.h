#pragma once

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace fcitx::win32::ipc {

inline constexpr wchar_t kInstallerKey[] = L"SOFTWARE\\Fcitx5\\Installer";
inline constexpr char kManagedInstall[] = "FCITX5-MANAGED-1\n";

inline std::filesystem::path installationRootForAddress(const void *address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &module)) {
        return {};
    }
    std::wstring path(32768, L'\0');
    const auto size = GetModuleFileNameW(module, path.data(),
                                         static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) {
        return {};
    }
    path.resize(size);
    return std::filesystem::path(path).parent_path().parent_path();
}

inline bool sameInstallationPath(const std::filesystem::path &left,
                                 const std::filesystem::path &right) {
    const auto a = left.lexically_normal().wstring();
    const auto b = right.lexically_normal().wstring();
    return !a.empty() && !b.empty() &&
           CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) ==
               CSTR_EQUAL;
}

inline bool registeredInstallationRoot(const std::filesystem::path &root) {
    wchar_t path[32768]{};
    DWORD size = sizeof(path);
    const auto result = RegGetValueW(
        HKEY_LOCAL_MACHINE, kInstallerKey, L"InstallRoot",
        RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, path, &size);
    return result == ERROR_SUCCESS && sameInstallationPath(root, path);
}

class InstallationGate {
  public:
    explicit InstallationGate(std::filesystem::path root)
        : root_(std::move(root)) {
        std::error_code error;
        const auto marker = root_ / "setup" / "managed-install";
        managed_ = registeredInstallationRoot(root_) ||
                   std::filesystem::exists(marker, error) || bool(error);
    }

    bool allowsStart() const {
        if (root_.empty()) {
            return false;
        }
        if (!managed_ && !registeredInstallationRoot(root_)) {
            return true;
        }
        std::ifstream state(root_ / "setup" / "install-state",
                            std::ios::binary);
        std::string value;
        std::getline(state, value);
        return state && value == "installed" && state.peek() == EOF;
    }

  private:
    std::filesystem::path root_;
    bool managed_ = false;
};

inline bool currentInstallationAllowsStart() {
    return InstallationGate(
               installationRootForAddress(reinterpret_cast<const void *>(
                   &currentInstallationAllowsStart)))
        .allowsStart();
}

} // namespace fcitx::win32::ipc
