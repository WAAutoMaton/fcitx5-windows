#include "tsf.h"

namespace fcitx {
bool Tsf::initTextEditSink(CComPtr<ITfDocumentMgr> documentMgr) {
    CComPtr<ITfContext> next;
    if (documentMgr && FAILED(documentMgr->GetTop(&next))) {
        return false;
    }
    if (next && next == textEditSinkContext_) {
        return true;
    }
    CComPtr<ITfSource> source;
    clearRemoteContext();
    if (textEditSinkCookie_ != TF_INVALID_COOKIE) {
        if (SUCCEEDED(textEditSinkContext_->QueryInterface(IID_ITfSource,
                                                           (void **)&source))) {
            source->UnadviseSink(textEditSinkCookie_);
        }
        textEditSinkContext_ = nullptr;
        textEditSinkCookie_ = TF_INVALID_COOKIE;
    }
    if (documentMgr == nullptr) {
        return true;
    }
    textEditSinkContext_ = next;
    if (textEditSinkContext_ == nullptr) {
        return true;
    }
    source.Release();
    bool ret = false;
    if (SUCCEEDED(textEditSinkContext_->QueryInterface(IID_ITfSource,
                                                       (void **)&source))) {
        if (SUCCEEDED(source->AdviseSink(IID_ITfTextEditSink,
                                         (ITfTextEditSink *)this,
                                         &textEditSinkCookie_))) {
            ret = true;
        } else {
            textEditSinkCookie_ = TF_INVALID_COOKIE;
        }
    }
    if (!ret) {
        textEditSinkContext_ = nullptr;
        return false;
    }
    initRemoteContext();
    return true;
}

STDMETHODIMP Tsf::OnEndEdit(ITfContext *context, TfEditCookie cookie,
                            ITfEditRecord *record) {
    if (!composition_ || context != textEditSinkContext_ || !record) {
        return S_OK;
    }
    BOOL changed = FALSE;
    if (FAILED(record->GetSelectionStatus(&changed)) || !changed) {
        return S_OK;
    }
    TF_SELECTION selection{};
    ULONG fetched = 0;
    CComPtr<ITfRange> range;
    if (FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1,
                                     &selection, &fetched)) ||
        !fetched) {
        return S_OK;
    }
    CComPtr<ITfRange> selected;
    selected.Attach(selection.range);
    LONG start = 0, end = 0;
    if (SUCCEEDED(composition_->GetRange(&range)) && range &&
        SUCCEEDED(
            range->CompareStart(cookie, selected, TF_ANCHOR_START, &start)) &&
        SUCCEEDED(range->CompareEnd(cookie, selected, TF_ANCHOR_END, &end)) &&
        (start > 0 || end < 0)) {
        cancelComposition();
        ++generation_;
        edits_.clear();
        editRequested_ = false;
        displayedPreedit_.clear();
        candidates_.hide();
        PipeClient::KeyReply reply;
        if (remoteContextId_ && pipe_.reset(remoteContextId_, reply)) {
            state_ = std::move(reply);
        }
    }
    return S_OK;
}
} // namespace fcitx
