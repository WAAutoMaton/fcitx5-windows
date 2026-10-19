#include "../tsf/pipeclient.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
const fcitx::ipc::ConfigOption &
option(const fcitx::ipc::MethodConfigReply &config, const std::string &path) {
    auto found =
        std::find_if(config.options.begin(), config.options.end(),
                     [&](const auto &item) { return item.path == path; });
    require(found != config.options.end(), "missing engine config option");
    return *found;
}
void candidate(fcitx::PipeClient &client, uint64_t context, const char *keys,
               const std::string &text) {
    fcitx::PipeClient::KeyReply reply;
    require(client.reset(context, reply), "reset failed");
    for (const char *key = keys; *key; ++key) {
        require(client.key(context, false, *key, 0, 0, 0, reply) &&
                    reply.consumed,
                "engine rejected input");
    }
    if (reply.commit == text) {
        return;
    }
    auto found =
        std::find_if(reply.candidates.begin(), reply.candidates.end(),
                     [&](const auto &item) { return item.text == text; });
    require(found != reply.candidates.end(), "real engine candidate missing");
    const auto index = found - reply.candidates.begin();
    require(index < 9 &&
                client.key(context, false, '1' + static_cast<uint32_t>(index),
                           0, 0, 0, reply) &&
                reply.commit == text,
            "real engine commit missing");
}
} // namespace

