#include "windowssettings.h"
#include "../win32/ipc/atomicfile.h"
#include "../win32/ipc/service.h"
#include <algorithm>
#include <charconv>
#include <fcitx-config/configuration.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputmethodmanager.h>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fcitx::win32 {
namespace {
ipc::ShuangpinProfile profileFromValue(const std::string &value) {
    for (unsigned i = 0;
         i <= static_cast<unsigned>(ipc::ShuangpinProfile::Custom); ++i) {
        const auto profile = static_cast<ipc::ShuangpinProfile>(i);
        if (ipc::profileConfigValue(profile) == value) {
            return profile;
        }
    }
    throw std::runtime_error("Invalid ShuangpinProfile configuration");
}

std::string value(const RawConfig &config, const char *key) {
    const auto node = config.get(key);
    return node ? node->value() : "";
}

void makeExplicit(RawConfig &config) {
    config.setImplicit(false);
    for (const auto &key : config.subItems()) {
        makeExplicit(*config.get(key));
    }
}
} // namespace

WindowsSettings::WindowsSettings(Instance &instance, std::filesystem::path file)
    : instance_(instance), file_(std::move(file)) {
    if (file_.empty()) {
        file_ = StandardPaths::global().userDirectory(
                    StandardPathsType::PkgConfig) /
                "conf/windows.conf";
    }
    if (auto *addon = instance_.addonManager().addon("pinyin", true)) {
        if (const auto *configuration = addon->getConfig()) {
            RawConfig raw;
            configuration->save(raw);
            settings_.profile =
                profileFromValue(value(raw, "ShuangpinProfile"));
        }
    }
    if (std::filesystem::exists(file_)) {
        RawConfig raw;
        std::ifstream stream(file_, std::ios::binary);
        if (!stream) {
            throw std::runtime_error("Cannot read Windows input settings");
        }
        readFromIni(raw, stream);
        const auto scheme = value(raw, "Scheme");
        if ((value(raw, "Version") != "1" && value(raw, "Version") != "2") ||
            (scheme != "Full" && scheme != "Double")) {
            throw std::runtime_error("Invalid Windows input settings");
        }
        settings_.scheme = scheme == "Double" ? ipc::PinyinScheme::Double
                                              : ipc::PinyinScheme::Full;
        settings_.profile = profileFromValue(value(raw, "Profile"));
        if (value(raw, "Version") == "2") {
            if (auto list = raw.get("InputMethods")) {
                for (const auto &key : list->subItems()) {
                    enabled_.push_back(value(*list, key.c_str()));
                }
            }
            if (!ipc::validSelection({1, enabled_})) {
                throw std::runtime_error("Invalid Windows input method list");
            }
            if (auto overrides = raw.get("Options")) {
                overrides_ = *overrides;
            }
        }
        const auto revision = value(raw, "Revision");
        const auto parsed =
            std::from_chars(revision.data(), revision.data() + revision.size(),
                            settings_.revision);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != revision.data() + revision.size() ||
            !settings_.revision) {
            throw std::runtime_error("Invalid Windows settings revision");
        }
        if (snapshot().pinyinAvailable && !applyProfile(settings_.profile)) {
            throw std::runtime_error("Cannot restore Windows input settings");
        }
    }
    initializeMethods();
    instance_.inputMethodManager().setDefaultInputMethod(chineseEntry());
}

const char *WindowsSettings::chineseEntry() const {
    const auto preferred =
        settings_.scheme == ipc::PinyinScheme::Double ? "shuangpin" : "pinyin";
    if (const auto *info = method(preferred);
        selected(preferred) && info && info->available) {
        return preferred;
    }
    for (const auto &id : enabled_) {
        const auto *info = method(id);
        if (info && info->available && !info->keyboard) {
            return id.c_str();
        }
    }
    for (const auto &id : enabled_) {
        const auto *info = method(id);
        if (info && info->available) {
            return id.c_str();
        }
    }
    return "keyboard-us";
}

bool WindowsSettings::chineseMode(InputContext *context) const {
    const auto *entry = instance_.inputMethodEntry(context);
    const auto *info = entry ? method(entry->uniqueName()) : nullptr;
    return info && !info->keyboard;
}

ipc::SettingsReply WindowsSettings::snapshot(ipc::SettingsError error) const {
    ipc::SettingsReply reply;
    reply.error = error;
    reply.settings = settings_;
    reply.pinyinAvailable = instance_.inputMethodManager().entry("pinyin") &&
                            instance_.addonManager().addon("pinyin", true);
    reply.shuangpinAvailable =
        reply.pinyinAvailable &&
        instance_.inputMethodManager().entry("shuangpin");
    return reply;
}

