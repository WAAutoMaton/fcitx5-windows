#include "langbaritem.h"
#include "../dll/util.h"

#include <algorithm>
#include <cwchar>
#include <olectl.h>

extern void DllAddRef();
extern void DllRelease();

namespace fcitx {
namespace {
constexpr wchar_t kDescription[] = L"Fcitx5 input mode";
constexpr wchar_t kTooltipChinese[] = L"Fcitx5 Pinyin - Chinese input";
constexpr wchar_t kTooltipEnglish[] = L"Fcitx5 Pinyin - English input";
struct MenuCommand {
    UINT id;
    UINT message;
    const wchar_t *text;
};
constexpr MenuCommand kCommands[] = {
    {LangBarItem::kSettingsMenuId, LangBarItem::kSettingsMessage,
     L"\u8f93\u5165\u6cd5\u8bbe\u7f6e"},
    {LangBarItem::kRestartMenuId, LangBarItem::kRestartMessage,
     L"\u91cd\u542f\u670d\u52a1"},
    {LangBarItem::kStopMenuId, LangBarItem::kStopMessage,
     L"\u5173\u95ed\u670d\u52a1"}};

HICON makeTextIcon(const wchar_t *text) {
    const auto baseSize = GetSystemMetrics(SM_CXSMICON);
    const auto size = std::max(16, baseSize > 0 ? baseSize : 16);
    HDC screen = GetDC(nullptr);
    if (!screen) {
        return nullptr;
    }
    HDC maskDc = CreateCompatibleDC(screen);
    // Monochrome icons store the AND mask above the XOR mask.
    HBITMAP mask = CreateBitmap(size, size * 2, 1, 1, nullptr);
    if (!maskDc || !mask) {
        if (maskDc) {
            DeleteDC(maskDc);
        }
        if (mask) {
            DeleteObject(mask);
        }
        ReleaseDC(nullptr, screen);
        return nullptr;
    }
    const auto oldMask = SelectObject(maskDc, mask);
    RECT bounds{0, 0, size, size};
    PatBlt(maskDc, 0, 0, size, size, WHITENESS);
    PatBlt(maskDc, 0, size, size, size, BLACKNESS);

    SetBkMode(maskDc, TRANSPARENT);
    SetTextColor(maskDc, RGB(0, 0, 0));
    auto font = CreateFontW(-(size - 2), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    const auto oldFont =
        SelectObject(maskDc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    const auto drawn =
        DrawTextW(maskDc, text, -1, &bounds,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(maskDc, oldFont);
    if (font) {
        DeleteObject(font);
    }
    SelectObject(maskDc, oldMask);

    HICON result = nullptr;
    if (drawn) {
        ICONINFO info{};
        info.fIcon = TRUE;
        info.xHotspot = 0;
        info.yHotspot = 0;
        info.hbmMask = mask;
        result = CreateIconIndirect(&info);
    }
    DeleteObject(mask);
    DeleteDC(maskDc);
    ReleaseDC(nullptr, screen);
    return result;
}
} // namespace

LangBarItem::LangBarItem(HWND dispatchWindow, bool chineseMode)
    : dispatchWindow_(dispatchWindow), chineseMode_(chineseMode) {
    DllAddRef();
}

LangBarItem::~LangBarItem() { DllRelease(); }

STDMETHODIMP LangBarItem::QueryInterface(REFIID riid, void **ppvObject) {
    if (!ppvObject) {
        return E_INVALIDARG;
    }
    *ppvObject = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfLangBarItem) ||
        IsEqualIID(riid, IID_ITfLangBarItemButton)) {
        *ppvObject = static_cast<ITfLangBarItemButton *>(this);
    } else if (IsEqualIID(riid, IID_ITfSource)) {
        *ppvObject = static_cast<ITfSource *>(this);
    }
    if (!*ppvObject) {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) LangBarItem::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&refCount_));
}

STDMETHODIMP_(ULONG) LangBarItem::Release() {
    const auto result = InterlockedDecrement(&refCount_);
    if (result == 0) {
        delete this;
    }
    return static_cast<ULONG>(result);
}

STDMETHODIMP LangBarItem::GetInfo(TF_LANGBARITEMINFO *pInfo) {
    if (!pInfo) {
        return E_INVALIDARG;
    }
    *pInfo = {};
    pInfo->clsidService = FCITX_CLSID;
    pInfo->guidItem = GUID_LBI_INPUTMODE;
    // Declare both behaviors so the language bar routes left clicks to the
    // toggle and right clicks through InitMenu/OnMenuSelect.
    pInfo->dwStyle = TF_LBI_STYLE_BTN_MENU | TF_LBI_STYLE_BTN_TOGGLE |
                     TF_LBI_STYLE_HIDDENSTATUSCONTROL |
                     TF_LBI_STYLE_TEXTCOLORICON;
    pInfo->ulSort = 0;
    wcsncpy_s(pInfo->szDescription, kDescription, _TRUNCATE);
    return S_OK;
}

STDMETHODIMP LangBarItem::GetStatus(DWORD *pdwStatus) {
    if (!pdwStatus) {
        return E_INVALIDARG;
    }
    *pdwStatus = shown_ ? 0 : TF_LBI_STATUS_HIDDEN;
    if (chineseMode_) {
        *pdwStatus |= TF_LBI_STATUS_BTN_TOGGLED;
    }
    return S_OK;
}

