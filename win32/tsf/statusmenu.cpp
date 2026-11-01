#include "statusmenu.h"
#include "textutil.h"

namespace fcitx {
bool StatusMenuView::open(std::shared_ptr<const StatusMenuCache> cache) {
    clear();
    if (!cache || cache->reply.error != win32::ipc::StatusMenuError::None ||
        !win32::ipc::validStatusMenu(cache->reply))
        return false;
    if (nextCommand_ + cache->reply.nodes.size() > UINT_MAX)
        return false;
    cache_ = std::move(cache);
    for (const auto &node : cache_->reply.nodes)
        commands_.emplace(static_cast<UINT>(nextCommand_++), node.nodeId);
    return true;
}
void StatusMenuView::clear() {
    cache_.reset();
    commands_.clear();
    selected_ = false;
}
UINT StatusMenuView::command(uint32_t node) const {
    for (const auto &[command, id] : commands_)
        if (id == node)
            return command;
    return 0;
}
bool StatusMenuView::select(UINT command, StatusMenuSelection &selection) {
    if (!cache_ || selected_)
        return false;
    const auto found = commands_.find(command);
    if (found == commands_.end())
        return false;
    for (const auto &node : cache_->reply.nodes)
        if (node.nodeId == found->second) {
            if (!node.enabled ||
                node.kind != win32::ipc::StatusMenuNodeKind::Command)
                return false;
            selected_ = true;
            selection = {cache_, node.nodeId};
            return true;
        }
    return false;
}
std::wstring statusMenuText(const win32::ipc::StatusMenuNode &node) {
    auto clean = [](const std::string &source) {
        std::wstring wide, result;
        utf8ToWide(source, wide);
        for (const auto ch : wide) {
            if (ch < 0x20 || (ch >= 0x7f && ch <= 0x9f))
                continue;
            result += ch;
            if (ch == L'&')
                result += L'&';
        }
        return result;
    };
    auto text = clean(node.label);
    const auto shortcut = clean(node.shortcutText);
    if (!shortcut.empty()) {
        text += L'\t';
        text += shortcut;
    }
    return text;
}
} // namespace fcitx
