#include "../../src/windowskeyboard.h"
#include "../../src/windowsplugins.h"
#include <fcitx-config/configuration.h>
#include <fcitx-utils/environ.h>
#include <fcitx-utils/log.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
class ProbeContext final : public fcitx::InputContext {
  public:
    explicit ProbeContext(fcitx::InputContextManager &manager)
        : InputContext(manager, "addon-probe") {
        created();
    }
    ~ProbeContext() override { destroy(); }
    const char *frontend() const override { return "addon-probe"; }
    std::string commit;

  protected:
    void commitStringImpl(const std::string &text) override { commit += text; }
    void deleteSurroundingTextImpl(int, unsigned) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
};

std::filesystem::path path(const char *value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t *>(value)));
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 5) {
            throw std::runtime_error(
                "Usage: addon_probe <Core prefix> <plugin prefix> <method> "
                "<keys|--absent|--unavailable>");
        }
        fcitx::setEnvironment("SKIP_FCITX_USER_PATH", "1");
        fcitx::setEnvironment("SKIP_FCITX_SYSTEM_PATH", "1");
        fcitx::setEnvironment("SKIP_FCITX_PATH", "0");
        fcitx::Log::setLogRule("*=3,key_trace=0");
        const auto installation = path(argv[1]);
        const auto plugins =
            std::string_view(argv[2]) == "--default"
                ? fcitx::win32::pluginPrefixes(installation)
                : std::vector<std::filesystem::path>{path(argv[2])};
        fcitx::win32::setupPluginEnvironment(installation, plugins);
        fcitx::Instance instance(0, nullptr);
        fcitx::win32::WindowsKeyboardFactory keyboard;
        fcitx::StaticAddonRegistry registry{{"windowskeyboard", &keyboard}};
        instance.addonManager().registerDefaultLoader(&registry);
        instance.initialize();
        auto &manager = instance.inputMethodManager();
        const auto *entry = manager.entry(argv[3]);
        const std::string_view keys(argv[4]);
        if (keys == "--absent") {
            if (entry) {
                throw std::runtime_error("Unexpected input method entry");
            }
            std::cout << "PASS: plugin is absent\n";
            return 0;
        }
        if (!entry) {
            throw std::runtime_error("Input method was not discovered");
        }
        auto *addon = instance.addonManager().addon(entry->addon(), true);
        if (keys == "--unavailable") {
            if (addon) {
                throw std::runtime_error(
                    "Unexpectedly loaded incomplete plugin");
            }
            std::cout << "PASS: incomplete plugin is unavailable\n";
            return 0;
        }
        const auto *engine = dynamic_cast<fcitx::InputMethodEngine *>(addon);
        const auto *config =
            engine ? engine->getConfigForInputMethod(*entry) : nullptr;
        if (!config) {
            throw std::runtime_error(
                "Cannot load input method and configuration");
        }
        fcitx::RawConfig values, description;
        config->save(values);
        config->dumpDescription(description);
        if (values.subItems().empty() || description.subItems().empty()) {
            throw std::runtime_error("Input method configuration is empty");
        }
        fcitx::InputMethodGroup group("Probe");
        group.setDefaultLayout("us");
        group.inputMethodList().emplace_back("keyboard-us");
        group.inputMethodList().emplace_back(entry->uniqueName());
        group.setDefaultInputMethod(entry->uniqueName());
        manager.addEmptyGroup(group.name());
        manager.setGroup(std::move(group));
        manager.setCurrentGroup("Probe");
        ProbeContext context(instance.inputContextManager());
        context.setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
        context.focusIn();
        instance.setCurrentInputMethod(&context, entry->uniqueName(), true);
        for (const auto key : keys) {
            fcitx::KeyEvent event(&context,
                                  fcitx::Key(static_cast<fcitx::KeySym>(key)));
            context.keyEvent(event);
            if (!event.accepted()) {
                throw std::runtime_error(
                    "Input method did not consume the key");
            }
        }
        const auto candidates = context.inputPanel().candidateList();
        if (!candidates || candidates->empty()) {
            throw std::runtime_error("Input method produced no candidates");
        }
        const auto expected = candidates->candidate(0).text().toString();
        candidates->candidate(0).select(&context);
        if (expected.empty() || context.commit != expected) {
            throw std::runtime_error(
                "Candidate selection did not commit its text");
        }
        context.reset();
        context.focusOut();
        std::cout << "PASS: " << argv[3]
                  << " discovered, DLL loaded, configuration read, candidate "
                     "committed ("
                  << context.commit.size() << " UTF-8 bytes)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
