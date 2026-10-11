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
        next.enabled = {"keyboard-us", "pinyin", "shuangpin", "wbx"};
        require(client.setInputMethods(next, catalog) &&
                    catalog.error == SettingsError::None,
                "add failed");
        fcitx::PipeClient::KeyReply reply;
        require(client.selectInputMethod(context, "keyboard-us", reply) &&
                    !reply.enabled,
                "keyboard select failed");
        for (const auto &id : {"pinyin", "shuangpin", "wbx", "keyboard-us"}) {
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
