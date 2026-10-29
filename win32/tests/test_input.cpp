#include "../ipc/shortcuts.h"
#include "../tsf/keypolicy.h"
#include "../tsf/textutil.h"
#include <cassert>

int main() {
    using namespace fcitx;
    namespace ipc = fcitx::win32::ipc;
    assert(routesKey('N', 0, true, false));
    assert(!routesKey('N', 0, false, false));
    assert(!routesKey('C', kModifierControl, true, true));
    assert(!routesKey('A', kModifierAlt, true, true));
    auto shortcuts = ipc::defaultShortcuts();
    assert(ipc::validShortcuts(shortcuts));
    ipc::ShortcutKey pressed;
    assert(ipc::pressedShortcut(
        shortcuts[0], 2, [](UINT key) { return key == VK_SPACE; }, pressed));
    assert(!ipc::pressedShortcut(
        shortcuts[0], 3, [](UINT) { return true; }, pressed));
    assert(!ipc::pressedShortcut(
        shortcuts[0], 2, [](UINT) { return false; }, pressed));
    assert(ipc::shortcutAction(shortcuts, VK_SPACE, 0x39, kModifierControl) ==
           0);
    for (unsigned i = 1; i <= 5; ++i)
        assert(ipc::shortcutAction(shortcuts, '0' + i, 0, 6) == i);
    shortcuts[0] = "Control+Shift+J\nAlt+<59>";
    assert(ipc::shortcutAction(shortcuts, VK_SPACE, 0, 2) == -1);
    assert(ipc::shortcutAction(shortcuts, 'J', 0x24, 3) == 0);
    assert(ipc::shortcutAction(shortcuts, VK_F1, 0x3b, 4) == 0);
    assert(ipc::shortcutAction(shortcuts, VK_F1, 0x3b, 6) == -1);
    shortcuts[5].clear();
    assert(ipc::validShortcuts(shortcuts));
    assert(ipc::shortcutAction(shortcuts, '5', 0, 6) == -1);
    assert(ipc::pressedShortcut(
        shortcuts[0], 3, [](UINT key) { return key == 'J'; }, pressed));
    assert(!ipc::pressedShortcut(
        shortcuts[0], 2, [](UINT key) { return key == VK_SPACE; }, pressed));
    shortcuts[1] = "Shift_R";
    assert(ipc::shortcutAction(shortcuts, VK_SHIFT, 0x36, 1) == 1);
    assert(ipc::shortcutAction(shortcuts, VK_SHIFT, 0x2a, 1) == -1);
    shortcuts[2] = "Control+Shift+J";
    assert(!ipc::validShortcuts(shortcuts));
    shortcuts[0] = "Alt+<59>";
    shortcuts[2] = "Alt+F1";
    assert(!ipc::validShortcuts(shortcuts));
    shortcuts[2] = "InvalidKey";
    assert(!ipc::validShortcuts(shortcuts));
    assert(
        !routesKey(VK_SPACE, kModifierControl | kModifierShift, false, false));
    assert(!routesKey('1', 0, true, false));
    assert(routesKey('1', 0, true, true));
    assert(!routesKey(VK_SPACE, 0, true, false));
    assert(routesKey(VK_SPACE, 0, true, true));
    for (const auto key :
         {VK_OEM_1, VK_OEM_PLUS, VK_OEM_COMMA, VK_OEM_MINUS, VK_OEM_PERIOD,
          VK_OEM_2, VK_OEM_3, VK_OEM_4, VK_OEM_5, VK_OEM_6, VK_OEM_7, VK_OEM_8,
          VK_OEM_102}) {
        assert(routesKey(key, 0, true, false));
        assert(routesKey(key, kModifierShift, true, false));
        assert(!routesKey(key, 0, false, false));
        assert(!routesKey(key, kModifierControl, true, false));
        assert(!routesKey(key, kModifierAlt, true, false));
        assert(!routesKey(key, kModifierSuper, true, false));
    }
    for (uint32_t key = '0'; key <= '9'; ++key) {
        assert(routesKey(key, kModifierShift, true, false));
        assert(!routesKey(key, 0, true, false));
        assert(!routesKey(key, kModifierShift, false, false));
    }
    assert(routesKey(VK_OEM_PLUS, 0, true, true));
    assert(routesKey(VK_OEM_MINUS, 0, true, true));
    assert(!routesKey(VK_BACK, 0, true, false));
    assert(routesKey(VK_BACK, 0, true, true));
    assert(routesKey(VK_ESCAPE, 0, true, true));
    assert(!routesKey(VK_F5, 0, true, true));
    assert(routesKey('0', 0, true, false, true));
    assert(routesKey(VK_OEM_1, 0, true, false, true));
    assert(!routesKey('0', 0, false, false, true));
    assert(!routesKey('0', kModifierControl, true, false, true));
    std::wstring text;
    ULONG cursor = 0;
    const std::string input = "\xe4\xbd\xa0\xf0\x9f\x98\x80";
    assert(preeditToWide(input, 3, text, cursor));
    assert(text.size() == 3 && cursor == 1);
    assert(preeditToWide(input, 7, text, cursor) && cursor == 3);
    assert(!preeditToWide(input, 2, text, cursor));
    assert(!preeditToWide(input, 8, text, cursor));
    assert(!utf8ToWide("\xff", text));
    assert(preeditToWide("", 0, text, cursor) && text.empty() && cursor == 0);
}
