#include "buildinfo.h"
#include "diagnostics.h"
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
    item->setUiPolicy(modernUi_, restrictedInput_);
    if (restrictedInput_)
        item->setAvailable(pipe_.connected());
    item->setUiManager(uiElementMgr_);
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
    aboutUi_.reset();
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
    hideCandidates();
    uiElementMgr_.Release();
    uninitKeyboardCompartment();
    uninitThreadMgrEventSink();
    uninitKeyEventSink();
    threadMgr_ = nullptr;
    clientId_ = TF_CLIENTID_NULL;
    pipe_.disconnect();
    return S_OK;
}

STDAPI Tsf::ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId,
                       DWORD flags) {
    if (pThreadMgr == nullptr || tfClientId == TF_CLIENTID_NULL || threadMgr_) {
        return E_INVALIDARG;
    }
    CComPtr<ITfDocumentMgr> documentMgr;
    CComPtr<ITfCategoryMgr> categories;
    HRESULT activationResult = E_FAIL;
    activationFlags_ = flags;
    activeFlags_ = flags;
    CComPtr<ITfThreadMgrEx> extended;
    HRESULT flagsResult = pThreadMgr->QueryInterface(IID_PPV_ARGS(&extended));
    if (SUCCEEDED(flagsResult)) {
        DWORD active = 0;
        flagsResult = extended->GetActiveFlags(&active);
        if (SUCCEEDED(flagsResult))
            activeFlags_ |= active;
    }
    const auto environment = ipc::clientEnvironment();
    const auto policy = ipc::isolationPolicy(environment);
    restrictedInput_ = policy == ipc::IsolationPolicy::RestrictedInput;
    modernUi_ =
        environment.appContainer || restrictedInput_ ||
        (activeFlags_ & (TF_TMF_IMMERSIVEMODE | TF_TMF_UIELEMENTENABLEDONLY));
    inputAllowed_ =
        policy != ipc::IsolationPolicy::Refuse &&
        !(activeFlags_ & (TF_TMF_SECUREMODE | TF_TMF_COMLESS | TF_TMF_WOW16));
    WCHAR diagnostic[384]{};
    swprintf_s(
        diagnostic,
        L"Fcitx5: ActivateEx pid=%lu tid=%lu bits=%u flags=0x%lx active=0x%lx "
        L"query=0x%lx isolation=%u token=%u package=%u input=%u\n",
        GetCurrentProcessId(), GetCurrentThreadId(),
        static_cast<unsigned>(sizeof(void *) * 8), flags, activeFlags_,
        static_cast<unsigned long>(flagsResult),
        static_cast<unsigned>(environment.isolation), environment.tokenKnown,
        environment.hasPackageIdentity, inputAllowed_);
    OutputDebugStringW(diagnostic);
    OutputDebugStringW(L"Fcitx5: build=");
    OutputDebugStringW(buildinfo::commit);
    OutputDebugStringW(L"\n");
    threadMgr_ = pThreadMgr;
    foreground_ = true;
    clientId_ = tfClientId;
    if (!inputAllowed_)
        return S_OK;
    if (SUCCEEDED(categories.CoCreateInstance(CLSID_TF_CategoryMgr))) {
        categories->RegisterGUID(kPreeditAttribute, &attributeAtom_);
    }
    diagnosticResult(L"UIElement manager",
                     threadMgr_->QueryInterface(IID_PPV_ARGS(&uiElementMgr_)));
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
    diagnosticResult(L"Pipe connect",
                     HRESULT_FROM_WIN32(pipe_.connectionError()));
    if (!initMessageWindow()) {
        goto ActivateExError;
    }
    activationResult = initKeyboardCompartment();
    diagnosticResult(L"keyboard compartment", activationResult);
    if (FAILED(activationResult)) {
        goto ActivateExError;
    }
    activationResult = initLangBarItem();
    diagnosticResult(L"language bar", activationResult);
    if (FAILED(activationResult)) {
        OutputDebugStringW(L"Fcitx5: optional language bar unavailable.\n");
    }
    activationResult = initThreadMgrEventSink();
    diagnosticResult(L"thread manager sink", activationResult);
    if (FAILED(activationResult)) {
        goto ActivateExError;
    }

    if ((threadMgr_->GetFocus(&documentMgr) == S_OK) &&
        (documentMgr != nullptr)) {
        if (!initTextEditSink(documentMgr)) {
            activationResult = E_FAIL;
            goto ActivateExError;
        }
    }

    activationResult = initKeyEventSink();
    diagnosticResult(L"key sink", activationResult);
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
