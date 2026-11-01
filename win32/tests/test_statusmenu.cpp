#include "../ipc/statusmenuprotocol.h"
#include "../tsf/statusmenu.h"
#include <cassert>

using namespace fcitx;
using namespace fcitx::win32::ipc;
int main() {
    StatusMenuReply reply;
    reply.revision = 5;
    StatusMenuReply decoded;
    assert(decodeStatusMenu(encodeStatusMenu(reply), decoded) &&
           decoded.nodes.empty());
    reply.generation = 7;
    reply.inputMethod = "test";
    reply.nodes = {
        {1, 0, StatusMenuNodeKind::Submenu, true, false, false, "parent"},
        {2, 1, StatusMenuNodeKind::Command, true, true, true, "A&\nB",
         "Ctrl+F4"},
        {3, 1, StatusMenuNodeKind::Separator},
        {4, 1, StatusMenuNodeKind::Command, false, false, false, "disabled"}};
    const auto payload = encodeStatusMenu(reply);
    assert(decodeStatusMenu(payload, decoded) && decoded.nodes == reply.nodes);
    for (size_t i = 0; i < payload.size(); ++i)
        assert(
            !decodeStatusMenu({payload.begin(), payload.begin() + i}, decoded));
    auto bad = payload;
    bad.push_back(0);
    assert(!decodeStatusMenu(bad, decoded));
    bad = payload;
    bad[0] = 255;
    assert(!decodeStatusMenu(bad, decoded));
    auto invalid = reply;
    invalid.nodes[1].parentId = 4;
    assert(!validStatusMenu(invalid));
    invalid = reply;
    invalid.nodes[1].nodeId = 1;
    assert(!validStatusMenu(invalid));
    invalid = reply;
    invalid.nodes[2].enabled = true;
    assert(!validStatusMenu(invalid));
    invalid = reply;
    invalid.nodes[1].checkable = false;
    assert(!validStatusMenu(invalid));
    invalid = reply;
    invalid.nodes[1].label = "\xc0\x80";
    assert(!validStatusMenu(invalid));
    invalid = reply;
    invalid.nodes[1].label.assign(1025, 'a');
    assert(!validStatusMenu(invalid));
    invalid = reply;
    invalid.nodes[1].shortcutText.assign(257, 'a');
    assert(!validStatusMenu(invalid));
    invalid = reply;
    invalid.nodes.resize(257);
    assert(!validStatusMenu(invalid));
    invalid = reply;
    invalid.nodes.clear();
    for (uint32_t i = 1; i <= 5; ++i)
        invalid.nodes.push_back({i, i - 1, StatusMenuNodeKind::Submenu, true});
    assert(!validStatusMenu(invalid));
    invalid = reply;
    invalid.error = StatusMenuError::Failed;
    assert(!validStatusMenu(invalid));
    assert(statusMenuUtf8("\xf0\x9f\x98\x80"));
    assert(!statusMenuUtf8("\xed\xa0\x80") &&
           !statusMenuUtf8("\xf4\x90\x80\x80"));
    auto cache = std::make_shared<StatusMenuCache>();
    cache->reply = reply;
    StatusMenuView view;
    assert(view.open(cache));
    const auto command = view.command(2);
    StatusMenuSelection selection;
    assert(!view.select(view.command(1), selection));
    assert(!view.select(view.command(4), selection));
    assert(view.select(command, selection) && selection.node == 2 &&
           selection.cache == cache);
    assert(!view.select(command, selection));
    assert(statusMenuText(reply.nodes[1]) == L"A&&B\tCtrl+F4");
    assert(view.open(cache) && view.command(2) != command);
    assert(!view.select(command, selection));
    view.clear();
    assert(!view.select(view.command(2), selection));
    StatusMenuActionReply action;
    action.state.commit = "plugin commit";
    StatusMenuActionReply restored;
    assert(decodeStatusMenuAction(encodeStatusMenuAction(action), restored));
    assert(restored.state.commit == action.state.commit);
    action.error = StatusMenuError::Stale;
    assert(decodeStatusMenuAction(encodeStatusMenuAction(action), restored));
    bad = encodeStatusMenuAction(action);
    bad.push_back(0);
    assert(!decodeStatusMenuAction(bad, restored));
    assert(!globalSettingsRequest(MessageType::GetStatusMenu));
    assert(!globalSettingsRequest(MessageType::ActivateStatusMenu));
}
