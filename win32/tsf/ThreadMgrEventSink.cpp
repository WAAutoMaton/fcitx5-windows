#include "tsf.h"

namespace fcitx {
namespace {
constexpr uint64_t kCapabilityPreedit = 1ULL << 1;
constexpr uint64_t kCapabilityReportKeyRepeat = 1ULL << 38;
} // namespace

HRESULT Tsf::initThreadMgrEventSink() {
    CComPtr<ITfSource> source;
    auto result = threadMgr_->QueryInterface(IID_ITfSource, (void **)&source);
    if (FAILED(result)) {
        return result;
    }
    result = source->AdviseSink(IID_ITfThreadMgrEventSink,
                                (ITfThreadMgrEventSink *)this,
                                &threadMgrEventSinkCookie_);
    if (FAILED(result)) {
        threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;
    }
    return result;
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
    if (!pipe_.connected() || remoteContextId_ != 0 || !textEditSinkContext_) {
        return remoteContextId_ != 0;
    }
    const auto capabilities = kCapabilityPreedit | kCapabilityReportKeyRepeat;
    if (!pipe_.createContext(capabilities, remoteContextId_)) {
        remoteContextId_ = 0;
        return false;
    }
    if (!pipe_.focusIn(remoteContextId_)) {
        pipe_.destroyContext(remoteContextId_);
        remoteContextId_ = 0;
        return false;
    }
    PipeClient::KeyReply reply;
    if (!pipe_.poll(remoteContextId_, reply)) {
        pipe_.disconnect();
        remoteContextId_ = 0;
        return false;
    }
    state_ = std::move(reply);
    return true;
}

void Tsf::clearRemoteContext() {
    cancelComposition();
    ++generation_;
    edits_.clear();
    editRequested_ = false;
    composition_ = nullptr;
    displayedPreedit_.clear();
    displayedCursor_ = 0;
    state_ = {};
    handledKeys_.reset();
    candidates_.hide();
    if (remoteContextId_ != 0) {
        pipe_.focusOut(remoteContextId_);
        pipe_.destroyContext(remoteContextId_);
        remoteContextId_ = 0;
    }
}

STDMETHODIMP Tsf::OnInitDocumentMgr(ITfDocumentMgr *) { return S_OK; }

STDMETHODIMP Tsf::OnUninitDocumentMgr(ITfDocumentMgr *document) {
    CComPtr<ITfDocumentMgr> current;
    if (textEditSinkContext_ &&
        SUCCEEDED(textEditSinkContext_->GetDocumentMgr(&current)) &&
        current == document) {
        initTextEditSink(nullptr);
    }
    return S_OK;
}

STDMETHODIMP Tsf::OnSetFocus(ITfDocumentMgr *pDocMgrFocus, ITfDocumentMgr *) {
    initTextEditSink(pDocMgrFocus);
    return S_OK;
}

STDMETHODIMP Tsf::OnPushContext(ITfContext *context) {
    CComPtr<ITfDocumentMgr> document;
    if (context && SUCCEEDED(context->GetDocumentMgr(&document))) {
        CComPtr<ITfDocumentMgr> focused;
        if (SUCCEEDED(threadMgr_->GetFocus(&focused)) && focused == document) {
            initTextEditSink(document);
        }
    }
    return S_OK;
}

STDMETHODIMP Tsf::OnPopContext(ITfContext *context) {
    if (context == textEditSinkContext_) {
        CComPtr<ITfDocumentMgr> document;
        threadMgr_->GetFocus(&document);
        initTextEditSink(document);
    }
    return S_OK;
}
} // namespace fcitx
