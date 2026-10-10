#include "../tsf/pipeclient.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
namespace fs = std::filesystem;
using namespace fcitx::ipc;

void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

fs::path path(const std::string &text) {
    return fs::path(
        std::u8string(reinterpret_cast<const char8_t *>(text.c_str())));
}

bool samePath(const std::string &text, const fs::path &file) {
    const auto actual =
        path(text).lexically_normal().make_preferred().wstring();
    const auto expected = file.lexically_normal().make_preferred().wstring();
    return CompareStringOrdinal(actual.c_str(), -1, expected.c_str(), -1,
                                TRUE) == CSTR_EQUAL;
}

void wait(const std::function<bool()> &ready, const char *message) {
    for (unsigned i = 0; i < 100; ++i) {
        if (ready()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    throw std::runtime_error(message);
}

class Cleanup {
  public:
    Cleanup(fcitx::PipeClient &client, const fs::path &directory)
        : client_(client) {
        SettingsReply reply;
        require(client_.getSettings(reply), "cannot read original settings");
        original_ = reply.settings;
        const auto suffix = std::to_string(GetCurrentProcessId()) + "-" +
                            std::to_string(GetTickCount64());
        valid = directory / path("\u8bd7\u8bcd test-" + suffix + ".dict");
        renamed = directory / ("renamed-" + suffix + ".dict");
        broken = directory / ("broken-" + suffix + ".dict");
        ignored = directory / ("ignored-" + suffix + ".txt");
        subdirectory = directory / ("directory-" + suffix + ".dict");
        disabled = renamed;
        disabled += ".disable";
    }
    ~Cleanup() {
        for (const auto &file :
             {valid, renamed, broken, ignored, disabled, subdirectory}) {
            std::error_code error;
            fs::remove(file, error);
        }
        SettingsReply reply;
        if (client_.getSettings(reply)) {
            original_.revision = reply.settings.revision;
            client_.setSettings(original_, reply);
        }
    }
    fs::path valid, renamed, broken, ignored, subdirectory, disabled;

  private:
    fcitx::PipeClient &client_;
    PinyinSettings original_;
};

bool state(fcitx::PipeClient &client, const fs::path &file,
           DictionaryStatus expected) {
    DictionariesReply reply;
    require(client.getDictionaries(reply) && reply.available &&
                reply.error.empty(),
            "dictionary snapshot unavailable");
    return std::ranges::any_of(reply.dictionaries, [&](const auto &item) {
        return samePath(item.path, file) && item.status == expected;
    });
}

bool absent(fcitx::PipeClient &client, const fs::path &file) {
    DictionariesReply reply;
    require(client.getDictionaries(reply), "dictionary snapshot failed");
    return std::ranges::none_of(reply.dictionaries, [&](const auto &item) {
        return samePath(item.path, file);
    });
}

bool candidate(fcitx::PipeClient &client, uint64_t context,
               const std::string &input, const std::string &text) {
    KeyReply reply;
    require(client.reset(context, reply), "context reset failed");
    for (const auto letter : input) {
        require(client.key(context, false, letter, 0, 0, 0, reply) &&
                    reply.consumed,
                "pinyin input failed");
    }
    const bool found = std::ranges::any_of(
        reply.candidates, [&](const auto &item) { return item.text == text; });
    require(client.reset(context, reply), "context cleanup failed");
    return found;
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 3 || argc == 4,
                "usage: dictionaries_probe initial.dict replacement.dict "
                "[startup.dict]");
        fcitx::PipeClient configuration, input;
        require(configuration.connect() && input.connect(),
                "Core connection failed");
        DictionariesReply dictionaries;
        require(configuration.getDictionaries(dictionaries) &&
                    dictionaries.available && dictionaries.error.empty(),
                "dictionary query failed");
        const auto directory = path(dictionaries.directory);
        Cleanup cleanup(configuration, directory);
        SettingsReply settings;
        require(configuration.getSettings(settings), "settings query failed");
        auto requested = settings.settings;
        requested.scheme = PinyinScheme::Full;
        requested.profile = ShuangpinProfile::Xiaohe;
        require(configuration.setSettings(requested, settings) &&
                    settings.error == SettingsError::None,
                "full pinyin setup failed");
        uint64_t context = 0;
        require(input.createContext(1ULL << 1, context) &&
                    input.focusIn(context),
                "input context failed");
        KeyReply reply;
        require(input.setMode(context, true, reply), "Chinese mode failed");
        const std::string poetry = "\u591c\u6765\u98ce\u96e8\u58f0";
        const std::string replacement = "\u79cb\u6765\u98ce\u96e8\u7b19";
        if (argc == 4) {
            wait(
                [&] {
                    return state(configuration, path(argv[3]),
                                 DictionaryStatus::Loaded);
                },
                "startup dictionary was not loaded");
            require(candidate(input, context, "YELAIFENGYUSHENG", poetry),
                    "startup dictionary did not provide the poetry candidate");
            fs::remove(path(argv[3]));
            wait([&] { return absent(configuration, path(argv[3])); },
                 "startup dictionary removal was not detected");
        }
        const bool baseline =
            candidate(input, context, "YELAIFENGYUSHENG", poetry);
        const bool replacementBaseline =
            candidate(input, context, "QIULAIFENGYUSHENG", replacement);
        require(input.key(context, false, 'N', 0, 0, 0, reply) &&
                    input.key(context, false, 'I', 0, 0, 0, reply),
                "pending composition setup failed");
        fs::copy_file(path(argv[1]), cleanup.valid);
        std::ofstream(cleanup.broken, std::ios::binary) << "invalid dictionary";
        fs::copy_file(path(argv[1]), cleanup.ignored);
        fs::create_directory(cleanup.subdirectory);
        wait(
            [&] {
                return state(configuration, cleanup.valid,
                             DictionaryStatus::Loaded) &&
                       state(configuration, cleanup.broken,
                             DictionaryStatus::Failed);
            },
            "valid/broken dictionary states were not reported");
        require(absent(configuration, cleanup.ignored) &&
                    absent(configuration, cleanup.subdirectory),
                "non-dictionary file or directory was loaded");
        require(input.poll(context, reply) && reply.preedit == "ni" &&
                    reply.commit.empty(),
                "dictionary reload interrupted pending composition");
        for (const char letter : std::string("HAO")) {
            require(input.key(context, false, letter, 0, 0, 0, reply),
                    "composition continuation failed");
        }
        require(std::ranges::any_of(reply.candidates,
                                    [](const auto &item) {
                                        return item.text == "\u4f60\u597d";
                                    }),
                "dictionary reload broke existing input");
        require(candidate(input, context, "YELAIFENGYUSHENG", poetry),
                "full pinyin did not use third-party dictionary");
        requested = settings.settings;
        requested.scheme = PinyinScheme::Double;
        require(configuration.setSettings(requested, settings) &&
                    settings.error == SettingsError::None &&
                    candidate(input, context, "YELDFGYUUG", poetry),
                "Xiaohe did not use third-party dictionary");
        requested = settings.settings;
        requested.profile = ShuangpinProfile::Ziranma;
        require(configuration.setSettings(requested, settings) &&
                    settings.error == SettingsError::None &&
                    candidate(input, context, "YELLFGYUUG", poetry),
                "Ziranma did not use third-party dictionary");
        fs::rename(cleanup.valid, cleanup.renamed);
        wait(
            [&] {
                return absent(configuration, cleanup.valid) &&
                       state(configuration, cleanup.renamed,
                             DictionaryStatus::Loaded);
            },
            "dictionary rename was not detected");
        std::ofstream(cleanup.disabled) << "";
        wait(
            [&] {
                return state(configuration, cleanup.renamed,
                             DictionaryStatus::Disabled);
            },
            "upstream disable marker was ignored");
        fs::remove(cleanup.disabled);
        fs::copy_file(path(argv[2]), cleanup.renamed,
                      fs::copy_options::overwrite_existing);
        requested = settings.settings;
        requested.scheme = PinyinScheme::Full;
        require(configuration.setSettings(requested, settings) &&
                    settings.error == SettingsError::None,
                "full pinyin restore failed");
        wait(
            [&] {
                return state(configuration, cleanup.renamed,
                             DictionaryStatus::Loaded) &&
                       candidate(input, context, "QIULAIFENGYUSHENG",
                                 replacement) &&
                       candidate(input, context, "YELAIFENGYUSHENG", poetry) ==
                           baseline;
            },
            "dictionary replacement left stale entries");
        fs::remove(cleanup.renamed);
        wait([&] { return absent(configuration, cleanup.renamed); },
             "dictionary deletion was not detected");
        require(candidate(input, context, "QIULAIFENGYUSHENG", replacement) ==
                    replacementBaseline,
                "deleted dictionary still affected candidates");
        require(input.focusOut(context) && input.destroyContext(context),
                "context cleanup failed");
        std::cout << "PASS: startup and actual full/Xiaohe/Ziranma candidates, "
                     "live add/rename/replace/delete, "
                     "Unicode paths, invalid files, disable markers and "
                     "composition preservation\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
