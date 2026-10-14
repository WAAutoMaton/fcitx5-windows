#pragma once
#include "translator.h"
#include <functional>
#include <mutex>
#include <windows.h>

namespace fcitx::win32::i18n {
class TranslationMonitor {
  public:
    explicit TranslationMonitor(std::filesystem::path settings = {},
                                std::filesystem::path resources = {});
    Translation poll();
    void reset();
    void setLanguageReader(std::function<bool(ipc::UiLanguage &)> reader);
    Translation snapshot() const { return latest_; }

  private:
    struct State {
        std::mutex mutex;
        bool running = false;
        bool ready = false;
        Translation result;
    };
    struct Task {
        std::shared_ptr<State> state;
        Translation previous;
        HMODULE module;
        std::filesystem::path settings;
        std::filesystem::path resources;
        std::function<bool(ipc::UiLanguage &)> reader;
    };
    static DWORD WINAPI run(void *argument);
    std::shared_ptr<State> state_;
    Translation latest_;
    ULONGLONG nextCheck_ = 0;
    std::filesystem::path settings_;
    std::filesystem::path resources_;
    std::function<bool(ipc::UiLanguage &)> reader_;
};
} // namespace fcitx::win32::i18n
