#pragma once

#include <fcitx/addonfactory.h>
#include <fcitx/inputmethodengine.h>

namespace fcitx::win32 {

class WindowsKeyboardEngine final : public InputMethodEngine {
  public:
    std::vector<InputMethodEntry> listInputMethods() override {
        std::vector<InputMethodEntry> entries;
        entries.emplace_back("keyboard-us", "English keyboard", "en",
                             "windowskeyboard");
        entries.back().setLabel("A").setIcon("input-keyboard");
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
