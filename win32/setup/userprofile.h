#pragma once

#include <Windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace fcitx::setup {

struct InputProfile {
    DWORD type = 0;
    LANGID language = 0;
    CLSID clsid{};
    GUID profile{};
    GUID category{};
    DWORD substitute = 0;
    DWORD flags = 0;
    wchar_t id[MAX_PATH]{};
};

inline constexpr DWORD kDefaultProfile = 1;
inline constexpr DWORD kDisabledProfile = 2;

bool isFcitx(const InputProfile &profile);
std::wstring profileId(const InputProfile &profile);
const InputProfile *defaultProfile(const std::vector<InputProfile> &profiles);
const InputProfile *restoreProfile(const std::vector<InputProfile> &profiles,
                                   const InputProfile &previous);
DWORD configureUser(const std::filesystem::path &root);
DWORD removeUser(const std::filesystem::path &root, bool rollback);

} // namespace fcitx::setup
