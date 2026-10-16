#pragma once

#include <algorithm>
#include <atlcomcli.h>
#include <inputscope.h>
#include <msctf.h>
#include <string>
#include <textstor.h>

namespace fcitx::test {

inline constexpr GUID kInputScopeAttribute = {
    0x1713dd5a,
    0x68e7,
    0x4a5b,
    {0x9a, 0xf6, 0x59, 0x2a, 0x59, 0x5c, 0x77, 0x8d}};

class TextStore : public ITextStoreACP, public ITfInputScope {
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
    bool denyReadSynchronous = false;
    ULONG writes = 0;
    ULONG readLocks = 0;
    ULONG geometryCalls = 0;
    HRESULT geometryResult = S_OK;
    HRESULT screenResult = S_OK;
    RECT screenRectangle{0, 0, 400, 200};
    LONG caretWidth = 0;
    bool clipComposition = false;
    bool clipCaret = false;
    bool emptyRectangle = false;
    bool nullWindow = false;
    HWND windowOverride = nullptr;
    bool readOnly = false;
    bool suppliesScope = false;
    InputScope inputScope = IS_DEFAULT;
    HRESULT scopeResult = S_OK;
    void setScope(InputScope scope) {
        suppliesScope = true;
        inputScope = scope;
        if (sink_)
            sink_->OnAttrsChange(0, static_cast<LONG>(text.size()), 1,
                                 &kInputScopeAttribute);
    }
    LONG anchorX = 10;
    HWND window() const { return window_; }
    const TS_SELECTION_ACP &selection() const { return selection_; }
    HRESULT notifyLayout() {
        return sink_ ? sink_->OnLayoutChange(TS_LC_CHANGE, 1) : E_FAIL;
    }
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result)
            return E_INVALIDARG;
        *result = nullptr;
        if (id == __uuidof(ITfInputScope)) {
            *result = static_cast<ITfInputScope *>(this);
            AddRef();
            return S_OK;
        }
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
        const bool deny = (flags & TS_LF_READWRITE) == TS_LF_READWRITE
                              ? denySynchronous
                              : denyReadSynchronous;
        if (lock_ || (deny && (flags & TS_LF_SYNC))) {
            *result = TS_E_SYNCHRONOUS;
            return S_OK;
        }
        lock_ = flags;
        if ((flags & TS_LF_READWRITE) != TS_LF_READWRITE)
            ++readLocks;
        *result = sink_->OnLockGranted(flags);
        lock_ = 0;
        return S_OK;
    }
    STDMETHODIMP GetStatus(TS_STATUS *status) override {
        if (!status)
            return E_INVALIDARG;
        *status = {readOnly ? static_cast<DWORD>(TS_SD_READONLY) : 0UL,
                   TS_SS_NOHIDDENTEXT};
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
        selection_.acpStart =
            std::min(selection_.acpStart, static_cast<LONG>(text.size()));
        selection_.acpEnd =
            std::min(selection_.acpEnd, static_cast<LONG>(text.size()));
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
    STDMETHODIMP RequestSupportedAttrs(DWORD, ULONG count,
                                       const TS_ATTRID *attributes) override {
        return RequestAttrsAtPosition(0, count, attributes, 0);
    }
    STDMETHODIMP RequestAttrsAtPosition(LONG, ULONG count,
                                        const TS_ATTRID *attributes,
                                        DWORD) override {
        scopeRequested_ = false;
        for (ULONG i = 0; i < count; ++i)
            if (suppliesScope && attributes[i] == kInputScopeAttribute)
                scopeRequested_ = true;
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
    STDMETHODIMP RetrieveRequestedAttrs(ULONG capacity, TS_ATTRVAL *attributes,
                                        ULONG *fetched) override {
        if (!fetched)
            return E_INVALIDARG;
        *fetched = 0;
        if (scopeRequested_ && capacity && attributes) {
            attributes[0].idAttr = kInputScopeAttribute;
            attributes[0].dwOverlapId = 0;
            VariantInit(&attributes[0].varValue);
            attributes[0].varValue.vt = VT_UNKNOWN;
            attributes[0].varValue.punkVal = static_cast<ITfInputScope *>(this);
            AddRef();
            *fetched = 1;
        }
        scopeRequested_ = false;
        return S_OK;
    }
    STDMETHODIMP GetInputScopes(InputScope **scopes, UINT *count) override {
        if (!scopes || !count)
            return E_INVALIDARG;
        *scopes = nullptr;
        *count = 0;
        if (FAILED(scopeResult))
            return scopeResult;
        *scopes = static_cast<InputScope *>(CoTaskMemAlloc(sizeof(InputScope)));
        if (!*scopes)
            return E_OUTOFMEMORY;
        **scopes = inputScope;
        *count = 1;
        return S_OK;
    }
    STDMETHODIMP GetPhrase(BSTR **phrases, UINT *count) override {
        if (!phrases || !count)
            return E_INVALIDARG;
        *phrases = nullptr;
        *count = 0;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetRegularExpression(BSTR *text) override {
        if (text)
            *text = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetSRGS(BSTR *text) override {
        if (text)
            *text = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetXML(BSTR *text) override {
        if (text)
            *text = nullptr;
        return E_NOTIMPL;
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
        if (!lock_)
            return TS_E_NOLOCK;
        ++geometryCalls;
        *rect = emptyRectangle
                    ? RECT{}
                    : RECT{anchorX + start * 10, 10,
                           anchorX + end * 10 + (start == end ? caretWidth : 0),
                           30};
        *clipped = start == end ? clipCaret : clipComposition;
        return geometryResult;
    }
    STDMETHODIMP GetScreenExt(TsViewCookie, RECT *rect) override {
        if (!rect)
            return E_INVALIDARG;
        *rect = screenRectangle;
        return screenResult;
    }
    STDMETHODIMP GetWnd(TsViewCookie, HWND *window) override {
        if (!window)
            return E_INVALIDARG;
        *window = nullWindow       ? nullptr
                  : windowOverride ? windowOverride
                                   : window_;
        return S_OK;
    }

  private:
    bool valid(LONG start, LONG end) const {
        return start >= 0 && end >= start &&
               end <= static_cast<LONG>(text.size());
    }
    LONG references_ = 1;
    DWORD lock_ = 0;
    bool scopeRequested_ = false;
    TS_SELECTION_ACP selection_{0, 0, {TS_AE_NONE, FALSE}};
    CComPtr<ITextStoreACPSink> sink_;
    HWND window_ = nullptr;
};

} // namespace fcitx::test
