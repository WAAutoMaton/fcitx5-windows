#pragma once

#include <filesystem>
#include <vector>

namespace fcitx::win32 {

std::vector<std::filesystem::path>
pluginPrefixes(const std::filesystem::path &installation);

// Call before creating Instance or accessing StandardPaths::global().
void setupPluginEnvironment(const std::filesystem::path &installation,
                            const std::vector<std::filesystem::path> &plugins);

} // namespace fcitx::win32
