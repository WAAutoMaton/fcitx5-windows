#pragma once

#include "../ipc/protocol.h"
#include <atlcomcli.h>
#include <functional>
#include <memory>
#include <msctf.h>

namespace fcitx {

class CandidateUIElement : public ITfCandidateListUIElementBehavior {
  public:
    using Action =
        std::function<HRESULT(win32::ipc::CandidateAction, uint64_t, uint32_t)>;
    explicit CandidateUIElement(std::function<void(bool)> show,
                                Action action = {});
    ~CandidateUIElement();
    STDMETHODIMP QueryInterface(REFIID id, void **result) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP GetDescription(BSTR *description) override;
    STDMETHODIMP GetGUID(GUID *guid) override;
    STDMETHODIMP Show(BOOL show) override;
    STDMETHODIMP IsShown(BOOL *shown) override;
    STDMETHODIMP GetUpdatedFlags(DWORD *flags) override;
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr **document) override;
    STDMETHODIMP GetCount(UINT *count) override;
    STDMETHODIMP GetSelection(UINT *selection) override;
    STDMETHODIMP GetString(UINT index, BSTR *text) override;
    STDMETHODIMP GetPageIndex(UINT *indices, UINT capacity,
                              UINT *count) override;
    STDMETHODIMP SetPageIndex(UINT *indices, UINT count) override;
    STDMETHODIMP GetCurrentPage(UINT *page) override;
    STDMETHODIMP SetSelection(UINT index) override;
    STDMETHODIMP Finalize() override;
    STDMETHODIMP Abort() override;

    DWORD publish(const win32::ipc::KeyReply &reply, ITfDocumentMgr *document);
    void setVisible(bool visible) { visible_ = visible; }
    uint64_t showSerial() const { return showSerial_; }
    void detach();

  private:
    struct Snapshot {
        CComPtr<ITfDocumentMgr> document;
        std::vector<std::wstring> strings;
        UINT selection = UINT_MAX;
        std::vector<UINT> pages;
        uint64_t generation = 0;
    };
    LONG references_ = 1;
    std::shared_ptr<const Snapshot> snapshot_;
    DWORD updated_ = 0;
    bool visible_ = false;
    uint64_t showSerial_ = 0;
    std::function<void(bool)> show_;
    Action action_;
    UINT pendingSelection_ = UINT_MAX;
    uint64_t pendingGeneration_ = 0;
};

} // namespace fcitx
