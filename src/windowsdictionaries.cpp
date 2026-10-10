#include "windowsdictionaries.h"
#include <fcitx-config/configuration.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx/addonmanager.h>

namespace fcitx::win32 {
namespace {
std::string utf8(const std::filesystem::path &path) {
    const auto text = path.lexically_normal().make_preferred().u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}
} // namespace

WindowsDictionaries::WindowsDictionaries(Instance &instance)
    : instance_(instance), directory_(StandardPaths::global().userDirectory(
                                          StandardPathsType::PkgData) /
                                      "pinyin/dictionaries") {
    if (scan(observed_)) {
        applied_ = observed_;
    }
    timer_ = instance_.eventLoop().addTimeEvent(
        CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 1000000, 0,
        [this](EventSourceTime *timer, uint64_t) {
            poll();
            timer->setNextInterval(1000000);
            timer->setOneShot();
            return true;
        });
}

bool WindowsDictionaries::scan(Files &files) {
    try {
        std::filesystem::create_directories(directory_);
        for (const auto &entry :
             std::filesystem::directory_iterator(directory_)) {
            const auto extension = entry.path().extension();
            if ((extension == ".dict" || extension == ".disable") &&
                entry.is_regular_file()) {
                files.emplace(entry.path(), Stamp{entry.file_size(),
                                                  entry.last_write_time()});
            }
        }
        error_.clear();
        return true;
    } catch (const std::filesystem::filesystem_error &error) {
        error_ = error.what();
        return false;
    }
}

void WindowsDictionaries::poll() {
    Files files;
    if (!scan(files)) {
        return;
    }
    // Wait for two identical scans so copies/replacements can finish first.
    if (files == observed_ && files != applied_) {
        if (auto *addon = instance_.addonManager().addon("pinyin", true)) {
            try {
                addon->setSubConfig("dictmanager", RawConfig());
                applied_ = files;
            } catch (const std::exception &error) {
                error_ = error.what();
            }
        }
    }
    observed_ = std::move(files);
}

ipc::DictionariesReply WindowsDictionaries::snapshot() const {
    ipc::DictionariesReply reply;
    reply.directory = utf8(directory_);
    reply.error = error_;
    auto *addon = instance_.addonManager().addon("pinyin", true);
    const auto *configuration =
        addon ? addon->getSubConfig("windows-dictionaries") : nullptr;
    if (!configuration) {
        return reply;
    }
    reply.available = true;
    RawConfig raw;
    configuration->save(raw);
    const auto paths = raw.get("Paths");
    const auto states = raw.get("States");
    if (!paths || !states) {
        return reply;
    }
    for (const auto &key : paths->subItems()) {
        const auto name = paths->get(key);
        const auto status = states->get(key);
        if (!name || !status) {
            continue;
        }
        const auto path = std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t *>(name->value().c_str())));
        const auto &state = status->value();
        const auto loaded = state == "Loaded"   ? ipc::DictionaryStatus::Loaded
                            : state == "Failed" ? ipc::DictionaryStatus::Failed
                            : state == "Disabled"
                                ? ipc::DictionaryStatus::Disabled
                                : ipc::DictionaryStatus::Loading;
        reply.dictionaries.push_back(
            {utf8(path.filename()), utf8(path), loaded});
    }
    if (reply.dictionaries.size() > ipc::kMaxDictionaries ||
        ipc::encodeDictionariesReply(reply).size() > ipc::kMaxPayloadSize) {
        reply.dictionaries.clear();
        reply.error = "Dictionary list exceeds the IPC limit";
    }
    return reply;
}

} // namespace fcitx::win32
