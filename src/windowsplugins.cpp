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

bool isPluginPrefix(const std::filesystem::path &path) {
    const auto name = path.filename().wstring();
    for (const auto *reserved : {L"bin", L"lib", L"share"}) {
        if (CompareStringOrdinal(name.c_str(), -1, reserved, -1, TRUE) ==
            CSTR_EQUAL) {
            return false;
        }
    }
    for (const auto *subdirectory : {"bin", "lib", "share"}) {
        std::error_code error;
        if (std::filesystem::is_directory(path / subdirectory, error)) {
            return true;
        }
    }
    return false;
}

void appendPluginPrefixes(std::vector<std::filesystem::path> &prefixes,
                          const std::filesystem::path &directory) {
    // Keep the shared layout ahead of individual packages for compatibility.
    prefixes.push_back(directory);
    std::vector<std::filesystem::path> packages;
    std::error_code error;
    std::filesystem::directory_iterator entry(
        directory, std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::directory_iterator end;
    while (!error && entry != end) {
        std::error_code statusError;
        if (entry->is_directory(statusError) && isPluginPrefix(entry->path())) {
            packages.push_back(entry->path());
        }
        entry.increment(error);
    }
    std::sort(packages.begin(), packages.end());
    prefixes.insert(prefixes.end(), packages.begin(), packages.end());
}

} // namespace

std::vector<std::filesystem::path>
pluginPrefixes(const std::filesystem::path &installation) {
    std::vector<std::filesystem::path> result;
    appendPluginPrefixes(result, installation / "plugins");
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,
                                       KF_FLAG_DONT_VERIFY, nullptr, &local))) {
        appendPluginPrefixes(result, std::filesystem::path(local) / "Fcitx5" /
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
