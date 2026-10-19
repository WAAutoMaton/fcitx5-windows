#include "../../src/windowsplugins.h"
#include <algorithm>
#include <fcitx-utils/environ.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

namespace {
namespace fs = std::filesystem;

class TestFactory : public fcitx::AddonFactory {
  public:
    fcitx::AddonInstance *create(fcitx::AddonManager *) override {
        ++created;
        return new fcitx::AddonInstance;
    }
    unsigned created = 0;
};

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
        fcitx::setEnvironment("SKIP_FCITX_USER_PATH", "1");
        fcitx::setEnvironment("SKIP_FCITX_SYSTEM_PATH", "1");
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
        const auto descriptions = temporary.path / "addons";
        fs::create_directories(descriptions);
        for (const auto *id : {"loaded", "pending", "disabled"}) {
            std::ofstream config(descriptions / (std::string(id) + ".conf"));
            config << "[Addon]\nName=" << id
                   << "\nComment=Test plugin\nVersion=1.2.3\nCategory=Module\n"
                      "Type=StaticLibrary\nLibrary=test\nOnDemand="
                   << (std::string(id) == "loaded" ? "False" : "True")
                   << "\nEnabled="
                   << (std::string(id) == "disabled" ? "False" : "True")
                   << '\n';
        }
        TestFactory factory;
        fcitx::StaticAddonRegistry registry{{"loaded", &factory},
                                            {"pending", &factory},
                                            {"disabled", &factory}};
        fcitx::AddonManager manager(utf8(descriptions));
        manager.registerDefaultLoader(&registry);
        manager.load();
        require(factory.created == 1, "Only the eager addon should load");
        const auto snapshot = fcitx::win32::pluginSnapshot(manager);
        require(snapshot.plugins.size() == 3 &&
                    snapshot.directory ==
                        utf8(fcitx::win32::userPluginDirectory()),
                "Plugin snapshot or personal directory missing");
        using fcitx::win32::ipc::PluginStatus;
        require(snapshot.plugins[0].id == "disabled" &&
                    snapshot.plugins[0].status == PluginStatus::Disabled &&
                    snapshot.plugins[1].id == "loaded" &&
                    snapshot.plugins[1].status == PluginStatus::Loaded &&
                    snapshot.plugins[2].id == "pending" &&
                    snapshot.plugins[2].status == PluginStatus::NotLoaded,
                "Plugin status or deterministic ordering is wrong");
        require(snapshot.plugins[1].version == "1.2.3" &&
                    snapshot.plugins[1].library == "test" &&
                    snapshot.plugins[1].comment == "Test plugin",
                "Plugin metadata missing");
        require(factory.created == 1 && !manager.lookupAddon("pending"),
                "Inspecting plugins must not load on-demand addons");
        require(manager.addon("pending", true) && factory.created == 2 &&
                    fcitx::win32::pluginSnapshot(manager).plugins[2].status ==
                        PluginStatus::Loaded,
                "Snapshot did not reflect the current loaded instance");
        std::cout << "PASS: plugin discovery, legacy layout, ordering and "
                     "search paths\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
