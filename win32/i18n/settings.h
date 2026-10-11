#pragma once

#include "../ipc/protocol.h"
#include <filesystem>
#include <string_view>

namespace fcitx::win32::i18n {
using ipc::UiLanguage;

UiLanguage matchLanguage(std::wstring_view name);
UiLanguage resolveLanguage(UiLanguage language);
const char *localeName(UiLanguage language);
std::filesystem::path settingsPath();
bool readSettings(const std::filesystem::path &path,
                  ipc::GlobalSettings &settings);
ipc::SettingsError saveSettings(const std::filesystem::path &path,
                                const ipc::GlobalSettings &requested,
                                ipc::GlobalSettings &actual);
} // namespace fcitx::win32::i18n
