#include "tsf.h"

namespace fcitx {
STDAPI Tsf::Activate(ITfThreadMgr *pThreadMgr, TfClientId tfClientId) {
    return ActivateEx(pThreadMgr, tfClientId, 0U);
}

STDAPI Tsf::Deactivate() {
    initTextEditSink(CComPtr<ITfDocumentMgr>());
    uninitThreadMgrEventSink();
    uninitKeyEventSink();
    threadMgr_ = nullptr;
    clientId_ = TF_CLIENTID_NULL;
    pipe_.disconnect();
    return S_OK;
}

STDAPI Tsf::ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId,
                       DWORD) {
    if (pThreadMgr == nullptr) {
        return E_INVALIDARG;
    }
    CComPtr<ITfDocumentMgr> documentMgr;
    threadMgr_ = pThreadMgr;
    clientId_ = tfClientId;
    pipe_.connect();
    if (!initThreadMgrEventSink()) {
        goto ActivateExError;
    }

    if ((threadMgr_->GetFocus(&documentMgr) == S_OK) &&
        (documentMgr != nullptr)) {
        initTextEditSink(documentMgr);
    }

    if (!initKeyEventSink()) {
        goto ActivateExError;
    }

    return S_OK;

ActivateExError:
    Deactivate();
    return E_FAIL;
}
} // namespace fcitx