#pragma once

#include <atlcomcli.h>
#include <ctffunc.h>
#include <ctfutb.h>
#include <msctf.h>

class ThreadMgrAdapter : public ITfThreadMgr,
                         public ITfKeystrokeMgr,
                         public ITfSource,
                         public ITfLangBarItemMgr,
                         public ITfLangBarItemSink {
  public:
    explicit ThreadMgrAdapter(ITfThreadMgr *manager) : manager_(manager) {
        manager_->QueryInterface(IID_PPV_ARGS(&source_));
    }
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result)
            return E_INVALIDARG;
        *result = nullptr;
        if (id == IID_ITfCompartmentMgr)
            return manager_->QueryInterface(id, result);
        if (id == IID_IUnknown || id == IID_ITfThreadMgr)
            *result = static_cast<ITfThreadMgr *>(this);
        else if (id == IID_ITfKeystrokeMgr)
            *result = static_cast<ITfKeystrokeMgr *>(this);
        else if (id == IID_ITfSource)
            *result = static_cast<ITfSource *>(this);
        else if (id == IID_ITfLangBarItemMgr)
            *result = static_cast<ITfLangBarItemMgr *>(this);
        else if (id == IID_ITfLangBarItemSink)
            *result = static_cast<ITfLangBarItemSink *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&references_);
    }
    STDMETHODIMP_(ULONG) Release() override {
        const auto remaining = InterlockedDecrement(&references_);
        if (!remaining)
            delete this;
        return remaining;
    }
    STDMETHODIMP Activate(TfClientId *client) override {
        return manager_->Activate(client);
    }
    STDMETHODIMP Deactivate() override { return manager_->Deactivate(); }
    STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr **document) override {
        return manager_->CreateDocumentMgr(document);
    }
    STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs **documents) override {
        return manager_->EnumDocumentMgrs(documents);
    }
    STDMETHODIMP GetFocus(ITfDocumentMgr **document) override {
        return manager_->GetFocus(document);
    }
    STDMETHODIMP SetFocus(ITfDocumentMgr *document) override {
        return manager_->SetFocus(document);
    }
    STDMETHODIMP AssociateFocus(HWND window, ITfDocumentMgr *document,
                                ITfDocumentMgr **previous) override {
        return manager_->AssociateFocus(window, document, previous);
    }
    STDMETHODIMP IsThreadFocus(BOOL *focused) override {
        return manager_->IsThreadFocus(focused);
    }
    STDMETHODIMP GetFunctionProvider(REFCLSID id,
                                     ITfFunctionProvider **provider) override {
        return manager_->GetFunctionProvider(id, provider);
    }
    STDMETHODIMP
    EnumFunctionProviders(IEnumTfFunctionProviders **providers) override {
        return manager_->EnumFunctionProviders(providers);
    }
    STDMETHODIMP
    GetGlobalCompartment(ITfCompartmentMgr **compartment) override {
        return manager_->GetGlobalCompartment(compartment);
    }
    STDMETHODIMP AdviseSink(REFIID id, IUnknown *sink, DWORD *cookie) override {
        return source_->AdviseSink(id, sink, cookie);
    }
    STDMETHODIMP UnadviseSink(DWORD cookie) override {
        return source_->UnadviseSink(cookie);
    }
    STDMETHODIMP AdviseKeyEventSink(TfClientId, ITfKeyEventSink *sink,
                                    BOOL foreground) override {
        if (!sink || sink_)
            return E_INVALIDARG;
        sink_ = sink;
        if (foreground)
            sink_->OnSetFocus(TRUE);
        return S_OK;
    }
    STDMETHODIMP UnadviseKeyEventSink(TfClientId) override {
        sink_.Release();
        return S_OK;
    }
    STDMETHODIMP GetForeground(CLSID *id) override {
        if (!id)
            return E_INVALIDARG;
        *id = GUID_NULL;
        return S_OK;
    }
    STDMETHODIMP TestKeyDown(WPARAM, LPARAM, BOOL *) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP TestKeyUp(WPARAM, LPARAM, BOOL *) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP KeyDown(WPARAM, LPARAM, BOOL *) override { return E_NOTIMPL; }
    STDMETHODIMP KeyUp(WPARAM, LPARAM, BOOL *) override { return E_NOTIMPL; }
    STDMETHODIMP GetPreservedKey(ITfContext *, const TF_PRESERVEDKEY *,
                                 GUID *) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP IsPreservedKey(REFGUID, const TF_PRESERVEDKEY *,
                                BOOL *) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP PreserveKey(TfClientId, REFGUID guid,
                             const TF_PRESERVEDKEY *key, const WCHAR *,
                             ULONG) override {
        ++preserveCalls;
        if (!key || key->uVKey != VK_SPACE || key->uModifiers != TF_MOD_CONTROL)
            return E_INVALIDARG;
        preservedGuid = guid;
        return preserveResult;
    }
    STDMETHODIMP UnpreserveKey(REFGUID guid,
                               const TF_PRESERVEDKEY *key) override {
        if (!key || guid != preservedGuid || key->uVKey != VK_SPACE ||
            key->uModifiers != TF_MOD_CONTROL)
            return E_INVALIDARG;
        ++unpreserveCalls;
        return S_OK;
    }
    STDMETHODIMP SetPreservedKeyDescription(REFGUID, const WCHAR *,
                                            ULONG) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetPreservedKeyDescription(REFGUID, BSTR *) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP SimulatePreservedKey(ITfContext *, REFGUID, BOOL *) override {
        return E_NOTIMPL;
    }

    // The unregistered TIP's language item is captured instead of published.
    STDMETHODIMP AddItem(ITfLangBarItem *item) override {
        ++addItemCalls;
        if (FAILED(addItemResult))
            return addItemResult;
        if (!item || langBarItem)
            return E_INVALIDARG;
        CComPtr<ITfSource> source;
        auto result = item->QueryInterface(IID_PPV_ARGS(&source));
        if (FAILED(result))
            return result;
        result = source->AdviseSink(IID_ITfLangBarItemSink,
                                    static_cast<ITfLangBarItemSink *>(this),
                                    &langBarCookie_);
        if (SUCCEEDED(result))
            langBarItem = item;
        return result;
    }
    STDMETHODIMP RemoveItem(ITfLangBarItem *item) override {
        if (!item || item != langBarItem)
            return E_INVALIDARG;
        ++removeItemCalls;
        CComPtr<ITfSource> source;
        if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&source))))
            source->UnadviseSink(langBarCookie_);
        langBarItem.Release();
        langBarCookie_ = TF_INVALID_COOKIE;
        return S_OK;
    }
    STDMETHODIMP OnUpdate(DWORD flags) override {
        ++langBarUpdates;
        langBarUpdateFlags |= flags;
        return S_OK;
    }
    STDMETHODIMP GetItem(REFGUID guid, ITfLangBarItem **item) override {
        if (!item)
            return E_INVALIDARG;
        *item = nullptr;
        if (guid != GUID_LBI_INPUTMODE || !langBarItem)
            return E_FAIL;
        *item = langBarItem;
        (*item)->AddRef();
        return S_OK;
    }
    STDMETHODIMP EnumItems(IEnumTfLangBarItems **) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP AdviseItemSink(ITfLangBarItemSink *, DWORD *,
                                REFGUID) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP UnadviseItemSink(DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP GetItemFloatingRect(DWORD, REFGUID, RECT *) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetItemsStatus(ULONG, const GUID *, DWORD *) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetItemNum(ULONG *) override { return E_NOTIMPL; }
    STDMETHODIMP GetItems(ULONG, ITfLangBarItem **, TF_LANGBARITEMINFO *,
                          DWORD *, ULONG *) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP AdviseItemsSink(ULONG, ITfLangBarItemSink **, const GUID *,
                                 DWORD *) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP UnadviseItemsSink(ULONG, DWORD *) override {
        return E_NOTIMPL;
    }

    GUID preservedGuid = GUID_NULL;
    unsigned int preserveCalls = 0;
    unsigned int unpreserveCalls = 0;
    HRESULT preserveResult = S_OK;
    CComPtr<ITfLangBarItem> langBarItem;
    unsigned int addItemCalls = 0;
    unsigned int removeItemCalls = 0;
    unsigned int langBarUpdates = 0;
    DWORD langBarUpdateFlags = 0;
    HRESULT addItemResult = S_OK;

  private:
    LONG references_ = 1;
    CComPtr<ITfThreadMgr> manager_;
    CComPtr<ITfSource> source_;
    CComPtr<ITfKeyEventSink> sink_;
    DWORD langBarCookie_ = TF_INVALID_COOKIE;
};
