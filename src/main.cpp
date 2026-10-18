#include "../win32/i18n/translator.h"
#include "../win32/ipc/diagnostic_log.h"
#include "../win32/ipc/service.h"
#include "../win32/ipc/userdata.h"
#include "windowsfrontend.h"
#include "windowskeyboard.h"
#include "windowslogging.h"
#include "windowsplugins.h"
#include <fcitx-utils/environ.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/instance.h>
#include <filesystem>
#include <libintl.h>
#include <stdexcept>
#include <windows.h>

namespace fs = std::filesystem;

namespace fcitx {
std::unique_ptr<Instance> instance;
std::unique_ptr<fcitx::EventDispatcher> dispatcher;
std::unique_ptr<fcitx::win32::WindowsPipeServer> pipeServer;

void setenv(const char *name, const std::string &value) {
    setEnvironment(name, value.c_str());
}

void setupEnv() {
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(),
                                           static_cast<DWORD>(path.size()));
    if (!length || length == path.size()) {
        throw std::runtime_error("Cannot locate the Core executable");
    }
    path.resize(length);
    auto rootPath = ::fs::path(path).parent_path().parent_path();
    win32::setupPluginEnvironment(rootPath, win32::pluginPrefixes(rootPath));
}

void start(const ::fs::path &settingsFile, const ::fs::path &uiSettingsFile,
           win32::ipc::ServiceProcess &service) {
    win32::ipc::GlobalSettings uiSettings;
    const bool settingsLoaded =
        win32::i18n::readSettings(uiSettingsFile, uiSettings);
    win32::CoreLogStream logStream;
    win32::applyDiagnosticLogging(uiSettings.detailedLogging);
    win32::diagnostic::coreLogger().maintain();
    if (!settingsLoaded) {
        FCITX_WARN()
            << "Cannot read UI language settings; using system language";
    }
    win32::diagnostic::coreLogger().event(win32::diagnostic::Level::Info,
                                          "core_start");
    setupEnv();
    const auto activeLanguage =
        win32::i18n::resolveLanguage(uiSettings.language);
    const auto language = win32::i18n::localeName(activeLanguage);
    setenv("LANGUAGE", language);
    setenv("FCITX_LOCALE", language);
    // An inherited C/C.UTF-8 locale makes gettext ignore LANGUAGE.
    setenv("LC_ALL", std::string(language) + ".UTF-8");
    // libintl's setlocale also initializes LC_MESSAGES on Windows.
    const auto *locale = ::setlocale(LC_ALL, "");
    if (!locale || std::string_view(locale) == "C") {
        if (!::setlocale(LC_ALL, ".UTF-8")) {
            FCITX_WARN() << "Cannot initialize a non-C locale for gettext";
        }
    }
    // Bind before addon factories register their build-time locale directory.
    registerDomain("fcitx5-chinese-addons",
                   StandardPaths::fcitxPath("localedir"));
    instance = std::make_unique<Instance>(0, nullptr);
    auto &addonMgr = instance->addonManager();
    win32::WindowsKeyboardFactory keyboardFactory;
    StaticAddonRegistry registry{{"windowskeyboard", &keyboardFactory}};
    addonMgr.registerDefaultLoader(&registry);
    instance->initialize();
    auto &imManager = instance->inputMethodManager();
    if (imManager.entry("pinyin") && imManager.entry("keyboard-us") &&
        addonMgr.addon("pinyin", true)) {
        InputMethodGroup group("Windows");
        group.setDefaultLayout("us");
        group.inputMethodList().emplace_back("keyboard-us");
        group.inputMethodList().emplace_back("pinyin");
        if (imManager.entry("shuangpin")) {
            group.inputMethodList().emplace_back("shuangpin");
        }
        group.setDefaultInputMethod("pinyin");
        imManager.addEmptyGroup(group.name());
        imManager.setGroup(std::move(group));
        imManager.setCurrentGroup("Windows");
        win32::WindowsInputContext warmup(instance->inputContextManager(), 0,
                                          "win32-startup");
        warmup.setCapabilities(1ULL << 1);
        warmup.focusIn();
        instance->setCurrentInputMethod(&warmup, "pinyin", true);
        KeyEvent event(&warmup, Key(FcitxKey_n));
        warmup.keyEvent(event);
        warmup.reset();
        warmup.focusOut();
    } else {
        FCITX_WARN()
            << "Pinyin is unavailable; install the Chinese addons and data";
    }
    dispatcher = std::make_unique<fcitx::EventDispatcher>();
    dispatcher->attach(&instance->eventLoop());
    pipeServer = std::make_unique<fcitx::win32::WindowsPipeServer>(
        *instance, *dispatcher, settingsFile, uiSettingsFile, activeLanguage,
        uiSettings.detailedLogging);
    pipeServer->start();
    auto shutdown = instance->eventLoop().addTimeEvent(
        CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 100000, 0,
        [&service](EventSourceTime *source, uint64_t) {
            if (service.stopping()) {
                instance->eventLoop().exit();
            } else {
                source->setNextInterval(100000);
                source->setOneShot();
            }
            return true;
        });
    if (!shutdown) {
        throw std::runtime_error("Cannot watch Core shutdown requests");
    }
    instance->eventLoop().exec();
    win32::diagnostic::coreLogger().event(win32::diagnostic::Level::Info,
                                          "core_stop");
    shutdown.reset();
    pipeServer->stop();
    pipeServer.reset();
    dispatcher.reset();
    instance.reset();
    win32::diagnostic::coreLogger().setEnabled(false);
    win32::diagnostic::coreLogger().flush(std::chrono::milliseconds(250));
}
} // namespace fcitx

