#include "userprofile.h"
#include "../dll/util.h"
#include "../ipc/atomicfile.h"
#include "../ipc/service.h"
#include "../tsf/pipeclient.h"
#include "log.h"
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
        logStep("LoadLibraryExW input.dll",
                module_ ? ERROR_SUCCESS : GetLastError());
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
            logStep("EnumEnabledLayoutOrTip count", count);
            if (!count || count > 4096) {
                ipc::serviceError(ERROR_INVALID_DATA);
            }
            std::vector<InputProfile> items(count);
            const auto copied =
                enumerate(nullptr, nullptr, nullptr, items.data(), count);
            logStep("EnumEnabledLayoutOrTip copied", copied);
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

void logProfileState(const char *stage, ITfInputProcessorProfiles *profiles,
                     const InputProfile &profile) noexcept {
    const auto lastError = GetLastError();
    LANGID language = 0;
    BOOL enabled = FALSE;
    const auto currentResult = profiles->GetCurrentLanguage(&language);
    const auto enabledResult = profiles->IsEnabledLanguageProfile(
        profile.clsid, profile.language, profile.profile, &enabled);
    char state[256]{};
    snprintf(state, sizeof(state),
             "%s GetCurrentLanguage=0x%08lX current=0x%04X target=0x%04X "
             "IsEnabledLanguageProfile=0x%08lX enabled=%d",
             stage, static_cast<DWORD>(currentResult), language,
             profile.language, static_cast<DWORD>(enabledResult), enabled);
    logMessage(state);
    SetLastError(lastError);
}

DWORD activate(const InputProfile &profile) {
    logMessage(L"activate target=" + profileId(profile));
    CComPtr<ITfInputProcessorProfileMgr> manager;
    auto result = manager.CoCreateInstance(CLSID_TF_InputProcessorProfiles);
    logStep("CoCreateInstance ITfInputProcessorProfileMgr",
            static_cast<DWORD>(result));
    if (FAILED(result)) {
        return static_cast<DWORD>(result);
    }
    HKL layout = nullptr;
    if (profile.type == 2) {
        const auto id = std::wstring(profile.id, wcsnlen(profile.id, MAX_PATH));
        layout = LoadKeyboardLayoutW(id.c_str(), KLF_ACTIVATE);
        const auto error = layout ? ERROR_SUCCESS : GetLastError();
        logStep("LoadKeyboardLayoutW", error);
        if (!layout) {
            return error;
        }
    } else {
        // Change the caller's input language before requesting desktop
        // activation.
        CComPtr<ITfInputProcessorProfiles> profiles;
        result = profiles.CoCreateInstance(CLSID_TF_InputProcessorProfiles);
        logStep("CoCreateInstance ITfInputProcessorProfiles",
                static_cast<DWORD>(result));
        if (SUCCEEDED(result)) {
            logProfileState("before ChangeCurrentLanguage", profiles, profile);
            result = profiles->ChangeCurrentLanguage(profile.language);
            logStep("ChangeCurrentLanguage", static_cast<DWORD>(result));
            logProfileState("after ChangeCurrentLanguage", profiles, profile);
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
    logStep("ActivateProfile TF_IPPMF_FORSESSION raw HRESULT",
            static_cast<DWORD>(result));
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
    logMessage("configureUser begin");
    InputApi api;
    const auto before = api.profiles();
    const auto saved = backupPath();
    const auto first = !std::filesystem::exists(saved);
    logStep("configureUser first initialization", first);
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
        const auto written = ipc::replaceFile(
            saved, std::string_view(reinterpret_cast<const char *>(&backup),
                                    sizeof(backup)));
        logStep("Save user profile backup",
                written ? ERROR_SUCCESS : ERROR_WRITE_FAULT);
        if (!written) {
            return ERROR_WRITE_FAULT;
        }
    } else {
        logMessage("Read existing user profile backup");
        readBackup();
    }
    const auto tip = fcitxProfile();
    const auto id = profileId(tip);
    const auto installed = api.install(id.c_str(), 0);
    logStep("InstallLayoutOrTip enable BOOL", installed);
    if (!installed) {
        return ERROR_INSTALL_FAILURE;
    }
    if (first) {
        const auto setDefault = api.setDefault(id.c_str(), 0);
        logStep("SetDefaultLayoutOrTip BOOL", setDefault);
        if (!setDefault) {
            return ERROR_INSTALL_FAILURE;
        }
        const auto activated = activate(tip);
        logStep("configureUser activate result", activated);
        if (activated) {
            return activated;
        }
    }
    logMessage("configureUser Core restart begin");
    ipc::ServiceController controller(root / L"bin" / L"Fcitx5.exe");
    controller.restart();
    logMessage("configureUser Core restart complete; waiting for GetSettings");
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        PipeClient pipe;
        if (pipe.connect()) {
            ipc::SettingsReply settings;
            if (pipe.getSettings(settings) &&
                settings.error == ipc::SettingsError::None) {
                logStep("configureUser Core GetSettings ready", ERROR_SUCCESS);
                return ERROR_SUCCESS;
            }
        }
        Sleep(100);
    }
    logStep("configureUser Core GetSettings timeout",
            ERROR_SERVICE_REQUEST_TIMEOUT);
    return ERROR_SERVICE_REQUEST_TIMEOUT;
}

DWORD removeUser(const std::filesystem::path &root, bool rollback) {
    logMessage(rollback ? "removeUser rollback begin"
                        : "removeUser uninstall begin");
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
