#include "../../src/windowsplugins.h"
#include <algorithm>
#include <fcitx-utils/environ.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

namespace {
namespace fs = std::filesystem;

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        const auto base = fs::temp_directory_path();
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            path = base /
                   ("fcitx5-plugins-" + std::to_string(GetCurrentProcessId()) +
                    "-" + std::to_string(attempt));
            if (fs::create_directory(path)) {
                return;
            }
        }
        throw std::runtime_error(
            "Cannot create the temporary plugin directory");
    }

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path, error);
    }

    fs::path path;
};

std::string utf8(const fs::path &path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}

void requirePaths(const char *variable, const std::vector<fs::path> &prefixes,
                  const fs::path &suffix) {
    std::string expected;
    for (const auto &prefix : prefixes) {
        if (!expected.empty()) {
            expected += ';';
        }
        expected += utf8(prefix / suffix);
    }
    require(fcitx::getEnvironmentOrEmpty(variable) == expected, variable);
}
} // namespace

int main() {
    try {
        TemporaryDirectory temporary;
        const auto installation = temporary.path / "Core with spaces";
        const auto plugins = installation / "plugins";
        // Discovery must tolerate an absent root or a file in place of a root.
        require(fcitx::win32::pluginPrefixes(installation).front() == plugins,
                "Missing shared prefix must remain compatible");
        fs::create_directories(installation);
        std::ofstream(plugins).put('\n');
        require(fcitx::win32::pluginPrefixes(installation).front() == plugins,
                "Non-directory plugin root must be ignored");
        fs::remove(plugins);

        const auto unicode = plugins / fs::path(u8"\u6e29\u5dde\u8bdd");
        // Create in reverse order to catch reliance on enumeration order.
        fs::create_directories(unicode / "share/locale");
        fs::create_directories(plugins / "zeta/lib/fcitx5");
        fs::create_directories(plugins / "resources/share/fcitx5");
        fs::create_directories(plugins / "private/bin");
        fs::create_directories(plugins / "Alpha/lib/fcitx5");
        fs::create_directories(plugins / "empty");
        fs::create_directories(plugins / "nested/inner/lib/fcitx5");
        fs::create_directories(plugins / "invalid");
        std::ofstream(plugins / "invalid/lib").put('\n');
        std::ofstream(plugins / "not-a-directory").put('\n');
        // Reserved names must not become packages even with nested prefixes.
        fs::create_directories(plugins / "BiN/share");
        fs::create_directories(plugins / "LIB/lib");
        fs::create_directories(plugins / "Share/bin");

        const std::vector<fs::path> expected{plugins,
                                             plugins / "Alpha",
                                             plugins / "private",
                                             plugins / "resources",
                                             plugins / "zeta",
                                             unicode};
        const auto discovered = fcitx::win32::pluginPrefixes(installation);
        require(discovered.size() >= expected.size(),
                "Missing plugin packages");
        require(
            std::equal(expected.begin(), expected.end(), discovered.begin()),
            "Unexpected plugin selection or search order");
        if (discovered.size() > expected.size()) {
            require(discovered[expected.size()].filename() == "plugins",
                    "User shared prefix must follow portable packages");
        }

        // Use only temporary prefixes when configuring DLL search directories.
        fs::create_directories(installation / "bin");
        auto duplicates = expected;
        duplicates.push_back(expected[1]);
        fcitx::win32::setupPluginEnvironment(installation, duplicates);
        auto roots = expected;
        roots.insert(roots.begin(), installation);
        requirePaths("FCITX_ADDON_DIRS", roots, "lib/fcitx5");
        requirePaths("FCITX_DATA_DIRS", roots, "share/fcitx5");
        requirePaths("XDG_DATA_DIRS", roots, "share");
        requirePaths("LIBIME_MODEL_DIRS", roots, "lib/libime");
        std::cout << "PASS: plugin discovery, legacy layout, ordering and "
                     "search paths\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
