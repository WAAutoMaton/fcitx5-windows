#include "../tsf/pipeclient.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool result, const char *message) {
    if (!result) {
        throw std::runtime_error(message);
    }
}

void checkHello(fcitx::PipeClient &client, uint64_t context,
                const char *input) {
    fcitx::PipeClient::KeyReply reply;
    for (; *input; ++input) {
        require(client.key(context, false, *input, 0, 0, 0, reply) &&
                    reply.consumed && reply.enabled,
                "scheme did not consume Chinese input");
    }
    const std::string hello = "\xe4\xbd\xa0\xe5\xa5\xbd";
    const auto candidate =
        std::find_if(reply.candidates.begin(), reply.candidates.end(),
                     [&](const auto &item) { return item.text == hello; });
    require(candidate != reply.candidates.end(),
            "scheme did not produce the real nihao candidate");
    const auto index = candidate - reply.candidates.begin();
    require(index < 9 &&
                client.key(context, false, '1' + index, 0, 0, 0, reply) &&
                reply.commit == hello,
            "scheme did not commit real Chinese text");
}

class Restore {
  public:
    explicit Restore(fcitx::PipeClient &client) : client_(client) {
        fcitx::ipc::SettingsReply reply;
        require(client_.getSettings(reply), "cannot read original settings");
        original_ = reply.settings;
    }
    ~Restore() {
        fcitx::ipc::SettingsReply reply;
        if (client_.getSettings(reply)) {
            original_.revision = reply.settings.revision;
            client_.setSettings(original_, reply);
        }
    }

  private:
    fcitx::PipeClient &client_;
    fcitx::ipc::PinyinSettings original_;
};
} // namespace

int main(int argc, char **argv) {
    using namespace fcitx::ipc;
    try {
        fcitx::PipeClient configuration;
        require(configuration.connect(), "Core connection failed");
        if (argc == 2 && std::string(argv[1]) == "--open-settings") {
            SettingsReply opened;
            require(configuration.openSettings(opened) &&
                        opened.error == SettingsError::None,
                    "Core could not launch the deployed settings application");
            std::cout << "PASS: Core OpenSettings launched the deployed "
                         "application\n";
            return 0;
        }
        Restore restore(configuration);
        SettingsReply settings;
        require(configuration.getSettings(settings) &&
                    settings.pinyinAvailable && settings.shuangpinAvailable,
                "pinyin/shuangpin unavailable");
        auto requested = settings.settings;
        requested.scheme = PinyinScheme::Full;
        requested.profile = ShuangpinProfile::Xiaohe;
        require(configuration.setSettings(requested, settings) &&
                    settings.error == SettingsError::None,
                "full pinyin configuration failed");
        fcitx::PipeClient input, english;
        uint64_t context = 0, englishContext = 0;
        require(input.connect() && input.createContext(1ULL << 1, context) &&
                    input.focusIn(context) && english.connect() &&
                    english.createContext(1ULL << 1, englishContext) &&
                    english.focusIn(englishContext),
                "input contexts failed");
        fcitx::PipeClient::KeyReply reply;
        require(english.setMode(englishContext, false, reply),
                "English mode failed");
        checkHello(input, context, "NIHAO");
        require(input.key(context, false, 'N', 0, 0, 0, reply) &&
                    !reply.preedit.empty(),
                "preedit setup failed");
        if (argc == 3 && std::string(argv[1]) == "--read-only-file") {
            const auto path = std::filesystem::path(
                std::u8string(reinterpret_cast<const char8_t *>(argv[2])));
            const auto attributes = GetFileAttributesW(path.c_str());
            require(attributes != INVALID_FILE_ATTRIBUTES &&
                        SetFileAttributesW(
                            path.c_str(), attributes | FILE_ATTRIBUTE_READONLY),
                    "read-only settings setup failed");
            auto rejected = settings.settings;
            rejected.profile = ShuangpinProfile::Ziranma;
            const bool failed = configuration.setSettings(rejected, settings) &&
                                settings.error == SettingsError::SaveFailed;
            SetFileAttributesW(path.c_str(), attributes);
            require(failed, "storage failure reported success");
            require(input.key(context, false, 'I', 0, 0, 0, reply) &&
                        reply.preedit == "ni",
                    "storage failure cleared the decoder's preedit");
            require(input.reset(context, reply),
                    "reset after storage failure failed");
        }
        const auto oldRevision = settings.settings.revision;
        requested = settings.settings;
        requested.scheme = PinyinScheme::Double;
        require(configuration.setSettings(requested, settings) &&
                    settings.error == SettingsError::None &&
                    settings.settings.revision > oldRevision,
                "Xiaohe switch failed");
        require(input.poll(context, reply) && reply.enabled &&
                    reply.preedit.empty() && reply.commit.empty() &&
                    reply.settingsRevision == settings.settings.revision,
                "scheme change did not cancel old preedit");
        require(english.poll(englishContext, reply) && !reply.enabled,
                "scheme change enabled an English context");
        require(input.setMode(context, true, reply) && reply.enabled,
                "double pinyin mode failed");
        checkHello(input, context, "NIHC");
        requested = settings.settings;
        requested.profile = ShuangpinProfile::Ziranma;
        require(configuration.setSettings(requested, settings) &&
                    settings.error == SettingsError::None,
                "Ziranma switch failed");
        checkHello(input, context, "NIHK");
        requested.profile = ShuangpinProfile::MS;
        require(configuration.setSettings(requested, settings) &&
                    settings.error == SettingsError::Conflict,
                "stale settings overwrite allowed");
        requested = settings.settings;
        requested.profile = ShuangpinProfile::GB;
        require(configuration.setSettings(requested, settings) &&
                    settings.error == SettingsError::None &&
                    settings.settings.profile == ShuangpinProfile::GB,
                "GB Standard mapping failed");
        requested = settings.settings;
        requested.profile = static_cast<ShuangpinProfile>(255);
        require(configuration.setSettings(requested, settings) &&
                    settings.error == SettingsError::Invalid,
                "invalid profile accepted");
        requested = settings.settings;
        requested.scheme = PinyinScheme::Full;
        require(configuration.setSettings(requested, settings) &&
                    settings.error == SettingsError::None,
                "switch back to full pinyin failed");
        checkHello(input, context, "NIHAO");
        const auto revision = settings.settings.revision;
        require(configuration.setSettings(settings.settings, settings) &&
                    settings.error == SettingsError::None &&
                    settings.settings.revision == revision,
                "same settings are not idempotent");
        require(english.setMode(englishContext, true, reply) && reply.enabled,
                "English context could not activate new scheme");
        checkHello(english, englishContext, "NIHAO");
        input.destroyContext(context);
        english.destroyContext(englishContext);
        std::cout << "PASS: real full/Xiaohe/Ziranma Chinese commits, global "
                     "settings, modes, revisions and GB mapping\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
