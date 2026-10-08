#include "../dll/util.h"
#include "threadmgradapter.h"
#include <algorithm>
#include <atlcomcli.h>
#include <iostream>
#include <msctf.h>
#include <stdexcept>
#include <string>
#include <textstor.h>

namespace {

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

class TextStore : public ITextStoreACP {
  public:
    TextStore() {
        window_ = CreateWindowExW(0, L"STATIC", L"Fcitx5 TSF test",
                                  WS_OVERLAPPED, 0, 0, 400, 200, nullptr,
                                  nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    ~TextStore() {
        if (window_)
            DestroyWindow(window_);
    }
    std::wstring text;
    bool denySynchronous = false;
    ULONG writes = 0;
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result)
            return E_INVALIDARG;
        *result = nullptr;
        if (id != IID_IUnknown && id != IID_ITextStoreACP)
            return E_NOINTERFACE;
        *result = static_cast<ITextStoreACP *>(this);
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
    STDMETHODIMP AdviseSink(REFIID id, IUnknown *sink, DWORD) override {
        if (id != IID_ITextStoreACPSink || !sink)
            return E_INVALIDARG;
        if (sink_)
            return CONNECT_E_ADVISELIMIT;
        return sink->QueryInterface(IID_PPV_ARGS(&sink_));
    }
    STDMETHODIMP UnadviseSink(IUnknown *) override {
        sink_.Release();
        return S_OK;
    }
    STDMETHODIMP RequestLock(DWORD flags, HRESULT *result) override {
        if (!result || !sink_)
            return E_INVALIDARG;
        if (lock_ || (denySynchronous && (flags & TS_LF_SYNC))) {
            *result = TS_E_SYNCHRONOUS;
            return S_OK;
        }
        lock_ = flags;
        *result = sink_->OnLockGranted(flags);
        lock_ = 0;
        return S_OK;
    }
    STDMETHODIMP GetStatus(TS_STATUS *status) override {
        if (!status)
            return E_INVALIDARG;
        *status = {0, TS_SS_NOHIDDENTEXT};
        return S_OK;
    }
    STDMETHODIMP QueryInsert(LONG start, LONG end, ULONG, LONG *resultStart,
                             LONG *resultEnd) override {
        if (!resultStart || !resultEnd || !valid(start, end))
            return E_INVALIDARG;
        *resultStart = start;
        *resultEnd = end;
        return S_OK;
    }
    STDMETHODIMP GetSelection(ULONG index, ULONG count,
                              TS_SELECTION_ACP *selection,
                              ULONG *fetched) override {
        if (!selection || !fetched || !count ||
            (index != TS_DEFAULT_SELECTION && index != 0))
            return E_INVALIDARG;
        if (!lock_)
            return TS_E_NOLOCK;
        selection[0] = selection_;
        *fetched = 1;
        return S_OK;
    }
    STDMETHODIMP SetSelection(ULONG count,
                              const TS_SELECTION_ACP *selection) override {
        if (!selection || count != 1 ||
            !valid(selection[0].acpStart, selection[0].acpEnd))
            return E_INVALIDARG;
        if ((lock_ & TS_LF_READWRITE) != TS_LF_READWRITE)
            return TS_E_NOLOCK;
        selection_ = selection[0];
        return S_OK;
    }
    STDMETHODIMP GetText(LONG start, LONG end, WCHAR *buffer, ULONG capacity,
                         ULONG *copied, TS_RUNINFO *runs, ULONG runCapacity,
                         ULONG *runsCopied, LONG *next) override {
        if (end == -1)
            end = static_cast<LONG>(text.size());
        if (!copied || !runsCopied || !next || !valid(start, end))
            return E_INVALIDARG;
        if (!lock_)
            return TS_E_NOLOCK;
        *copied = std::min(capacity, static_cast<ULONG>(end - start));
        *runsCopied = 0;
        *next = start + static_cast<LONG>(*copied);
        if (*copied && buffer)
            std::copy_n(text.data() + start, *copied, buffer);
        if (runs && runCapacity && end > start) {
            runs[0] = {static_cast<ULONG>(end - start), TS_RT_PLAIN};
            *runsCopied = 1;
        }
        return S_OK;
    }
    STDMETHODIMP SetText(DWORD, LONG start, LONG end, const WCHAR *value,
                         ULONG length, TS_TEXTCHANGE *change) override {
        if (!change || (!value && length) || !valid(start, end))
            return E_INVALIDARG;
        if ((lock_ & TS_LF_READWRITE) != TS_LF_READWRITE)
            return TS_E_NOLOCK;
        text.replace(start, end - start, value ? value : L"", length);
        *change = {start, end, start + static_cast<LONG>(length)};
        ++writes;
        return S_OK;
    }
    STDMETHODIMP InsertTextAtSelection(DWORD flags, const WCHAR *value,
                                       ULONG length, LONG *start, LONG *end,
                                       TS_TEXTCHANGE *change) override {
        if (!lock_)
            return TS_E_NOLOCK;
        if (start)
            *start = selection_.acpStart;
        if (end)
            *end = selection_.acpEnd;
        if (flags & TS_IAS_QUERYONLY)
            return S_OK;
        return SetText(0, selection_.acpStart, selection_.acpEnd, value, length,
                       change);
    }
    STDMETHODIMP GetFormattedText(LONG, LONG, IDataObject **result) override {
        if (result)
            *result = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetEmbedded(LONG, REFGUID, REFIID,
                             IUnknown **result) override {
        if (result)
            *result = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP QueryInsertEmbedded(const GUID *, const FORMATETC *,
                                     BOOL *allowed) override {
        if (!allowed)
            return E_INVALIDARG;
        *allowed = FALSE;
        return S_OK;
    }
    STDMETHODIMP InsertEmbedded(DWORD, LONG, LONG, IDataObject *,
                                TS_TEXTCHANGE *) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP InsertEmbeddedAtSelection(DWORD, IDataObject *, LONG *, LONG *,
                                           TS_TEXTCHANGE *) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP RequestSupportedAttrs(DWORD, ULONG,
                                       const TS_ATTRID *) override {
        return S_OK;
    }
    STDMETHODIMP RequestAttrsAtPosition(LONG, ULONG, const TS_ATTRID *,
                                        DWORD) override {
        return S_OK;
    }
    STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG, ULONG,
                                                     const TS_ATTRID *,
                                                     DWORD) override {
        return S_OK;
    }
    STDMETHODIMP FindNextAttrTransition(LONG, LONG halt, ULONG,
                                        const TS_ATTRID *, DWORD, LONG *next,
                                        BOOL *found, LONG *offset) override {
        if (!next || !found || !offset)
            return E_INVALIDARG;
        *next = halt;
        *found = FALSE;
        *offset = 0;
        return S_OK;
    }
    STDMETHODIMP RetrieveRequestedAttrs(ULONG, TS_ATTRVAL *,
                                        ULONG *fetched) override {
        if (!fetched)
            return E_INVALIDARG;
        *fetched = 0;
        return S_OK;
    }
    STDMETHODIMP GetEndACP(LONG *end) override {
        if (!end)
            return E_INVALIDARG;
        *end = static_cast<LONG>(text.size());
        return S_OK;
    }
    STDMETHODIMP GetActiveView(TsViewCookie *view) override {
        if (!view)
            return E_INVALIDARG;
        *view = 1;
        return S_OK;
    }
    STDMETHODIMP GetACPFromPoint(TsViewCookie, const POINT *, DWORD,
                                 LONG *position) override {
        if (!position)
            return E_INVALIDARG;
        *position = selection_.acpStart;
        return S_OK;
    }
    STDMETHODIMP GetTextExt(TsViewCookie, LONG start, LONG end, RECT *rect,
                            BOOL *clipped) override {
        if (!rect || !clipped)
            return E_INVALIDARG;
        *rect = {10 + start * 10, 10, 10 + end * 10, 30};
        *clipped = FALSE;
        return S_OK;
    }
    STDMETHODIMP GetScreenExt(TsViewCookie, RECT *rect) override {
        if (!rect)
            return E_INVALIDARG;
        *rect = {0, 0, 400, 200};
        return S_OK;
    }
    STDMETHODIMP GetWnd(TsViewCookie, HWND *window) override {
        if (!window)
            return E_INVALIDARG;
        *window = window_;
        return S_OK;
    }

  private:
    bool valid(LONG start, LONG end) const {
        return start >= 0 && end >= start &&
               end <= static_cast<LONG>(text.size());
    }
    LONG references_ = 1;
    DWORD lock_ = 0;
    TS_SELECTION_ACP selection_{0, 0, {TS_AE_NONE, FALSE}};
    CComPtr<ITextStoreACPSink> sink_;
    HWND window_ = nullptr;
};

void pump(DWORD duration = 50) {
    const auto deadline = GetTickCount64() + duration;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    } while (GetTickCount64() < deadline);
}

void press(ITfKeyEventSink *keys, ITfContext *context, uint32_t key,
           bool runPump = true) {
    BOOL handled = FALSE;
    require(SUCCEEDED(keys->OnTestKeyDown(context, key, 0, &handled)) &&
                handled,
            "key routing failed");
    require(SUCCEEDED(keys->OnKeyDown(context, key, 0, &handled)) && handled,
            "key processing failed");
    if (runPump)
        pump();
}

} // namespace

int wmain(int count, wchar_t **arguments) {
    if (count != 2)
        return 2;
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)),
            "COM initialization failed");
    HMODULE module = LoadLibraryW(arguments[1]);
    if (!module)
        return 3;
    int exit = 1;
    try {
        using GetFactory = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
        using CanUnload = HRESULT(WINAPI *)();
        auto getFactory = reinterpret_cast<GetFactory>(
            GetProcAddress(module, "DllGetClassObject"));
        auto canUnload = reinterpret_cast<CanUnload>(
            GetProcAddress(module, "DllCanUnloadNow"));
        require(getFactory && canUnload && canUnload() == S_OK,
                "DLL exports/unload state failed");
        CComPtr<IClassFactory> factory;
        require(getFactory(GUID_NULL, IID_IClassFactory,
                           reinterpret_cast<void **>(&factory)) ==
                    CLASS_E_CLASSNOTAVAILABLE,
                "DLL accepted the wrong CLSID");
        require(SUCCEEDED(getFactory(fcitx::FCITX_CLSID, IID_IClassFactory,
                                     reinterpret_cast<void **>(&factory))),
                "class factory failed");
        CComPtr<ITfTextInputProcessorEx> service;
        require(
            SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&service))),
            "TIP creation failed");
        CComPtr<ITfKeyEventSink> keys;
        CComPtr<ITfCompositionSink> compositionSink;
        CComPtr<ITfThreadMgrEventSink> events;
        CComPtr<ITfDisplayAttributeProvider> attributes;
        require(SUCCEEDED(service.QueryInterface(&keys)) &&
                    SUCCEEDED(service.QueryInterface(&compositionSink)) &&
                    SUCCEEDED(service.QueryInterface(&attributes)),
                "TIP interfaces missing");
        require(SUCCEEDED(service.QueryInterface(&events)),
                "thread event interface missing");
        require(canUnload() == S_FALSE,
                "live TIP incorrectly permits unloading");
        CComPtr<IEnumTfDisplayAttributeInfo> attributeEnumerator;
        CComPtr<ITfDisplayAttributeInfo> attributeInfo;
        require(SUCCEEDED(
                    attributes->EnumDisplayAttributeInfo(&attributeEnumerator)),
                "display attribute enumeration failed");
        ULONG fetchedAttributes = 0;
        require(SUCCEEDED(attributeEnumerator->Next(1, &attributeInfo,
                                                    &fetchedAttributes)) &&
                    fetchedAttributes == 1,
                "preedit display attribute missing");
        TF_DISPLAYATTRIBUTE display{};
        require(SUCCEEDED(attributeInfo->GetAttributeInfo(&display)) &&
                    display.lsStyle == TF_LS_DOT,
                "preedit underline missing");
        CComPtr<ITfThreadMgrEx> manager;
        require(SUCCEEDED(manager.CoCreateInstance(CLSID_TF_ThreadMgr)),
                "thread manager creation failed");
        TfClientId client = TF_CLIENTID_NULL;
        require(SUCCEEDED(manager->ActivateEx(&client, TF_TMAE_NOACTIVATETIP)),
                "thread manager activation failed");
        CComPtr<ITfThreadMgr> adapter;
        adapter.Attach(new ThreadMgrAdapter(manager));
        CComPtr<TextStore> store;
        store.Attach(new TextStore);
        CComPtr<ITfDocumentMgr> document;
        CComPtr<ITfContext> context;
        TfEditCookie cookie = 0;
        require(SUCCEEDED(manager->CreateDocumentMgr(&document)) &&
                    SUCCEEDED(document->CreateContext(client, 0, store,
                                                      &context, &cookie)) &&
                    SUCCEEDED(document->Push(context)) &&
                    SUCCEEDED(manager->SetFocus(document)),
                "real TSF context creation failed");
        const auto activation = service->ActivateEx(adapter, client, 0);
        if (FAILED(activation))
            std::cerr << "ActivateEx HRESULT: 0x" << std::hex << activation
                      << std::dec << '\n';
        require(SUCCEEDED(activation), "TIP activation failed");
        BOOL handled = FALSE;
        const auto writes = store->writes;
        require(SUCCEEDED(keys->OnTestKeyDown(context, 'N', 0, &handled)) &&
                    handled &&
                    SUCCEEDED(keys->OnTestKeyDown(context, 'N', 0, &handled)) &&
                    handled && store->writes == writes,
                "test-key callback mutated the document");
        for (const auto letter : std::string("NIHAO"))
            press(keys, context, letter);
        require(!store->text.empty(),
                "composition did not reach the TSF text store");
        press(keys, context, VK_SPACE);
        const std::wstring hello = L"\u4f60\u597d";
        require(store->text == hello,
                "pinyin Chinese commit did not reach the TSF text store");
        store->denySynchronous = true;
        press(keys, context, 'N');
        press(keys, context, 'I');
        press(keys, context, VK_BACK);
        require(store->text.size() == hello.size() + 1,
                "TSF backspace did not shrink preedit");
        press(keys, context, VK_ESCAPE);
        pump(200);
        require(store->text == hello,
                "async cancellation corrupted committed text");
        press(keys, context, 'N');
        CComPtr<TextStore> otherStore;
        otherStore.Attach(new TextStore);
        CComPtr<ITfDocumentMgr> otherDocument;
        CComPtr<ITfContext> otherContext;
        require(SUCCEEDED(manager->CreateDocumentMgr(&otherDocument)) &&
                    SUCCEEDED(otherDocument->CreateContext(
                        client, 0, otherStore, &otherContext, &cookie)) &&
                    SUCCEEDED(otherDocument->Push(otherContext)) &&
                    SUCCEEDED(manager->SetFocus(otherDocument)),
                "focus switch failed");
        require(SUCCEEDED(events->OnSetFocus(otherDocument, document)),
                "focus callback failed");
        pump(200);
        require(store->text == hello && otherStore->text.empty(),
                "focus switch leaked composition");
        press(keys, otherContext, 'N', false);
        require(SUCCEEDED(manager->SetFocus(document)),
                "second focus switch failed");
        require(SUCCEEDED(events->OnSetFocus(document, otherDocument)),
                "second focus callback failed");
        pump(200);
        require(store->text == hello && otherStore->text.empty(),
                "late edit reached the wrong context");
        require(SUCCEEDED(service->Deactivate()), "TIP deactivation failed");
        manager->SetFocus(nullptr);
        document->Pop(TF_POPF_ALL);
        otherDocument->Pop(TF_POPF_ALL);
        pump();
        manager->Deactivate();
        attributes.Release();
        events.Release();
        compositionSink.Release();
        keys.Release();
        service.Release();
        factory.Release();
        require(canUnload() == S_FALSE,
                "display attribute objects did not keep DLL loaded");
        attributeInfo.Release();
        attributeEnumerator.Release();
        require(canUnload() == S_OK, "TIP/edit session reference leak");
        std::cout << "PASS: actual DLL factory, TSF context, Chinese commit, "
                     "async edits, cancellation and focus isolation (key-sink "
                     "adapter, no registration)\n";
        exit = 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
    }
    if (exit == 0)
        FreeLibrary(module);
    CoUninitialize();
    return exit;
}
