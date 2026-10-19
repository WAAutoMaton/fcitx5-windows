#pragma once

#include "../win32/ipc/protocol.h"
#include <fcitx/addonmanager.h>
#include <filesystem>
#include <vector>

namespace fcitx::win32 {

std::filesystem::path userPluginDirectory();
ipc::PluginsReply pluginSnapshot(AddonManager &manager);

// Discover direct package directories, retaining each legacy shared prefix
// first.
std::vector<std::filesystem::path>
pluginPrefixes(const std::filesystem::path &installation);

// Call before creating Instance or accessing StandardPaths::global().
void setupPluginEnvironment(const std::filesystem::path &installation,
                            const std::vector<std::filesystem::path> &plugins);

} // namespace fcitx::win32
