#pragma once

#include <fcitx/addonfactory.h>
#include <fcitx/inputmethodengine.h>

namespace fcitx::win32 {

class WindowsKeyboardEngine final : public InputMethodEngine {
  public:
    std::vector<InputMethodEntry> listInputMethods() override {
        std::vector<InputMethodEntry> entries;
        entries.emplace_back("keyboard-us", "Windows direct input", "en",
                             "windowskeyboard");
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