STDMETHODIMP LangBarItem::Show(BOOL fShow) {
    const auto shown = fShow != FALSE;
    if (shown_ != shown) {
        shown_ = shown;
        notify(TF_LBI_STATUS);
    }
    return S_OK;
}

STDMETHODIMP LangBarItem::GetTooltipString(BSTR *pbstrToolTip) {
    if (!pbstrToolTip) {
        return E_INVALIDARG;
    }
    *pbstrToolTip =
        SysAllocString(chineseMode_ ? kTooltipChinese : kTooltipEnglish);
    return *pbstrToolTip ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarItem::OnClick(TfLBIClick click, POINT point, const RECT *) {
    if (click == TF_LBI_CLK_LEFT && dispatchWindow_) {
        return PostMessageW(dispatchWindow_, kToggleMessage, 0, 0) ? S_OK
                                                                   : E_FAIL;
    }
    if (click == TF_LBI_CLK_RIGHT && dispatchWindow_) {
        CComPtr<LangBarItem> lifetime(this);
        const auto menu = CreatePopupMenu();
        if (!menu) {
            return E_OUTOFMEMORY;
        }
        const auto owner =
            CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP, point.x,
                            point.y, 0, 0, nullptr, nullptr, nullptr, nullptr);
        bool populated = owner != nullptr;
        for (const auto &command : kCommands) {
            populated = populated &&
                        AppendMenuW(menu, MF_STRING, command.id, command.text);
        }
        if (!populated) {
            if (owner) {
                DestroyWindow(owner);
            }
            DestroyMenu(menu);
            return E_FAIL;
        }
        SetForegroundWindow(owner);
        const auto selected = TrackPopupMenuEx(
            menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x,
            point.y, owner, nullptr);
        PostMessageW(owner, WM_NULL, 0, 0);
        DestroyMenu(menu);
        DestroyWindow(owner);
        if (selected) {
            return OnMenuSelect(selected);
        }
    }
    return S_OK;
}

STDMETHODIMP LangBarItem::InitMenu(ITfMenu *menu) {
    if (!menu) {
        return E_INVALIDARG;
    }
    for (const auto &command : kCommands) {
        const auto result = menu->AddMenuItem(
            command.id, 0, nullptr, nullptr, command.text,
            static_cast<ULONG>(wcslen(command.text)), nullptr);
        if (FAILED(result)) {
            return result;
        }
    }
    return S_OK;
}

STDMETHODIMP LangBarItem::OnMenuSelect(UINT id) {
    for (const auto &command : kCommands) {
        if (id == command.id) {
            return !dispatchWindow_ ||
                           PostMessageW(dispatchWindow_, command.message, 0, 0)
                       ? S_OK
                       : E_FAIL;
        }
    }
    return E_INVALIDARG;
}

STDMETHODIMP LangBarItem::GetIcon(HICON *phIcon) {
    if (!phIcon) {
        return E_INVALIDARG;
    }
    *phIcon = createIcon();
    return *phIcon ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarItem::GetText(BSTR *pbstrText) {
    if (!pbstrText) {
        return E_INVALIDARG;
    }
    *pbstrText = SysAllocString(chineseMode_ ? L"\u4e2d" : L"A");
    return *pbstrText ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarItem::AdviseSink(REFIID riid, IUnknown *punk,
                                     DWORD *pdwCookie) {
    if (!pdwCookie) {
        return E_INVALIDARG;
    }
    *pdwCookie = TF_INVALID_COOKIE;
    if (!punk) {
        return E_INVALIDARG;
    }
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink)) {
        return CONNECT_E_CANNOTCONNECT;
    }
    if (sink_) {
        return CONNECT_E_ADVISELIMIT;
    }
    CComPtr<ITfLangBarItemSink> sink;
    const auto result = punk->QueryInterface(IID_PPV_ARGS(&sink));
    if (FAILED(result)) {
        return result;
    }
    sink_ = sink;
    do {
        sinkCookie_ = ++nextSinkCookie_;
    } while (sinkCookie_ == TF_INVALID_COOKIE);
    *pdwCookie = sinkCookie_;
    return S_OK;
}

STDMETHODIMP LangBarItem::UnadviseSink(DWORD dwCookie) {
    if (dwCookie != sinkCookie_ || !sink_) {
        return CONNECT_E_NOCONNECTION;
    }
    sink_.Release();
    sinkCookie_ = TF_INVALID_COOKIE;
    return S_OK;
}

void LangBarItem::notify(DWORD flags) {
    const auto sink = sink_;
    if (sink) {
        sink->OnUpdate(flags);
    }
}

void LangBarItem::setMode(bool chineseMode) {
    if (chineseMode_ != chineseMode) {
        chineseMode_ = chineseMode;
        notify(TF_LBI_BTNALL | TF_LBI_STATUS);
    }
}

void LangBarItem::detach() {
    dispatchWindow_ = nullptr;
    shown_ = false;
    notify(TF_LBI_STATUS);
    sink_.Release();
    sinkCookie_ = TF_INVALID_COOKIE;
}

HICON LangBarItem::createIcon() const {
    return makeTextIcon(chineseMode_ ? L"\u4e2d" : L"A");
}

} // namespace fcitx
