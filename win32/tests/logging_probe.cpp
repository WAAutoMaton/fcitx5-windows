#include "../tsf/pipeclient.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **argv) {
    try {
        using namespace fcitx;
        if (argc != 2)
            return 2;
        const std::string_view action(argv[1]);
        if (action == "--check-ui-test") {
#ifdef FCITX5_UI_TEST
            return 0;
#else
            return 1;
#endif
        }
        require(action == "--enable" || action == "--disable" ||
                    action == "--check-enabled",
                "invalid logging action");
        PipeClient client;
        ipc::GlobalSettingsReply global;
        require(client.connect() && client.getGlobalSettings(global) &&
                    global.error == ipc::SettingsError::None,
                "global settings unavailable");
        if (action != "--check-enabled") {
            auto next = global.settings;
            const auto old = next;
            next.detailedLogging = action == "--enable";
            require(client.setGlobalSettings(next, global) &&
                        global.error == ipc::SettingsError::None &&
                        global.settings.detailedLogging == next.detailedLogging,
                    "logging setting was not applied");
            if (old.detailedLogging != next.detailedLogging) {
                require(global.settings.revision == old.revision + 1,
                        "logging revision did not advance");
                auto stale = old;
                stale.language = old.language == ipc::UiLanguage::English
                                     ? ipc::UiLanguage::System
                                     : ipc::UiLanguage::English;
                ipc::GlobalSettingsReply conflict;
                require(client.setGlobalSettings(stale, conflict) &&
                            conflict.error == ipc::SettingsError::Conflict &&
                            conflict.settings == global.settings,
                        "stale language draft overwrote the logging setting");
            }
        }
        const bool enabled = action != "--disable";
        require(global.settings.detailedLogging == enabled,
                "logging state did not survive restart");
        uint64_t context = 0;
        require(client.createContext(1ULL << 1, context) &&
                    client.focusIn(context),
                "context unavailable");
        PipeClient::KeyReply reply;
        const auto synchronized = [&] {
            require(reply.detailedLogging == enabled,
                    "KeyReply logging state is stale");
        };
        require(client.poll(context, reply), "poll failed");
        synchronized();
        require(client.reset(context, reply), "reset failed");
        synchronized();
        require(client.setMode(context, false, reply), "keyboard mode failed");
        synchronized();
        require(client.setMode(context, true, reply), "Chinese mode failed");
        synchronized();
        require(client.cycleInputMethod(context, reply), "cycle failed");
        synchronized();
        require(client.selectInputMethod(context, "pinyin", reply),
                "Pinyin selection failed");
        synchronized();
        for (const auto key : std::string_view("NIHAO")) {
            require(client.key(context, false, key, 0, 0, 0, reply),
                    "key down failed");
            synchronized();
            require(client.key(context, true, key, 0, 0, 0, reply),
                    "key up failed");
            synchronized();
        }
        require(client.key(context, false, VK_SPACE, 0, 0, 0, reply) &&
                    reply.commit == "\xe4\xbd\xa0\xe5\xa5\xbd",
                "real Pinyin commit failed");
        synchronized();
        client.disconnect();
        require(client.connect() && client.getGlobalSettings(global) &&
                    global.settings.detailedLogging == enabled,
                "reconnect changed logging state");
        std::cout << "PASS: logging setting, revision conflict, KeyReply sync, "
                     "real Pinyin and reconnect\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
