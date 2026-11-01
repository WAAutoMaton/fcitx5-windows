#pragma once

#include "../ipc/statusmenuprotocol.h"
#include <atlcomcli.h>
#include <memory>
#include <msctf.h>
#include <unordered_map>

namespace fcitx {
struct StatusMenuBinding {
    CComPtr<ITfContext> context;
    uint64_t localGeneration = 0, remoteContext = 0, connection = 0;
};
struct StatusMenuCache {
    StatusMenuBinding binding;
    win32::ipc::StatusMenuReply reply;
};
struct StatusMenuSelection {
    std::shared_ptr<const StatusMenuCache> cache;
    uint32_t node = 0;
};
// Each open menu owns a frozen cache and a fresh range of Windows command IDs.
class StatusMenuView {
  public:
    bool open(std::shared_ptr<const StatusMenuCache> cache);
    void clear();
    bool select(UINT command, StatusMenuSelection &selection);
    UINT command(uint32_t node) const;
    std::shared_ptr<const StatusMenuCache> cache() const { return cache_; }

  private:
    uint64_t nextCommand_ = 0x10000;
    bool selected_ = false;
    std::shared_ptr<const StatusMenuCache> cache_;
    std::unordered_map<UINT, uint32_t> commands_;
};
std::wstring statusMenuText(const win32::ipc::StatusMenuNode &node);
} // namespace fcitx
