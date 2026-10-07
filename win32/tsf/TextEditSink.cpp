#include "tsf.h"

namespace fcitx {
bool Tsf::initTextEditSink(CComPtr<ITfDocumentMgr> documentMgr) {
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
    if (FAILED(documentMgr->GetTop(&textEditSinkContext_))) {
        return false;
    }
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

STDMETHODIMP Tsf::OnEndEdit(ITfContext *, TfEditCookie,
                            ITfEditRecord *) {
    return S_OK;
}
} // namespace fcitx