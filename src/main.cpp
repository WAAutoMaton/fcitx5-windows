#include "windowsfrontend.h"
#include "windowskeyboard.h"
#include <fcitx-utils/environ.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/key.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/instance.h>
#include <filesystem>
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
    if (!SetDllDirectoryW((rootPath / "bin").c_str())) {
        throw std::runtime_error("Cannot set the Core runtime DLL directory");
    }
    auto fcitx_addon_dirs = rootPath / "lib" / "fcitx5";
    setenv("FCITX_ADDON_DIRS", fcitx_addon_dirs.string());
    auto xdg_data_dirs = rootPath / "share";
    auto fcitx_data_dirs = xdg_data_dirs / "fcitx5";
    setenv("XDG_DATA_DIRS", xdg_data_dirs.string());
    setenv("FCITX_DATA_DIRS", fcitx_data_dirs.string());
    setenv("LIBIME_MODEL_DIRS", (rootPath / "lib" / "libime").string());
}

void start() {
    Log::setLogRule("*=3,notimedate");
    setupEnv();
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
    pipeServer = std::make_unique<fcitx::win32::WindowsPipeServer>(*instance,
                                                                   *dispatcher);
    pipeServer->start();
    instance->eventLoop().exec();
    pipeServer->stop();
    pipeServer.reset();
    dispatcher.reset();
    instance.reset();
}
} // namespace fcitx

int main() {
    fcitx::start();
    return 0;
}
