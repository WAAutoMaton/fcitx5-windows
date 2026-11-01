#pragma once

#include "protocol.h"
#include <unordered_map>

namespace fcitx::win32::ipc {
constexpr size_t kMaxStatusMenuNodes = 256;
constexpr size_t kMaxStatusMenuDepth = 4;
constexpr size_t kMaxStatusMenuBytes = 64 * 1024;
enum class StatusMenuNodeKind : uint8_t { Command, Submenu, Separator };
enum class StatusMenuError : uint8_t {
    None,
    Unavailable,
    Stale,
    Invalid,
    Busy,
    Failed
};
struct StatusMenuNode {
    uint32_t nodeId = 0, parentId = 0;
    StatusMenuNodeKind kind = StatusMenuNodeKind::Command;
    bool enabled = false, checkable = false, checked = false;
    std::string label, shortcutText;
    bool operator==(const StatusMenuNode &) const = default;
};
struct StatusMenuReply {
    StatusMenuError error = StatusMenuError::None;
    uint64_t revision = 0, generation = 0;
    std::string inputMethod;
    std::vector<StatusMenuNode> nodes;
};
struct StatusMenuActionReply {
    StatusMenuError error = StatusMenuError::None;
    KeyReply state;
};

inline bool statusMenuUtf8(std::string_view text) {
    for (size_t i = 0; i < text.size();) {
        const auto lead = static_cast<uint8_t>(text[i++]);
        if (lead < 0x80)
            continue;
        unsigned count = lead >= 0xc2 && lead <= 0xdf   ? 1
                         : lead >= 0xe0 && lead <= 0xef ? 2
                         : lead >= 0xf0 && lead <= 0xf4 ? 3
                                                        : 0;
        if (!count || i + count > text.size())
            return false;
        uint32_t code = lead & ((1U << (6 - count)) - 1);
        for (unsigned j = 0; j < count; ++j) {
            const auto next = static_cast<uint8_t>(text[i++]);
            if ((next & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (next & 0x3f);
        }
        if ((count == 1 && code < 0x80) || (count == 2 && code < 0x800) ||
            (count == 3 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff))
            return false;
    }
    return true;
}

inline bool validStatusMenu(const StatusMenuReply &reply) {
    if (reply.error > StatusMenuError::Failed ||
        reply.nodes.size() > kMaxStatusMenuNodes ||
        reply.inputMethod.size() > 1024 || !statusMenuUtf8(reply.inputMethod))
        return false;
    if (reply.error != StatusMenuError::None)
        return !reply.generation && reply.nodes.empty();
    if (!reply.revision || (reply.nodes.empty() != (reply.generation == 0)))
        return false;
    std::unordered_map<uint32_t, const StatusMenuNode *> seen;
    std::vector<uint32_t> path;
    size_t bytes = 29 + reply.inputMethod.size();
    for (const auto &node : reply.nodes) {
        bytes += 20 + node.label.size() + node.shortcutText.size();
        if (bytes > kMaxStatusMenuBytes || !node.nodeId ||
            seen.contains(node.nodeId) ||
            node.kind > StatusMenuNodeKind::Separator ||
            node.label.size() > 1024 || node.shortcutText.size() > 256 ||
            !statusMenuUtf8(node.label) || !statusMenuUtf8(node.shortcutText) ||
            (node.checked && !node.checkable))
            return false;
        while (!path.empty() && path.back() != node.parentId)
            path.pop_back();
        if (node.parentId && path.empty())
            return false;
        if (path.size() >= kMaxStatusMenuDepth)
            return false;
        if (node.kind == StatusMenuNodeKind::Separator &&
            (node.enabled || node.checkable || node.checked ||
             !node.label.empty() || !node.shortcutText.empty()))
            return false;
        seen.emplace(node.nodeId, &node);
        if (node.kind == StatusMenuNodeKind::Submenu)
            path.push_back(node.nodeId);
    }
    return true;
}
inline std::vector<uint8_t> encodeStatusMenu(const StatusMenuReply &reply) {
    Writer w;
    w.u8(static_cast<uint8_t>(reply.error));
    w.u64(reply.revision);
    w.u64(reply.generation);
    w.string(reply.inputMethod);
    w.u32(static_cast<uint32_t>(reply.nodes.size()));
    for (const auto &n : reply.nodes) {
        w.u32(n.nodeId);
        w.u32(n.parentId);
        w.u8(static_cast<uint8_t>(n.kind));
        w.u8(n.enabled);
        w.u8(n.checkable);
        w.u8(n.checked);
        w.string(n.label);
        w.string(n.shortcutText);
    }
    return w.take();
}
inline bool decodeStatusMenu(const std::vector<uint8_t> &payload,
                             StatusMenuReply &reply) {
    if (payload.size() > kMaxStatusMenuBytes)
        return false;
    Reader r(payload.data(), payload.size());
    StatusMenuReply result;
    uint8_t error;
    uint32_t count;
    if (!r.u8(error) || !r.u64(result.revision) || !r.u64(result.generation) ||
        !r.string(result.inputMethod, 1024) || !r.u32(count) ||
        count > kMaxStatusMenuNodes || count > r.remaining() / 20)
        return false;
    result.error = static_cast<StatusMenuError>(error);
    for (uint32_t i = 0; i < count; ++i) {
        StatusMenuNode n;
        uint8_t kind, enabled, checkable, checked;
        if (!r.u32(n.nodeId) || !r.u32(n.parentId) || !r.u8(kind) ||
            !r.u8(enabled) || !r.u8(checkable) || !r.u8(checked) ||
            enabled > 1 || checkable > 1 || checked > 1 ||
            !r.string(n.label, 1024) || !r.string(n.shortcutText, 256))
            return false;
        n.kind = static_cast<StatusMenuNodeKind>(kind);
        n.enabled = enabled;
        n.checkable = checkable;
        n.checked = checked;
        result.nodes.push_back(std::move(n));
    }
    if (r.remaining() || !validStatusMenu(result))
        return false;
    reply = std::move(result);
    return true;
}
inline std::vector<uint8_t>
encodeStatusMenuAction(const StatusMenuActionReply &reply) {
    Writer w;
    w.u8(static_cast<uint8_t>(reply.error));
    if (reply.error == StatusMenuError::None) {
        const auto data = encodeKeyReply(reply.state);
        w.bytes(data.data(), data.size());
    }
    return w.take();
}
inline bool decodeStatusMenuAction(const std::vector<uint8_t> &data,
                                   StatusMenuActionReply &reply) {
    if (data.empty() || data[0] > static_cast<uint8_t>(StatusMenuError::Failed))
        return false;
    StatusMenuActionReply result;
    result.error = static_cast<StatusMenuError>(data[0]);
    if (result.error == StatusMenuError::None) {
        if (!decodeKeyReply({data.begin() + 1, data.end()}, result.state))
            return false;
    } else if (data.size() != 1)
        return false;
    reply = std::move(result);
    return true;
}
} // namespace fcitx::win32::ipc