int main(int argc, char **argv) {
    try {
        fcitx::win32::ipc::requireHostServices();
        if (argc == 2 && std::string_view(argv[1]) == "--check-ui-test") {
#ifdef FCITX5_UI_TEST
            return 0;
#else
            return 1;
#endif
        }
        if (!fcitx::win32::ipc::currentInstallationAllowsStart()) {
            return 0;
        }
        const bool loginStart =
            argc == 2 && std::string_view(argv[1]) == "--login-start";
        if (loginStart && !fcitx::win32::ipc::serviceAutoStartAllowed())
            return 0;
        if (argc == 2 && std::string_view(argv[1]) == "--open-user-data") {
            return static_cast<int>(fcitx::win32::ipc::openUserDataFolder());
        }
        if (argc == 2 && (std::string_view(argv[1]) == "--stop-services" ||
                          std::string_view(argv[1]) == "--restart-services")) {
            std::wstring path(32768, L'\0');
            const auto size = GetModuleFileNameW(
                nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (!size || size >= path.size()) {
                fcitx::win32::ipc::serviceError();
            }
            path.resize(size);
            const fs::path core(path);
            fcitx::win32::ipc::ServiceController controller(core);
            if (std::string_view(argv[1]) == "--stop-services") {
                controller.stop();
            } else if (controller.restart()) {
                bool ready = false;
                for (unsigned attempt = 0; attempt < 50; ++attempt) {
                    if (WaitNamedPipeW(fcitx::win32::ipc::pipeName().c_str(),
                                       50)) {
                        ready = true;
                        break;
                    }
                    Sleep(100);
                }
                if (!ready) {
                    fcitx::win32::ipc::serviceError(
                        ERROR_SERVICE_REQUEST_TIMEOUT);
                }
                fcitx::win32::ipc::launchServiceExecutable(
                    core.parent_path().parent_path() / "settings" /
                        "Fcitx5Settings.exe",
                    false);
            }
            return 0;
        }
        fs::path settingsFile;
        fs::path uiSettingsFile;
        for (int i = loginStart ? 2 : 1; i < argc; i += 2) {
            if (i + 1 >= argc)
                throw std::runtime_error("Missing settings path");
            auto path = fs::absolute(fs::path(
                std::u8string(reinterpret_cast<const char8_t *>(argv[i + 1]))));
            if (std::string_view(argv[i]) == "--windows-settings-file")
                settingsFile = path;
            else if (std::string_view(argv[i]) == "--windows-ui-settings-file")
                uiSettingsFile = path;
            else
                throw std::runtime_error("Unknown Core argument");
        }
        if (uiSettingsFile.empty())
            uiSettingsFile =
                settingsFile.empty()
                    ? fcitx::win32::i18n::settingsPath()
                    : settingsFile.parent_path() / "windows-ui.conf";
        fcitx::win32::ipc::ServiceProcess service(L"Core");
        if (!service.acquire()) {
            return 0;
        }
        fcitx::start(settingsFile, uiSettingsFile, service);
        return 0;
    } catch (const std::exception &error) {
        FCITX_ERROR() << error.what();
        if (argc == 2 && (std::string_view(argv[1]) == "--stop-services" ||
                          std::string_view(argv[1]) == "--restart-services")) {
            MessageBoxW(nullptr, L"Cannot stop or restart Fcitx5 services.",
                        L"Fcitx5", MB_OK | MB_ICONERROR);
        }
        return 1;
    }
}