int main(int argc, char **argv) {
    using namespace fcitx::ipc;
    try {
        fcitx::PipeClient client;
        InputMethodsReply catalog;
        require(client.connect() && client.getInputMethods(catalog),
                "catalog unavailable");
        if (argc == 3 && std::string(argv[1]) == "--localization") {
            const std::string language = argv[2];
            GlobalSettingsReply global;
            require(client.getGlobalSettings(global) &&
                        global.error == SettingsError::None,
                    "language retrieval failed");
            const auto expected =
                language == "zh_CN"   ? UiLanguage::SimplifiedChinese
                : language == "zh_TW" ? UiLanguage::TraditionalChinese
                                      : UiLanguage::English;
            require((language == "zh_CN" || language == "zh_TW" ||
                     language == "en_US") &&
                        global.coreActiveLanguage == expected,
                    "unexpected active language");
            const struct {
                const char *method;
                const char *path;
                const char *english;
                const char *simplified;
                const char *traditional;
            } labels[] = {
                {"pinyin", "CurrentCandidate", "Select Current Candidate",
                 "\u9009\u62e9\u5f53\u524d\u5019\u9009\u8bcd",
                 "\u9078\u64c7\u76ee\u524d\u5019\u9078\u8a5e"},
                {"shuangpin", "CommitRawInput", "Commit Raw Input",
                 "\u63d0\u4ea4\u539f\u59cb\u5b57\u7b26\u4e32",
                 "\u63d0\u4ea4\u539f\u59cb\u5b57\u4e32"},
                {"wbx", "DefaultCandidate", "Select Current Candidate",
                 "\u9009\u62e9\u5f53\u524d\u5019\u9009\u8bcd",
                 "\u9078\u64c7\u76ee\u524d\u5019\u9078\u8a5e"},
                {"wbx", "SecondCandidate", "Select Second Candidate",
                 "\u9009\u62e9\u7b2c\u4e8c\u4e2a\u5019\u9009\u8bcd",
                 "\u9078\u64c7\u7b2c\u4e8c\u500b\u5019\u9078\u8a5e"}};
            for (const auto &label : labels) {
                MethodConfigReply config;
                require(client.getMethodConfig(label.method, config) &&
                            config.error == SettingsError::None,
                        "localized config retrieval failed");
                const auto &actual = option(config, label.path).label;
                std::cout << label.method << "/" << label.path << "=" << actual
                          << "\n";
                require(actual == (expected == UiLanguage::SimplifiedChinese
                                       ? label.simplified
                                   : expected == UiLanguage::TraditionalChinese
                                       ? label.traditional
                                       : label.english),
                        "engine config translation missing");
            }
            std::cout
                << "PASS: localized Pinyin/Shuangpin/table descriptions\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--inspect") {
            for (const auto &method : catalog.methods) {
                std::cout << method.id << " panel=" << method.panel
                          << " available=" << method.available << "\n";
            }
            for (const auto &id : catalog.selection.enabled) {
                std::cout << "enabled=" << id << "\n";
            }
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--persistence-write") {
            auto next = catalog.selection;
            next.enabled = {"wbx", "shuangpin", "keyboard-us"};
            require(client.setInputMethods(next, catalog) &&
                        catalog.error == SettingsError::None,
                    "persist list failed");
            MethodConfigReply config;
            require(client.getMethodConfig("shuangpin", config) &&
                        client.setMethodConfig({"shuangpin",
                                                config.revision,
                                                {{"ShuangpinProfile", "Xiaohe"},
                                                 {"Fuzzy/IN_ING", "True"},
                                                 {"PageSize", "7"}}},
                                               config) &&
                        config.error == SettingsError::None,
                    "persist pinyin failed");
            require(client.getMethodConfig("wbx", config) &&
                        client.setMethodConfig(
                            {"wbx", config.revision, {{"PageSize", "6"}}},
                            config) &&
                        config.error == SettingsError::None,
                    "persist table failed");
            GlobalSettingsReply global;
            require(client.getGlobalSettings(global),
                    "shortcut persistence read failed");
            auto custom = global.settings;
            custom.shortcuts[0] = "Control+Shift+J";
            custom.shortcuts[5].clear();
            require(client.setGlobalSettings(custom, global) &&
                        global.error == SettingsError::None,
                    "shortcut persistence write failed");
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--persistence-read") {
            require(
                catalog.selection.enabled ==
                    std::vector<std::string>{"wbx", "shuangpin", "keyboard-us"},
                "restart lost method order");
            MethodConfigReply config;
            require(client.getMethodConfig("pinyin", config) &&
                        option(config, "Fuzzy/IN_ING").value == "True" &&
                        option(config, "PageSize").value == "7",
                    "restart lost fuzzy config");
            require(client.getMethodConfig("shuangpin", config) &&
                        option(config, "ShuangpinProfile").value == "Xiaohe",
                    "restart lost profile");
            require(client.getMethodConfig("wbx", config) &&
                        option(config, "PageSize").value == "6",
                    "restart lost table config");
            GlobalSettingsReply global;
            require(client.getGlobalSettings(global) &&
                        global.settings.shortcuts[0] == "Control+Shift+J" &&
                        global.settings.shortcuts[5].empty(),
                    "restart lost shortcuts");
            auto defaults = global.settings;
            defaults.shortcuts = defaultShortcuts();
            require(client.setGlobalSettings(defaults, global) &&
                        global.error == SettingsError::None,
                    "persisted shortcuts restore failed");
            std::cout << "PASS: restart preserves list order, shared pinyin "
                         "options/profile and table options\n";
            return 0;
        }
        require(catalog.selection.enabled ==
                    std::vector<std::string>{"keyboard-us", "pinyin"},
                "unexpected defaults");
        for (const auto &id :
             {"keyboard-us", "pinyin", "shuangpin", "wbx", "wbpy", "cangjie",
              "db", "erbi", "qxm", "wanfeng", "zrm"}) {
            auto found =
                std::find_if(catalog.methods.begin(), catalog.methods.end(),
                             [&](const auto &item) {
                                 return item.id == id && item.available;
                             });
            require(found != catalog.methods.end(),
                    "Chinese addon method unavailable");
        }
        MethodConfigReply pinyin, shuangpin, wubi;
        require(client.getMethodConfig("pinyin", pinyin) &&
                    client.getMethodConfig("shuangpin", shuangpin) &&
                    client.getMethodConfig("wbx", wubi),
                "config retrieval failed");
        require(pinyin.panel == shuangpin.panel && wubi.panel == "table",
                "shared panel declaration failed");
        const auto originalFuzzy = option(pinyin, "Fuzzy/IN_ING").value;
        const auto originalProfile =
            option(shuangpin, "ShuangpinProfile").value;
        const auto originalPageSize = option(wubi, "PageSize").value;
        uint64_t context = 0;
        require(client.createContext(1ULL << 1, context) &&
                    client.focusIn(context),
                "context setup failed");
        auto next = catalog.selection;
        next.enabled = {"keyboard-us", "pinyin", "shuangpin", "wbx", "wbpy"};
        require(client.setInputMethods(next, catalog) &&
                    catalog.error == SettingsError::None,
                "add failed");
        fcitx::PipeClient::KeyReply reply;
        require(client.selectInputMethod(context, "keyboard-us", reply) &&
                    !reply.enabled,
                "keyboard select failed");
        for (const auto &id :
             {"pinyin", "shuangpin", "wbx", "wbpy", "keyboard-us"}) {
            require(client.key(context, false, VK_SPACE, 0, 2, 0, reply) &&
                        reply.consumed && reply.inputMethod == id,
                    "Ctrl+Space order failed");
            const auto previous = reply.inputMethod;
            require(client.key(context, false, VK_SPACE, 0, 2 | 32, 0, reply) &&
                        reply.inputMethod == previous,
                    "repeat cycled input method");
            require(client.key(context, true, VK_SPACE, 0, 2, 0, reply) &&
                        reply.inputMethod == previous,
                    "release cycled input method");
        }
        GlobalSettingsReply global;
        require(client.getGlobalSettings(global),
                "global shortcuts unavailable");
        const auto originalShortcuts = global.settings.shortcuts;
        for (unsigned i = 1; i <= 5; ++i) {
            require(client.key(context, false, '0' + i, 0, 6, 0, reply),
                    "quick switch request failed");
            require(reply.consumed, "available quick switch was not consumed");
            require(reply.inputMethod == catalog.selection.enabled[i - 1],
                    "quick switch ignored method order");
            const auto selected = reply.inputMethod;
            require(client.key(context, false, '0' + i, 0, 6 | 32, 0, reply) &&
                        reply.inputMethod == selected,
                    "quick switch repeat changed selection");
            require(client.key(context, true, '0' + i, 0, 0, 0, reply) &&
                        reply.consumed && reply.inputMethod == selected,
                    "quick switch release lost consumption");
        }
        auto custom = global.settings;
        custom.shortcuts[0] = "Control+Shift+J";
        custom.shortcuts[1] = "Alt+<59>";
        custom.shortcuts[5].clear();
        require(client.setGlobalSettings(custom, global) &&
                    global.error == SettingsError::None,
                "custom shortcuts save failed");
        const auto beforeShortcut = reply.inputMethod;
        require(client.key(context, false, VK_SPACE, 0x39, 2, 0, reply) &&
                    !reply.consumed && reply.inputMethod == beforeShortcut,
                "old Ctrl+Space binding remained active");
        require(client.key(context, false, 'J', 0x24, 3, 0, reply) &&
                    reply.consumed && reply.inputMethod != beforeShortcut,
                "custom cycle shortcut did not apply immediately");
        require(client.key(context, true, 'J', 0x24, 0, 0, reply) &&
                    reply.consumed,
                "custom cycle release failed");
        require(client.key(context, false, VK_F1, 0x3b, 4, 0, reply) &&
                    reply.consumed && reply.inputMethod == "keyboard-us",
                "recorded scan-code shortcut failed");
        require(client.key(context, true, VK_F1, 0x3b, 0, 0, reply),
                "recorded scan-code release failed");
        custom = global.settings;
        custom.shortcuts = originalShortcuts;
        require(client.setGlobalSettings(custom, global) &&
                    global.error == SettingsError::None,
                "restore shortcuts failed");
        require(client.selectInputMethod(context, "pinyin", reply),
                "pinyin select failed");
        candidate(client, context, "NIHAO", "\xe4\xbd\xa0\xe5\xa5\xbd");
        require(client.key(context, false, 'N', 0, 0, 0, reply) &&
                    !reply.preedit.empty(),
                "preedit setup failed");
        require(client.cycleInputMethod(context, reply) &&
                    reply.inputMethod == "shuangpin" && reply.preedit.empty() &&
                    reply.commit.empty(),
                "cycle leaked old composition");
        require(client.getMethodConfig("shuangpin", shuangpin),
                "profile config failed");
        require(client.setMethodConfig({"shuangpin",
                                        shuangpin.revision,
                                        {{"ShuangpinProfile", "Xiaohe"}}},
                                       shuangpin) &&
                    shuangpin.error == SettingsError::None,
                "profile apply failed");
        candidate(client, context, "NIHC", "\xe4\xbd\xa0\xe5\xa5\xbd");
        require(client.getMethodConfig("pinyin", pinyin),
                "pinyin config failed");
        const auto changedFuzzy = originalFuzzy == "True" ? "False" : "True";
        auto stale = pinyin.revision;
        require(
            client.setMethodConfig(
                {"pinyin", stale, {{"Fuzzy/IN_ING", changedFuzzy}}}, pinyin) &&
                pinyin.error == SettingsError::None,
            "fuzzy apply failed");
        require(client.getMethodConfig("shuangpin", shuangpin) &&
                    option(shuangpin, "Fuzzy/IN_ING").value == changedFuzzy,
                "shared pinyin config diverged");
        require(
            client.setMethodConfig(
                {"pinyin", stale, {{"Fuzzy/IN_ING", originalFuzzy}}}, pinyin) &&
                pinyin.error == SettingsError::Conflict,
            "stale config accepted");
        require(
            client.setMethodConfig(
                {"pinyin", pinyin.revision, {{"PageSize", "999"}}}, pinyin) &&
                pinyin.error == SettingsError::Invalid,
            "invalid config accepted");
        require(client.selectInputMethod(context, "wbx", reply) &&
                    reply.enabled && reply.iconLabel != "\xe6\x8b\xbc",
                "wubi select/icon failed");
        candidate(client, context, "WQ", "\xe4\xbd\xa0");
        require(client.getMethodConfig("wbx", wubi), "wubi config failed");
        require(client.setMethodConfig(
                    {"wbx", wubi.revision, {{"PageSize", "6"}}}, wubi) &&
                    wubi.error == SettingsError::None &&
                    option(wubi, "PageSize").value == "6",
                "table config apply failed");
        require(client.getInputMethods(catalog), "catalog refresh failed");
        next = catalog.selection;
        next.enabled = {"wbx"};
        require(client.setInputMethods(next, catalog) &&
                    catalog.error == SettingsError::None,
                "remove keyboard failed");
        require(client.setMode(context, false, reply) &&
                    reply.inputMethod == "wbx",
                "compartment enabled an unselected keyboard");
        auto empty = catalog.selection;
        empty.enabled.clear();
        require(client.setInputMethods(empty, catalog) &&
                    catalog.error == SettingsError::LastInputMethod &&
                    catalog.selection.enabled ==
                        std::vector<std::string>{"wbx"},
                "last method deletion accepted");
        next = catalog.selection;
        next.enabled = {"pinyin", "keyboard-us", "wbx"};
        require(client.setInputMethods(next, catalog) &&
                    catalog.error == SettingsError::None,
                "reorder failed");
        require(client.cycleInputMethod(context, reply) &&
                    reply.inputMethod == "pinyin",
                "reordered wrap failed");
        require(client.key(context, false, '2', 0, 6, 0, reply) &&
                    reply.consumed && reply.inputMethod == "keyboard-us",
                "quick switch did not follow reordered list");
        require(client.key(context, true, '2', 0, 0, 0, reply) &&
                    reply.consumed,
                "reordered quick switch release failed");
        require(client.key(context, false, '5', 0, 6, 0, reply) &&
                    !reply.consumed && reply.inputMethod == "keyboard-us",
                "missing quick switch was consumed");
        next = catalog.selection;
        next.enabled = {"keyboard-us"};
        require(client.setInputMethods(next, catalog) &&
                    catalog.error == SettingsError::None &&
                    client.poll(context, reply) &&
                    reply.inputMethod == "keyboard-us" && !reply.enabled &&
                    reply.preedit.empty(),
                "removed active method not reconciled");
        require(client.cycleInputMethod(context, reply) &&
                    reply.inputMethod == "keyboard-us",
                "single entry cycle failed");
        require(client.getMethodConfig("pinyin", pinyin) &&
                    client.setMethodConfig({"pinyin",
                                            pinyin.revision,
                                            {{"Fuzzy/IN_ING", originalFuzzy}}},
                                           pinyin) &&
                    pinyin.error == SettingsError::None,
                "fuzzy restore failed");
        require(client.getMethodConfig("shuangpin", shuangpin) &&
                    client.setMethodConfig(
                        {"shuangpin",
                         shuangpin.revision,
                         {{"ShuangpinProfile", originalProfile}}},
                        shuangpin) &&
                    shuangpin.error == SettingsError::None,
                "profile restore failed");
        require(
            client.getMethodConfig("wbx", wubi) &&
                client.setMethodConfig(
                    {"wbx", wubi.revision, {{"PageSize", originalPageSize}}},
                    wubi) &&
                wubi.error == SettingsError::None,
            "table restore failed");
        require(client.getInputMethods(catalog), "catalog restore read failed");
        next = catalog.selection;
        next.enabled = {"keyboard-us", "pinyin"};
        require(client.setInputMethods(next, catalog) &&
                    catalog.error == SettingsError::None,
                "default list restore failed");
        client.destroyContext(context);
        std::cout << "PASS: catalog, shared schemas/config, real "
                     "Pinyin/Shuangpin/Wubi commits, Ctrl+Space order/repeat, "
                     "deletion guard, revisions and active method removal\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
