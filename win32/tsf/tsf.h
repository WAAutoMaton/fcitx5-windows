#pragma once

#include "candidatewindow.h"
#include "pipeclient.h"
#include <atlcomcli.h>
#include <bitset>
#include <deque>
#include <msctf.h>
#include <string>

namespace fcitx {
class Tsf : public ITfTextInputProcessorEx,
            public ITfThreadMgrEventSink,
            public ITfTextEditSink,
            public ITfKeyEventSink,
            public ITfCompositionSink,
            public ITfDisplayAttributeProvider {
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
    STDMETHODIMP OnTestKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                             BOOL *pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                         BOOL *pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext *pContext, REFGUID rguid,
                                BOOL *pfEaten) override;

    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite,
                                         ITfComposition *pComposition) override;
    STDMETHODIMP
    EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo **result) override;
    STDMETHODIMP
    GetDisplayAttributeInfo(REFGUID guid,
                            ITfDisplayAttributeInfo **result) override;

  private:
    friend class SnapshotEditSession;
    HRESULT initThreadMgrEventSink();
    void uninitThreadMgrEventSink();
    bool initTextEditSink(CComPtr<ITfDocumentMgr> documentMgr);
    HRESULT initKeyEventSink();
    void uninitKeyEventSink();
    bool initRemoteContext();
    void clearRemoteContext();
    BOOL processKey(ITfContext *context, WPARAM wParam, LPARAM lParam,
                    bool release, uint32_t modifiers = UINT32_MAX);
    bool submitSnapshot(PipeClient::KeyReply reply, bool synchronous);
    bool requestNextEdit(bool synchronous);
    HRESULT applySnapshot(TfEditCookie cookie, ITfContext *context,
                          const PipeClient::KeyReply &reply,
                          uint64_t generation);
    void finishEdit(uint64_t generation, HRESULT result);
    void cancelComposition();
    void pollState();
    bool initMessageWindow();
    static LRESULT CALLBACK messageWindowProc(HWND window, UINT message,
                                              WPARAM wParam, LPARAM lParam);

    LONG refCount_ = 1;
    CComPtr<ITfThreadMgr> threadMgr_;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    DWORD threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;
    DWORD textEditSinkCookie_ = TF_INVALID_COOKIE;
    CComPtr<ITfContext> textEditSinkContext_;
    uint64_t remoteContextId_ = 0;
    PipeClient pipe_;

    CComPtr<ITfComposition> composition_;
    struct EditOperation {
        CComPtr<ITfContext> context;
        PipeClient::KeyReply reply;
        uint64_t generation;
    };
    std::deque<EditOperation> edits_;
    bool editRequested_ = false;
    uint64_t generation_ = 0;
    PipeClient::KeyReply state_;
    std::string displayedPreedit_;
    uint32_t displayedCursor_ = 0;
    std::bitset<256> handledKeys_;
    CandidateWindow candidates_;
    HWND messageWindow_ = nullptr;
    ULONGLONG nextReconnect_ = 0;
    bool foreground_ = true;
    TfGuidAtom attributeAtom_ = TF_INVALID_GUIDATOM;
    bool keyEventSinkAdvised_ = false;
    bool modeSwitchPreserved_ = false;
};
} // namespace fcitx
