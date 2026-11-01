#pragma once

#include "../win32/ipc/statusmenuprotocol.h"
#include <chrono>
#include <fcitx-utils/signals.h>
#include <fcitx/instance.h>
#include <memory>
#include <unordered_map>

namespace fcitx::win32 {
// All methods and signal callbacks run on the Core event loop.
class WindowsStatusMenu {
  public:
    WindowsStatusMenu(Instance &instance, InputContext &context);
    ~WindowsStatusMenu();
    uint64_t revision(const std::string &method);
    void invalidate();
    void release();
    ipc::StatusMenuReply query(const std::string &method, bool throttle = true);
    ipc::StatusMenuError activate(const std::string &method,
                                  uint64_t generation, uint32_t node);

  private:
    struct Observed;
    struct Tree;
    std::shared_ptr<Observed> observe(ConnectableObject *object, bool menu);
    Tree build(const std::string &method);
    static uint64_t nextIdentity();
    Instance &instance_;
    InputContext &context_;
    std::unique_ptr<HandlerTableEntry<EventHandler>> uiWatcher_;
    std::unique_ptr<HandlerTableEntry<EventHandler>> uiRequestWatcher_;
    std::unordered_map<ConnectableObject *, std::shared_ptr<Observed>>
        observed_;
    std::unique_ptr<Tree> tree_;
    std::string method_;
    uint64_t revision_ = 0;
    bool dirty_ = true;
    bool pendingUi_ = false;
    std::chrono::steady_clock::time_point nextQuery_{};
};
} // namespace fcitx::win32
