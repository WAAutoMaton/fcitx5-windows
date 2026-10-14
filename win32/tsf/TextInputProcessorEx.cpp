#include "displayattribute.h"
#include "textutil.h"
#include "tsf.h"

#include <new>

namespace fcitx {
HRESULT Tsf::initLangBarItem() {
    CComPtr<ITfLangBarItemMgr> manager;
    auto result = threadMgr_->QueryInterface(IID_PPV_ARGS(&manager));
    if (FAILED(result)) {
        return result;
    }
    auto *item = new (std::nothrow) LangBarItem(messageWindow_, keyboardOpen_);
    if (!item) {
        return E_OUTOFMEMORY;
    }
    result = manager->AddItem(static_cast<ITfLangBarItem *>(item));
    if (FAILED(result)) {
        item->detach();
        item->Release();
        return result;
    }
    langBarItemManager_ = std::move(manager);
    langBarItem_.Attach(item);
    item->setTranslation(translationMonitor_.snapshot());
    about_.setTranslation(translationMonitor_.snapshot());
    return S_OK;
}

void Tsf::uninitLangBarItem() {
    if (!langBarItem_) {
        langBarItemManager_.Release();
        return;
    }
    langBarItem_.p->detach();
    if (langBarItemManager_) {
        langBarItemManager_->RemoveItem(
            static_cast<ITfLangBarItem *>(langBarItem_.p));
    }
    langBarItem_.Release();
    langBarItemManager_.Release();
}

void Tsf::notifyLangBarItem() {
    if (langBarItem_) {
        langBarItem_.p->setMode(keyboardOpen_);
    }
}

void Tsf::acceptInputMethod(const PipeClient::KeyReply &reply) {
    inputMethod_ = reply.inputMethod;
    if (reply.enabled != keyboardOpen_) {
        setKeyboardOpen(reply.enabled);
    }
    if (langBarItem_) {
        std::wstring name, label, icon;
        utf8ToWide(reply.inputMethodName, name);
        utf8ToWide(reply.iconLabel, label);
        utf8ToWide(reply.inputMethodIcon, icon);
        langBarItem_.p->setInputMethod(reply.enabled, std::move(name),
                                       std::move(label), std::move(icon));
    }
}

STDAPI Tsf::Activate(ITfThreadMgr *pThreadMgr, TfClientId tfClientId) {
    return ActivateEx(pThreadMgr, tfClientId, 0U);
}

STDAPI Tsf::Deactivate() {
    translationMonitor_.reset();
    about_.close();
    if (serviceTask_.valid()) {
        serviceTask_.get();
    }
    serviceCommand_ = 0;
    pendingServiceCommand_ = 0;
    serviceHostClosing_ = false;
    uninitLangBarItem();
    if (messageWindow_) {
        const auto module = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(messageWindow_, GWLP_HINSTANCE));
        KillTimer(messageWindow_, 1);
        DestroyWindow(messageWindow_);
        UnregisterClassW(L"Fcitx5WindowsDispatchV2", module);
        messageWindow_ = nullptr;
    }
    candidateRefreshPosted_ = false;
    if (!threadMgr_) {
        pipe_.disconnect();
        return S_OK;
    }
    initTextEditSink(CComPtr<ITfDocumentMgr>());
    uninitKeyboardCompartment();
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
    if (ipc::hostOnlyClient()) {
        translationMonitor_.setLanguageReader([](ipc::UiLanguage &language) {
            PipeClient client;
            ipc::GlobalSettingsReply reply;
            if (!client.connect() || !client.getGlobalSettings(reply) ||
                reply.error != ipc::SettingsError::None)
                return false;
            language = reply.resolvedLanguage;
            return true;
        });
    }
    pipe_.connect();
    if (!initMessageWindow()) {
        goto ActivateExError;
    }
    activationResult = initKeyboardCompartment();
    if (FAILED(activationResult)) {
        goto ActivateExError;
    }
    activationResult = initLangBarItem();
    if (FAILED(activationResult)) {
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

    PostMessageW(messageWindow_, kEnsureServiceMessage, 0, 0);
    return S_OK;

ActivateExError:
    Deactivate();
    return activationResult;
}
} // namespace fcitx
