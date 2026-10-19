#include "../settings/keycapture.h"
#include <cassert>
#ifdef FCITX5_KEYCAPTURE_CORE_CHECK
#include <fcitx-utils/key.h>
#endif

int main() {
    using namespace fcitx::settings;
    assert(portableKey(VK_SPACE, 0x39, Control) == "Control+space");
    assert(portableKey('A', 0x1e, Control) == "Control+A");
    assert(portableKey('A', 0x1e, Control | Shift) == "Control+Shift+A");
    assert(portableKey('A', 0x1e, Shift, "A") == "A");
    assert(portableKey('A', 0x1e, Caps) == "A");
    assert(portableKey('A', 0x1e, Caps | Shift, "a") == "a");
    assert(portableKey(VK_OEM_COMMA, 0x33, Shift, "<") == "less");
    assert(portableKey(VK_OEM_PLUS, 0x0d, Shift, "+") == "plus");
    assert(portableKey(VK_OEM_COMMA, 0x33, Control | Shift) ==
           "Control+Shift+<51>");
    assert(portableKey(VK_F1, 0x3b, Alt) == "Alt+<59>");
    assert(portableKey(VK_NUMPAD1, 0x4f, Control) == "Control+<79>");
    assert(portableKey(VK_NUMPAD1, 0x4f, 0, "1") == "1");
    assert(portableKey(VK_SHIFT, 0x36, Control | Shift) == "Control+Shift_R");
    assert(portableKey(VK_SHIFT, 0x2a, Shift) == "Shift_L");
    assert(portableKey(VK_CONTROL, 0x11d, Control) == "Control_R");
    assert(portableKey(VK_MENU, 0x138, Alt | Shift) == "Shift+Alt_R");
    assert(portableKey(VK_RWIN, 0x15c, Super) == "Super_R");
    assert(portableKey('1', 0x02, Control | Shift) == "Control+Shift+1");
    assert(portableKey(VK_TAB, 0x0f, Shift) == "Shift+Tab");
    assert(portableKey(VK_ESCAPE, 1, 0) == "Escape");
    assert(portableKey(0, 0, 0).empty());

#ifdef FCITX5_KEYCAPTURE_CORE_CHECK
    // Core compares reserialized options to the submitted value exactly.
    for (unsigned c = 0x20; c < 0x7f; ++c) {
        for (unsigned modifiers :
             {0u, unsigned(Shift), unsigned(Control | Shift)}) {
            const std::string unicode(1, static_cast<char>(c));
            fcitx::KeyStates states;
            if (modifiers & Shift)
                states |= fcitx::KeyState::Shift;
            if (modifiers & Control)
                states |= fcitx::KeyState::Ctrl;
            const auto expected =
                fcitx::Key(fcitx::Key::keySymFromUnicode(c), states)
                    .normalize()
                    .toString();
            const auto recorded =
                portableKey(VK_OEM_1, 0x27, modifiers, unicode);
            assert(recorded == expected);
            assert(fcitx::Key(recorded).toString() == recorded);
        }
    }
    for (const auto &value : {portableKey(VK_F1, 0x3b, Alt),
                              portableKey(VK_CONTROL, 0x11d, Control),
                              portableKey(VK_SHIFT, 0x36, Control | Shift)}) {
        assert(fcitx::Key(value).isValid());
        assert(fcitx::Key(value).toString() == value);
    }
#endif

    KeyCapture capture;
    assert(capture.press(VK_CONTROL, 0x1d, Control) == "Control_L");
    assert(capture.press(VK_SHIFT, 0x36, Control | Shift) == "Control+Shift_R");
    assert(capture.press('A', 0x1e, Control | Shift) == "Control+Shift+A");
    assert(capture.press('A', 0x1e, Control | Shift).empty());
    capture.release('A');
    // Releasing modifiers leaves the last full chord intact.
    capture.release(VK_SHIFT);
    assert(capture.press(VK_SHIFT, 0x36, Control | Shift).empty());
    capture.release(VK_SHIFT);
    capture.release(VK_CONTROL);
    assert(capture.press(VK_SHIFT, 0x2a, Shift) == "Shift_L");
    capture.reset();
    assert(capture.press(VK_TAB, 0x0f, 0) == "Tab");
    capture.release(VK_TAB);
    assert(capture.press(VK_RETURN, 0x1c, 0) == "Return");
}
