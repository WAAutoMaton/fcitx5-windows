#include "windowsplugins.h"
#include <algorithm>
#include <fcitx-utils/environ.h>
#include <fcitx-utils/i18n.h>
#include <shlobj.h>
#include <stdexcept>
#include <string>
#include <windows.h>

namespace fcitx::win32 {
namespace {

std::string utf8(const std::filesystem::path &path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}

void setPaths(const char *name, const std::vector<std::filesystem::path> &roots,
              const std::filesystem::path &suffix) {
    std::string value;
    for (const auto &root : roots) {
        if (!value.empty()) {
            value += ';';
        }
        value += utf8(root / suffix);
    }
    setEnvironment(name, value.c_str());
}

void registerTranslations(const std::filesystem::path &locale) {
    std::error_code error;
    for (const auto &language :
         std::filesystem::directory_iterator(locale, error)) {
        for (const auto &catalog : std::filesystem::directory_iterator(
                 language.path() / "LC_MESSAGES", error)) {
            if (catalog.path().extension() == ".mo") {
                const auto domain = utf8(catalog.path().stem());
                registerDomain(domain.c_str(), locale);
            }
        }
    }
}

} // namespace

std::vector<std::filesystem::path>
pluginPrefixes(const std::filesystem::path &installation) {
    std::vector<std::filesystem::path> result{installation / "plugins"};
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,
                                       KF_FLAG_DONT_VERIFY, nullptr, &local))) {
        result.emplace_back(std::filesystem::path(local) / "Fcitx5" /
                            "plugins");
        CoTaskMemFree(local);
    }
    return result;
}

void setupPluginEnvironment(const std::filesystem::path &installation,
                            const std::vector<std::filesystem::path> &plugins) {
    // AddDllDirectory takes effect for LoadLibrary used by libdl only after
    // enabling USER_DIRS. Keep the Core DLLs in the application directory.
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) ||
        !SetDllDirectoryW((installation / "bin").c_str())) {
        throw std::runtime_error("Cannot configure the Core DLL search path");
    }
    std::vector<std::filesystem::path> roots{installation};
    for (const auto &plugin : plugins) {
        if (!plugin.is_absolute()) {
            throw std::runtime_error("Plugin prefix must be absolute");
        }
        if (std::find(roots.begin(), roots.end(), plugin) != roots.end()) {
            continue;
        }
        roots.push_back(plugin);
        std::error_code error;
        const auto bin = plugin / "bin";
        if (std::filesystem::is_directory(bin, error) &&
            !AddDllDirectory(bin.c_str())) {
            throw std::runtime_error("Cannot add the plugin DLL directory");
        }
    }
    setPaths("FCITX_ADDON_DIRS", roots, "lib/fcitx5");
    setPaths("XDG_DATA_DIRS", roots, "share");
    setPaths("FCITX_DATA_DIRS", roots, "share/fcitx5");
    setPaths("LIBIME_MODEL_DIRS", roots, "lib/libime");
    for (const auto &root : roots) {
        registerTranslations(root / "share/locale");
    }
}

} // namespace fcitx::win32
