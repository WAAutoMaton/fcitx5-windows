#include "tsf.h"

namespace fcitx {
namespace {
constexpr uint64_t kCapabilityPreedit = 1ULL << 1;
constexpr uint64_t kCapabilityFormattedPreedit = 1ULL << 4;
constexpr uint64_t kCapabilityReportKeyRepeat = 1ULL << 38;
}

bool Tsf::initThreadMgrEventSink() {
    CComPtr<ITfSource> source;
    if (threadMgr_->QueryInterface(IID_ITfSource, (void **)&source) != S_OK) {
        return false;
    }
    if (source->AdviseSink(IID_ITfThreadMgrEventSink,
                           (ITfThreadMgrEventSink *)this,
                           &threadMgrEventSinkCookie_) != S_OK) {
        threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;
    }
    return threadMgrEventSinkCookie_ != TF_INVALID_COOKIE;
}

void Tsf::uninitThreadMgrEventSink() {
    CComPtr<ITfSource> source;
    if (threadMgrEventSinkCookie_ == TF_INVALID_COOKIE) {
        return;
    }
    if (SUCCEEDED(
            threadMgr_->QueryInterface(IID_ITfSource, (void **)&source))) {
        source->UnadviseSink(threadMgrEventSinkCookie_);
    }
    threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;
}

bool Tsf::initRemoteContext() {
    if (!pipe_.connected() || remoteContextId_ != 0) {
        return remoteContextId_ != 0;
    }
    const auto capabilities = kCapabilityPreedit |
                              kCapabilityFormattedPreedit |
                              kCapabilityReportKeyRepeat;
    if (!pipe_.createContext(capabilities, remoteContextId_)) {
        remoteContextId_ = 0;
        return false;
    }
    if (!pipe_.focusIn(remoteContextId_)) {
        pipe_.destroyContext(remoteContextId_);
        remoteContextId_ = 0;
        return false;
    }
    return true;
}

void Tsf::clearRemoteContext() {
    composition_ = nullptr;
    pendingEditContext_ = nullptr;
    pendingCommit_.clear();
    pendingPreedit_.clear();
    pendingPreeditCursor_ = 0;
    if (remoteContextId_ != 0) {
        pipe_.focusOut(remoteContextId_);
        pipe_.destroyContext(remoteContextId_);
        remoteContextId_ = 0;
    }
}

STDMETHODIMP Tsf::OnInitDocumentMgr(ITfDocumentMgr *) { return S_OK; }

STDMETHODIMP Tsf::OnUninitDocumentMgr(ITfDocumentMgr *) { return S_OK; }

STDMETHODIMP Tsf::OnSetFocus(ITfDocumentMgr *pDocMgrFocus,
                             ITfDocumentMgr *) {
    initTextEditSink(pDocMgrFocus);
    return S_OK;
}

STDMETHODIMP Tsf::OnPushContext(ITfContext *) { return S_OK; }

STDMETHODIMP Tsf::OnPopContext(ITfContext *) { return S_OK; }
} // namespace fcitx