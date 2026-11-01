#include "tsf.h"

namespace fcitx {
void Tsf::clearStatusMenu() {
    statusMenuActions_.clear();
    statusMenuCache_.reset();
    statusMenuRefreshPosted_ = false;
    statusMenuRevision_ = 0;
    if (langBarItem_)
        langBarItem_->invalidateStatusMenu();
}
void Tsf::scheduleStatusMenu(uint64_t revision) {
    if (restrictedInput_ || !inputAllowed_ || !scopeAllowed_ || !foreground_ ||
        !textEditSinkContext_ || !remoteContextId_ || !pipe_.connected())
        return;
    if (statusMenuCache_ && statusMenuCache_->reply.inputMethod != inputMethod_)
        clearStatusMenu();
    statusMenuRevision_ = revision;
    if (langBarItem_)
        langBarItem_->setStatusMenuContext(true);
    if (statusMenuCache_ &&
        statusMenuCache_->reply.error == ipc::StatusMenuError::None &&
        statusMenuCache_->reply.revision == revision)
        return;
    if (!statusMenuRefreshPosted_ && messageWindow_) {
        statusMenuRefreshPosted_ =
            PostMessageW(messageWindow_, LangBarItem::kStatusMenuMessage, 0,
                         0) != FALSE;
    }
}
void Tsf::refreshStatusMenu() {
    statusMenuRefreshPosted_ = false;
    if (restrictedInput_ || !inputAllowed_ || !foreground_ ||
        !remoteContextId_ || !textEditSinkContext_ || !pipe_.connected())
        return;
    if (!refreshContextPolicy()) {
        clearStatusMenu();
        return;
    }
    if (GetTickCount64() < nextStatusMenuQuery_)
        return;
    nextStatusMenuQuery_ = GetTickCount64() + 500;
    const StatusMenuBinding binding{textEditSinkContext_, generation_,
                                    remoteContextId_,
                                    pipe_.connectionGeneration()};
    ipc::StatusMenuReply reply;
    if (!pipe_.getStatusMenu(binding.remoteContext, reply)) {
        clearStatusMenu();
        if (!pipe_.connected())
            clearRemoteContext();
        return;
    }
    if (!statusMenuBindingValid(binding))
        return;
    if (reply.error == ipc::StatusMenuError::Busy)
        return;
    auto cache = std::make_shared<StatusMenuCache>();
    cache->binding = binding;
    cache->reply = std::move(reply);
    statusMenuCache_ = cache;
    if (langBarItem_) {
        langBarItem_->setStatusMenuContext(true);
        langBarItem_->setStatusMenu(cache);
    }
}
bool Tsf::statusMenuBindingValid(const StatusMenuBinding &binding) const {
    return inputAllowed_ && scopeAllowed_ && foreground_ && !restrictedInput_ &&
           binding.context == textEditSinkContext_ &&
           binding.localGeneration == generation_ && binding.remoteContext &&
           binding.remoteContext == remoteContextId_ && pipe_.connected() &&
           binding.connection == pipe_.connectionGeneration();
}
HRESULT Tsf::queueStatusMenu(const StatusMenuSelection &selection) {
    if (!selection.cache || !statusMenuBindingValid(selection.cache->binding))
        return E_UNEXPECTED;
    if (statusMenuActions_.size() >= 8)
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    try {
        statusMenuActions_.push_back(selection);
    } catch (...) {
        return E_OUTOFMEMORY;
    }
    if (!PostMessageW(messageWindow_, kStatusMenuActionMessage, 0, 0)) {
        statusMenuActions_.pop_back();
        return E_FAIL;
    }
    return S_OK;
}
void Tsf::processStatusMenu() {
    if (statusMenuActions_.empty())
        return;
    const auto selection = statusMenuActions_.front();
    statusMenuActions_.pop_front();
    const auto binding = selection.cache->binding;
    if (!statusMenuBindingValid(binding))
        return;
    if (!refreshContextPolicy()) {
        clearStatusMenu();
        return;
    }
    ipc::StatusMenuActionReply reply;
    const bool received = pipe_.activateStatusMenu(
        binding.remoteContext, selection.cache->reply.generation,
        selection.node, reply);
    if (!received) {
        clearStatusMenu();
        if (!pipe_.connected())
            clearRemoteContext();
        return;
    }
    if (!statusMenuBindingValid(binding))
        return;
    statusMenuCache_.reset();
    if (langBarItem_)
        langBarItem_->invalidateStatusMenu();
    if (reply.error == ipc::StatusMenuError::None) {
        // submitSnapshot captures the same ITfContext and local generation;
        // SnapshotEditSession checks both again before touching the document.
        submitSnapshot(std::move(reply.state), false);
    }
    scheduleStatusMenu(statusMenuRevision_);
}
} // namespace fcitx
