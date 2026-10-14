#pragma once

#include "../i18n/translator.h"
#include "auxiliaryuielement.h"
#include <atlcomcli.h>
#include <ctffunc.h>
#include <ctfutb.h>
#include <string>

namespace fcitx {

class LangBarItem : public ITfLangBarItemButton, public ITfSource {
  public:
    static constexpr UINT kToggleMessage = WM_APP + 3;
    static constexpr UINT kSettingsMessage = WM_APP + 4;
    static constexpr UINT kRestartMessage = WM_APP + 5;
    static constexpr UINT kStopMessage = WM_APP + 6;
    // WM_APP + 7..9 are owned by Tsf's service/candidate/mode dispatch.
    // Keep language-bar commands in a separate range so messageWindowProc
    // cannot mistake them for internal notifications.
    static constexpr UINT kUserDataMessage = WM_APP + 10;
    static constexpr UINT kAboutMessage = WM_APP + 11;
    static constexpr UINT kSettingsMenuId = 1;
    static constexpr UINT kRestartMenuId = 2;
    static constexpr UINT kStopMenuId = 3;
    static constexpr UINT kUserDataMenuId = 4;
    static constexpr UINT kAboutMenuId = 5;
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
    void setAvailable(bool available);
    void setInputMethod(bool enabled, std::wstring name, std::wstring label,
                        std::wstring icon = {});
    void detach();
    void setTranslation(win32::i18n::Translation translation);
    void setUiPolicy(bool systemMenu, bool restricted) {
        systemMenu_ = systemMenu;
        restricted_ = restricted;
    }
    void setUiManager(ITfUIElementMgr *manager) { uiManager_ = manager; }

  private:
    HICON createIcon() const;
    void notify(DWORD flags);

    LONG refCount_ = 1;
    HWND dispatchWindow_ = nullptr;
    bool chineseMode_ = true;
    std::wstring methodName_;
    std::wstring iconLabel_;
    std::wstring iconPath_;
    CComPtr<ITfLangBarItemSink> sink_;
    DWORD sinkCookie_ = TF_INVALID_COOKIE;
    DWORD nextSinkCookie_ = 0;
    bool shown_ = true;
    bool available_ = true;
    bool systemMenu_ = false;
    bool restricted_ = false;
    CComPtr<ITfUIElementMgr> uiManager_;
    win32::i18n::Translation translation_;
};

} // namespace fcitx
