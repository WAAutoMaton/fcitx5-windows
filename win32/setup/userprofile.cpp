#include "userprofile.h"
#include "../dll/util.h"
#include "../ipc/atomicfile.h"
#include "../ipc/service.h"
#include "../tsf/pipeclient.h"
#include <atlcomcli.h>
#include <fstream>
#include <msctf.h>
#include <shlobj.h>

namespace fcitx::setup {
namespace {
namespace ipc = win32::ipc;

using InstallTip = BOOL(WINAPI *)(LPCWSTR, DWORD);
using EnumTips = UINT(WINAPI *)(LPCWSTR, LPCWSTR, LPCWSTR, InputProfile *,
                                UINT);

class InputApi {
  public:
    InputApi() {
        module_ =
            LoadLibraryExW(L"input.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module_) {
            ipc::serviceError();
        }
        install = reinterpret_cast<InstallTip>(
            GetProcAddress(module_, "InstallLayoutOrTip"));
        setDefault = reinterpret_cast<InstallTip>(
            GetProcAddress(module_, "SetDefaultLayoutOrTip"));
        enumerate = reinterpret_cast<EnumTips>(
            GetProcAddress(module_, "EnumEnabledLayoutOrTip"));
        if (!install || !setDefault || !enumerate) {
            FreeLibrary(module_);
            module_ = nullptr;
            ipc::serviceError(ERROR_PROC_NOT_FOUND);
        }
    }
    ~InputApi() { FreeLibrary(module_); }

    std::vector<InputProfile> profiles() const {
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            const auto count = enumerate(nullptr, nullptr, nullptr, nullptr, 0);
            if (!count || count > 4096) {
                ipc::serviceError(ERROR_INVALID_DATA);
            }
            std::vector<InputProfile> items(count);
            const auto copied =
                enumerate(nullptr, nullptr, nullptr, items.data(), count);
            if (copied && copied <= count) {
                items.resize(copied);
                return items;
            }
        }
        ipc::serviceError(ERROR_RETRY);
    }

    InstallTip install = nullptr;
    InstallTip setDefault = nullptr;
    EnumTips enumerate = nullptr;

  private:
    HMODULE module_ = nullptr;
};

struct UserBackup {
    DWORD magic = 0x31555346;
    DWORD version = 1;
    InputProfile previous;
    DWORD enabled = 0;
};

std::filesystem::path backupPath() {
    PWSTR folder = nullptr;
    const auto result =
        SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder);
    if (FAILED(result)) {
        ipc::serviceError(HRESULT_CODE(result));
    }
    const auto path =
        std::filesystem::path(folder) / L"fcitx5" / L"installer-user-state.bin";
    CoTaskMemFree(folder);
    return path;
}

UserBackup readBackup() {
    UserBackup backup;
    std::ifstream file(backupPath(), std::ios::binary);
    if (!file.read(reinterpret_cast<char *>(&backup), sizeof(backup)) ||
        file.peek() != EOF || backup.magic != 0x31555346 ||
        backup.version != 1) {
        ipc::serviceError(ERROR_INVALID_DATA);
    }
    return backup;
}

bool sameProfile(const InputProfile &a, const InputProfile &b) {
    if (a.type != b.type || a.language != b.language) {
        return false;
    }
    return a.type == 1 ? a.clsid == b.clsid && a.profile == b.profile
                       : std::wstring(a.id, wcsnlen(a.id, MAX_PATH)) ==
                             std::wstring(b.id, wcsnlen(b.id, MAX_PATH));
}

DWORD activate(const InputProfile &profile) {
    CComPtr<ITfInputProcessorProfileMgr> manager;
    auto result = manager.CoCreateInstance(CLSID_TF_InputProcessorProfiles);
    if (FAILED(result)) {
        return static_cast<DWORD>(result);
    }
    HKL layout = nullptr;
    if (profile.type == 2) {
        const auto id = std::wstring(profile.id, wcsnlen(profile.id, MAX_PATH));
        layout = LoadKeyboardLayoutW(id.c_str(), KLF_ACTIVATE);
        if (!layout) {
            return GetLastError();
        }
    } else {
        // Change the caller's input language before requesting desktop
        // activation.
        CComPtr<ITfInputProcessorProfiles> profiles;
        result = profiles.CoCreateInstance(CLSID_TF_InputProcessorProfiles);
        if (SUCCEEDED(result)) {
            result = profiles->ChangeCurrentLanguage(profile.language);
        }
        if (FAILED(result)) {
            return static_cast<DWORD>(result);
        }
    }
    result = manager->ActivateProfile(
        profile.type == 1 ? TF_PROFILETYPE_INPUTPROCESSOR
                          : TF_PROFILETYPE_KEYBOARDLAYOUT,
        profile.language, profile.type == 1 ? profile.clsid : CLSID_NULL,
        profile.type == 1 ? profile.profile : GUID_NULL, layout,
        TF_IPPMF_FORSESSION);
    return result == S_OK
               ? ERROR_SUCCESS
               : static_cast<DWORD>(FAILED(result) ? result : E_FAIL);
}