bool WindowsSettings::applyProfile(ipc::ShuangpinProfile profile) {
    auto *addon = instance_.addonManager().addon("pinyin", true);
    if (!addon || !addon->getConfig()) {
        return false;
    }
    RawConfig raw;
    addon->getConfig()->save(raw);
    const auto requested = std::string(ipc::profileConfigValue(profile));
    if (value(raw, "ShuangpinProfile") == requested) {
        return true;
    }
    raw.setValueByPath("ShuangpinProfile", requested);
    addon->setConfig(raw);
    RawConfig updated;
    addon->getConfig()->save(updated);
    return value(updated, "ShuangpinProfile") == requested;
}

bool WindowsSettings::save(const ipc::PinyinSettings &settings) const {
    RawConfig raw;
    raw.setValueByPath("Version", "2");
    for (size_t i = 0; i < enabled_.size(); ++i) {
        raw.setValueByPath("InputMethods/" + std::to_string(i), enabled_[i]);
    }
    raw["Options"] = overrides_;
    // The Windows record is authoritative, including values reset to defaults.
    makeExplicit(raw);
    raw.setValueByPath("Scheme", settings.scheme == ipc::PinyinScheme::Double
                                     ? "Double"
                                     : "Full");
    raw.setValueByPath("Profile",
                       std::string(ipc::profileConfigValue(settings.profile)));
    raw.setValueByPath("Revision", std::to_string(settings.revision));
    std::ostringstream stream;
    writeAsIni(raw, stream);
    return stream && ipc::replaceFile(file_, stream.str());
}

ipc::SettingsError
WindowsSettings::apply(const ipc::PinyinSettings &requested) {
    resetRequired_ = false;
    if (!ipc::validSettings(requested)) {
        return ipc::SettingsError::Invalid;
    }
    if (requested.profile == ipc::ShuangpinProfile::Custom &&
        settings_.profile != ipc::ShuangpinProfile::Custom) {
        return ipc::SettingsError::Invalid;
    }
    const auto capabilities = snapshot();
    if (!capabilities.pinyinAvailable ||
        (requested.scheme == ipc::PinyinScheme::Double &&
         !capabilities.shuangpinAvailable)) {
        return ipc::SettingsError::Unavailable;
    }
    if (requested.scheme == settings_.scheme &&
        requested.profile == settings_.profile) {
        return ipc::SettingsError::None;
    }
    if (requested.revision != settings_.revision ||
        settings_.revision == UINT64_MAX) {
        return ipc::SettingsError::Conflict;
    }
    const auto previous = settings_;
    const auto previousEnabled = enabled_;
    const auto previousOverrides = overrides_;
    if (auto config = overrides_.get("pinyin")) {
        config->setValueByPath(
            "ShuangpinProfile",
            std::string(ipc::profileConfigValue(requested.profile)));
    }
    const auto oldEntry =
        settings_.scheme == ipc::PinyinScheme::Double ? "shuangpin" : "pinyin";
    const auto newEntry =
        requested.scheme == ipc::PinyinScheme::Double ? "shuangpin" : "pinyin";
    if (auto found = std::find(enabled_.begin(), enabled_.end(), oldEntry);
        found != enabled_.end() && oldEntry != newEntry &&
        !selected(newEntry)) {
        *found = newEntry;
    }
    auto next = requested;
    next.revision = settings_.revision + 1;
    try {
        // Updating the addon clears libime contexts. Fail storage before doing
        // that.
        if (!save(next)) {
            enabled_ = previousEnabled;
            overrides_ = previousOverrides;
            return ipc::SettingsError::SaveFailed;
        }
        resetRequired_ = true;
        if (!applyProfile(next.profile)) {
            enabled_ = previousEnabled;
            overrides_ = previousOverrides;
            applyProfile(previous.profile);
            if (!save(previous)) {
                throw std::runtime_error(
                    "Cannot restore the previous Windows settings file");
            }
            return ipc::SettingsError::ApplyFailed;
        }
    } catch (const std::exception &exception) {
        enabled_ = previousEnabled;
        overrides_ = previousOverrides;
        FCITX_ERROR() << "Windows settings update failed: " << exception.what();
        try {
            applyProfile(previous.profile);
        } catch (...) {
        }
        if (!save(previous)) {
            FCITX_ERROR() << "Windows settings rollback could not be persisted";
        }
        return ipc::SettingsError::ApplyFailed;
    }
    settings_ = next;
    return ipc::SettingsError::None;
}

ipc::SettingsError WindowsSettings::openWindow() const {
    if (!ipc::serviceAutoStartAllowed()) {
        return ipc::SettingsError::LaunchFailed;
    }
    std::wstring executable(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, executable.data(),
                                         static_cast<DWORD>(executable.size()));
    if (!size || size >= executable.size()) {
        return ipc::SettingsError::LaunchFailed;
    }
    executable.resize(size);
    const auto directory =
        std::filesystem::path(executable).parent_path().parent_path() /
        "settings";
    const auto path = directory / "Fcitx5Settings.exe";
    auto command = L"\"" + path.wstring() + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE,
                        0, nullptr, directory.c_str(), &startup, &process)) {
        return ipc::SettingsError::LaunchFailed;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return ipc::SettingsError::None;
}

} // namespace fcitx::win32
