#include "displayattribute.h"
#include "tsf.h"

namespace fcitx {
STDAPI Tsf::Activate(ITfThreadMgr *pThreadMgr, TfClientId tfClientId) {
    return ActivateEx(pThreadMgr, tfClientId, 0U);
}

STDAPI Tsf::Deactivate() {
    if (messageWindow_) {
        const auto module = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(messageWindow_, GWLP_HINSTANCE));
        KillTimer(messageWindow_, 1);
        DestroyWindow(messageWindow_);
        UnregisterClassW(L"Fcitx5WindowsDispatchV2", module);
        messageWindow_ = nullptr;
    }
    if (!threadMgr_) {
        pipe_.disconnect();
        return S_OK;
    }
    initTextEditSink(CComPtr<ITfDocumentMgr>());
    uninitThreadMgrEventSink();
    uninitKeyEventSink();
    threadMgr_ = nullptr;
    clientId_ = TF_CLIENTID_NULL;
    pipe_.disconnect();
    return S_OK;
}

STDAPI Tsf::ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId, DWORD) {
    if (pThreadMgr == nullptr || tfClientId == TF_CLIENTID_NULL || threadMgr_) {
        return E_INVALIDARG;
    }
    CComPtr<ITfDocumentMgr> documentMgr;
    CComPtr<ITfCategoryMgr> categories;
    HRESULT activationResult = E_FAIL;
    if (SUCCEEDED(categories.CoCreateInstance(CLSID_TF_CategoryMgr))) {
        categories->RegisterGUID(kPreeditAttribute, &attributeAtom_);
    }
    threadMgr_ = pThreadMgr;
    foreground_ = true;
    clientId_ = tfClientId;
    pipe_.connect();
    if (!initMessageWindow()) {
        goto ActivateExError;
    }
    activationResult = initThreadMgrEventSink();
    if (FAILED(activationResult)) {
        goto ActivateExError;
    }

    if ((threadMgr_->GetFocus(&documentMgr) == S_OK) &&
        (documentMgr != nullptr)) {
        initTextEditSink(documentMgr);
    }

    activationResult = initKeyEventSink();
    if (FAILED(activationResult)) {
        goto ActivateExError;
    }

    return S_OK;

ActivateExError:
    Deactivate();
    return activationResult;
}
} // namespace fcitx
