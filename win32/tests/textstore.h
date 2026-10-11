#pragma once

#include <algorithm>
#include <atlcomcli.h>
#include <msctf.h>
#include <string>
#include <textstor.h>

namespace fcitx::test {

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
    ULONG readLocks = 0;
    ULONG geometryCalls = 0;
    HRESULT geometryResult = S_OK;
    bool clipComposition = false;
    bool clipCaret = false;
    bool emptyRectangle = false;
    LONG anchorX = 10;
    HWND window() const { return window_; }
    HRESULT notifyLayout() {
        return sink_ ? sink_->OnLayoutChange(TS_LC_CHANGE, 1) : E_FAIL;
    }
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
        if ((flags & TS_LF_READWRITE) != TS_LF_READWRITE)
            ++readLocks;
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
        if (!lock_)
            return TS_E_NOLOCK;
        ++geometryCalls;
        *rect = emptyRectangle
                    ? RECT{}
                    : RECT{anchorX + start * 10, 10, anchorX + end * 10, 30};
        *clipped = start == end ? clipCaret : clipComposition;
        return geometryResult;
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

} // namespace fcitx::test
