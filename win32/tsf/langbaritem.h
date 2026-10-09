#pragma once

#include <atlcomcli.h>
#include <ctffunc.h>
#include <ctfutb.h>

namespace fcitx {

class LangBarItem : public ITfLangBarItemButton, public ITfSource {
  public:
    static constexpr UINT kToggleMessage = WM_APP + 3;
    LangBarItem(HWND dispatchWindow, bool chineseMode);
    ~LangBarItem();

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO *pInfo) override;
    STDMETHODIMP GetStatus(DWORD *pdwStatus) override;
    STDMETHODIMP Show(BOOL fShow) override;
    STDMETHODIMP GetTooltipString(BSTR *pbstrToolTip) override;

    STDMETHODIMP OnClick(TfLBIClick click, POINT pt,
                         const RECT *prcArea) override;
    STDMETHODIMP InitMenu(ITfMenu *pMenu) override;
    STDMETHODIMP OnMenuSelect(UINT wID) override;
    STDMETHODIMP GetIcon(HICON *phIcon) override;
    STDMETHODIMP GetText(BSTR *pbstrText) override;

    STDMETHODIMP AdviseSink(REFIID riid, IUnknown *punk,
                            DWORD *pdwCookie) override;
    STDMETHODIMP UnadviseSink(DWORD dwCookie) override;

    void setMode(bool chineseMode);
    void detach();

  private:
    HICON createIcon() const;
    void notify(DWORD flags);

    LONG refCount_ = 1;
    HWND dispatchWindow_ = nullptr;
    bool chineseMode_ = true;
    CComPtr<ITfLangBarItemSink> sink_;
    DWORD sinkCookie_ = TF_INVALID_COOKIE;
    DWORD nextSinkCookie_ = 0;
    bool shown_ = true;
};

} // namespace fcitx
