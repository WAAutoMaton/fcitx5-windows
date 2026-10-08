#include "../tsf/keypolicy.h"
#include "../tsf/textutil.h"
#include <cassert>

int main() {
    using namespace fcitx;
    assert(routesKey('N', 0, true, false));
    assert(!routesKey('N', 0, false, false));
    assert(!routesKey('C', kModifierControl, true, true));
    assert(!routesKey('A', kModifierAlt, true, true));
    assert(isModeSwitchKey(VK_SPACE, kModifierControl));
    assert(routesKey(VK_SPACE, kModifierControl, false, false));
    assert(
        !routesKey(VK_SPACE, kModifierControl | kModifierShift, false, false));
    assert(!routesKey('1', 0, true, false));
    assert(routesKey('1', 0, true, true));
    assert(!routesKey(VK_SPACE, 0, true, false));
    assert(routesKey(VK_SPACE, 0, true, true));
    assert(!routesKey(VK_OEM_PLUS, 0, true, false));
    assert(!routesKey(VK_OEM_MINUS, 0, true, false));
    assert(routesKey(VK_OEM_PLUS, 0, true, true));
    assert(routesKey(VK_OEM_MINUS, 0, true, true));
    assert(!routesKey(VK_BACK, 0, true, false));
    assert(routesKey(VK_BACK, 0, true, true));
    assert(routesKey(VK_ESCAPE, 0, true, true));
    assert(!routesKey(VK_F5, 0, true, true));
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
