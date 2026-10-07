#pragma once

#include <atlcomcli.h>
#include <msctf.h>
#include <string>
#include "pipeclient.h"

namespace fcitx {
class Tsf : public ITfTextInputProcessorEx,
            public ITfThreadMgrEventSink,
            public ITfTextEditSink,
            public ITfKeyEventSink,
            public ITfCompositionSink,
            public ITfEditSession {
  public:
    Tsf();
    ~Tsf();

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP Activate(ITfThreadMgr *pThreadMgr,
                          TfClientId tfClientId) override;
    STDMETHODIMP Deactivate() override;
    STDMETHODIMP ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId,
                            DWORD dwFlags) override;

    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr *pDocMgr) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr *pDocMgr) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr *pDocMgrFocus,
                            ITfDocumentMgr *pDocMgrPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext *pContext) override;
    STDMETHODIMP OnPopContext(ITfContext *pContext) override;

    STDMETHODIMP OnEndEdit(ITfContext *pic, TfEditCookie ecReadOnly,
                           ITfEditRecord *pEditRecord) override;

    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext *pContext, WPARAM wParam,
                               LPARAM lParam, BOOL *pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                           BOOL *pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext *pContext, WPARAM wParam,
                             LPARAM lParam, BOOL *pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                         BOOL *pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext *pContext, REFGUID rguid,
                                BOOL *pfEaten) override;

    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite,
                                         ITfComposition *pComposition) override;
    STDMETHODIMP DoEditSession(TfEditCookie ec) override;

  private:
    bool initThreadMgrEventSink();
    void uninitThreadMgrEventSink();
    bool initTextEditSink(CComPtr<ITfDocumentMgr> documentMgr);
    bool initKeyEventSink();
    void uninitKeyEventSink();
    bool initRemoteContext();
    void clearRemoteContext();
    BOOL processKey(ITfContext *context, WPARAM wParam, LPARAM lParam,
                    bool release);

    LONG refCount_ = 1;
    CComPtr<ITfThreadMgr> threadMgr_;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    DWORD threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;
    DWORD textEditSinkCookie_ = TF_INVALID_COOKIE;
    CComPtr<ITfContext> textEditSinkContext_;
    uint64_t remoteContextId_ = 0;
    PipeClient pipe_;

    CComPtr<ITfContext> pendingEditContext_;
    CComPtr<ITfComposition> composition_;
    std::string pendingCommit_;
    std::string pendingPreedit_;
    uint32_t pendingPreeditCursor_ = 0;
};
} // namespace fcitx