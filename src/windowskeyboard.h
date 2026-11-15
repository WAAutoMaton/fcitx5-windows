#pragma once

#include <fcitx/addonfactory.h>
#include <fcitx/inputmethodengine.h>

namespace fcitx::win32 {

class WindowsKeyboardEngine final : public InputMethodEngine {
  public:
    std::vector<InputMethodEntry> listInputMethods() override {
        std::vector<InputMethodEntry> entries;
        // Keep the keyboard catalog available on Windows even when no XKB
        // data is installed.  Every entry is a pass-through physical
        // keyboard source, while its language metadata lets Settings present
        // the same language-first picker as the macOS frontend.
        constexpr struct {
            const char *id;
            const char *name;
            const char *language;
            const char *label;
        } layouts[] = {
            {"keyboard-us", "English keyboard", "en", "A"},
            {"keyboard-gb", "British keyboard", "en", "GB"},
            {"keyboard-fr", "French keyboard", "fr", "FR"},
            {"keyboard-de", "German keyboard", "de", "DE"},
            {"keyboard-es", "Spanish keyboard", "es", "ES"},
            {"keyboard-it", "Italian keyboard", "it", "IT"},
            {"keyboard-pt", "Portuguese keyboard", "pt", "PT"},
            {"keyboard-ru", "Russian keyboard", "ru", "RU"},
            {"keyboard-uk", "Ukrainian keyboard", "uk", "UK"},
            {"keyboard-ar", "Arabic keyboard", "ar", "AR"},
            {"keyboard-he", "Hebrew keyboard", "he", "HE"},
            {"keyboard-el", "Greek keyboard", "el", "EL"},
            {"keyboard-tr", "Turkish keyboard", "tr", "TR"},
            {"keyboard-vi", "Vietnamese keyboard", "vi", "VI"},
            {"keyboard-th", "Thai keyboard", "th", "TH"},
            {"keyboard-ja", "Japanese keyboard", "ja", "JP"},
            {"keyboard-ko", "Korean keyboard", "ko", "KO"},
            {"keyboard-zh", "Keyboard - Chinese", "zh", "中"},
            {"keyboard-zh-pinyin",
             "Keyboard - Chinese - Pinyin alphabet (with AltGr dead keys)",
             "zh_CN", "拼"},
            {"keyboard-zh-tw", "Keyboard - Chinese (Traditional)", "zh_TW",
             "繁"},
        };
        for (const auto &layout : layouts) {
            entries.emplace_back(layout.id, layout.name, layout.language,
                                 "windowskeyboard");
            entries.back().setLabel(layout.label).setIcon("input-keyboard");
        }
        return entries;
    }

    void keyEvent(const InputMethodEntry &, KeyEvent &) override {}
};

class WindowsKeyboardFactory final : public AddonFactory {
  public:
    AddonInstance *create(AddonManager *) override {
        return new WindowsKeyboardEngine;
    }
};

} // namespace fcitx::win32