InputProfile fcitxProfile() {
    InputProfile profile;
    profile.type = 1;
    profile.language = TEXTSERVICE_LANGID_HANS;
    profile.clsid = FCITX_CLSID;
    profile.profile = PROFILE_GUID;
    return profile;
}
} // namespace

bool isFcitx(const InputProfile &profile) {
    return profile.type == 1 && profile.language == TEXTSERVICE_LANGID_HANS &&
           profile.clsid == FCITX_CLSID && profile.profile == PROFILE_GUID;
}

std::wstring profileId(const InputProfile &profile) {
    wchar_t language[16]{};
    swprintf_s(language, L"0x%04X:", profile.language);
    if (profile.type == 1) {
        return language +
               stringToWString(guidToString(profile.clsid), CP_UTF8) +
               stringToWString(guidToString(profile.profile), CP_UTF8) + L";";
    }
    if (profile.type != 2 || wcsnlen(profile.id, MAX_PATH) != 8) {
        return {};
    }
    const std::wstring id(profile.id, 8);
    if (id.find_first_not_of(L"0123456789abcdefABCDEF") != std::wstring::npos) {
        return {};
    }
    return std::wstring(language) + L"0x" + id;
}

const InputProfile *defaultProfile(const std::vector<InputProfile> &profiles) {
    for (const auto &profile : profiles) {
        if ((profile.flags & kDefaultProfile) &&
            !(profile.flags & kDisabledProfile)) {
            return &profile;
        }
    }
    return nullptr;
}

const InputProfile *restoreProfile(const std::vector<InputProfile> &profiles,
                                   const InputProfile &previous) {
    const InputProfile *fallback = nullptr;
    for (const auto &profile : profiles) {
        if (isFcitx(profile) || (profile.flags & kDisabledProfile) ||
            profileId(profile).empty()) {
            continue;
        }
        if (sameProfile(profile, previous)) {
            return &profile;
        }
        if (!fallback || (profile.flags & kDefaultProfile)) {
            fallback = &profile;
        }
    }
    return fallback;
}

DWORD configureUser(const std::filesystem::path &root) {
    InputApi api;
    const auto before = api.profiles();
    const auto saved = backupPath();
    const auto first = !std::filesystem::exists(saved);
    if (first) {
        UserBackup backup;
        const auto *previous = defaultProfile(before);
        if (previous) {
            backup.previous = *previous;
        }
        for (const auto &profile : before) {
            if (isFcitx(profile) && !(profile.flags & kDisabledProfile)) {
                backup.enabled = 1;
            }
        }
        if (!ipc::replaceFile(
                saved, std::string_view(reinterpret_cast<const char *>(&backup),
                                        sizeof(backup)))) {
            return ERROR_WRITE_FAULT;
        }
    } else {
        readBackup();
    }
    const auto tip = fcitxProfile();
    const auto id = profileId(tip);
    if (!api.install(id.c_str(), 0)) {
        return ERROR_INSTALL_FAILURE;
    }
    if (first) {
        if (!api.setDefault(id.c_str(), 0)) {
            return ERROR_INSTALL_FAILURE;
        }
        const auto activated = activate(tip);
        if (activated) {
            return activated;
        }
    }
    ipc::ServiceController controller(root / L"bin" / L"Fcitx5.exe");
    controller.restart();
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        PipeClient pipe;
        if (pipe.connect()) {
            ipc::SettingsReply settings;
            if (pipe.getSettings(settings) &&
                settings.error == ipc::SettingsError::None) {
                return ERROR_SUCCESS;
            }
        }
        Sleep(100);
    }
    return ERROR_SERVICE_REQUEST_TIMEOUT;
}

DWORD removeUser(const std::filesystem::path &root, bool rollback) {
    InputApi api;
    const auto before = api.profiles();
    UserBackup backup;
    const auto saved = std::filesystem::exists(backupPath());
    if (saved) {
        backup = readBackup();
    }
    const auto *current = defaultProfile(before);
    if (current && isFcitx(*current)) {
        const auto *replacement = restoreProfile(before, backup.previous);
        if (!replacement ||
            !api.setDefault(profileId(*replacement).c_str(), 0)) {
            return ERROR_INSTALL_FAILURE;
        }
        const auto result = activate(*replacement);
        if (result) {
            return result;
        }
    }
    if ((!rollback || !backup.enabled) &&
        !api.install(profileId(fcitxProfile()).c_str(), 1)) {
        return ERROR_INSTALL_FAILURE;
    }
    ipc::ServiceController controller(root / L"bin" / L"Fcitx5.exe");
    controller.stop();
    if (rollback && saved) {
        std::filesystem::remove(backupPath());
    }
    return ERROR_SUCCESS;
}

} // namespace fcitx::setup
