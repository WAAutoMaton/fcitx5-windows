#pragma once

#include "../ipc/portablekey.h"
#include <bitset>
#include <string>
#include <windows.h>

namespace fcitx::settings {
using namespace win32::keys;

class KeyCapture {
  public:
    std::string press(UINT key, UINT scan, unsigned modifiers,
                      const std::string &unicode = {}) {
        if (key >= pressed_.size() || pressed_.test(key))
            return {};
        pressed_.set(key);
        // Keep the completed chord while its modifiers are being released.
        if (completed_ && modifierForKey(key))
            return {};
        completed_ = !modifierForKey(key);
        return portableKey(key, scan, modifiers, unicode);
    }
    void release(UINT key) {
        if (key < pressed_.size())
            pressed_.reset(key);
        if (pressed_.none())
            completed_ = false;
    }
    void reset() {
        pressed_.reset();
        completed_ = false;
    }

  private:
    std::bitset<256> pressed_;
    bool completed_ = false;
};
} // namespace fcitx::settings
