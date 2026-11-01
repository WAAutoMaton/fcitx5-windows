#include "langbaritem.h"
#include "../dll/util.h"
#include "inputmethodicon.h"

#include <algorithm>
#include <cwchar>
#include <olectl.h>

extern void DllAddRef();
extern void DllRelease();

namespace fcitx {
namespace {
constexpr wchar_t kDescription[] = L"Fcitx5 input mode";
constexpr auto kTooltipChinese =
    win32::i18n::mark("menu", "Fcitx5 Pinyin - Chinese input");
constexpr auto kTooltipEnglish =
    win32::i18n::mark("menu", "Fcitx5 Pinyin - English input");
struct MenuCommand {
    UINT id;
    UINT message;
    const char *text;
};
constexpr MenuCommand kCommands[] = {
    {LangBarItem::kSettingsMenuId, LangBarItem::kSettingsMessage,
     win32::i18n::mark("menu", "Input Method Settings")},
    {LangBarItem::kUserDataMenuId, LangBarItem::kUserDataMessage,
     win32::i18n::mark("menu", "User Data Folder")},
    {LangBarItem::kRestartMenuId, LangBarItem::kRestartMessage,
     win32::i18n::mark("menu", "Restart Services")},
    {LangBarItem::kStopMenuId, LangBarItem::kStopMessage,
     win32::i18n::mark("menu", "Stop Services")},
    {LangBarItem::kAboutMenuId, LangBarItem::kAboutMessage,
     win32::i18n::mark("menu", "About")}};

HRESULT renderStatusMenu(const StatusMenuView &view, HMENU native,
                         ITfMenu *system, uint32_t parent = 0) {
    const auto cache = view.cache();
    if (!cache)
        return S_OK;
    for (const auto &node : cache->reply.nodes) {
        if (node.parentId != parent)
            continue;
        const auto text = statusMenuText(node);
        const auto id = view.command(node.nodeId);
        const bool submenu =
            node.kind == win32::ipc::StatusMenuNodeKind::Submenu;
        const bool separator =
            node.kind == win32::ipc::StatusMenuNodeKind::Separator;
        if (system) {
            DWORD flags =
                (submenu ? TF_LBMENUF_SUBMENU : 0) |
                (separator ? TF_LBMENUF_SEPARATOR : 0) |
                (!node.enabled && !separator ? TF_LBMENUF_GRAYED : 0) |
                (node.checked ? TF_LBMENUF_CHECKED : 0);
            CComPtr<ITfMenu> child;
            const auto hr = system->AddMenuItem(
                id, flags, nullptr, nullptr, text.c_str(),
                static_cast<ULONG>(text.size()), submenu ? &child : nullptr);
            if (FAILED(hr))
                return hr;
            if (submenu) {
                if (!child)
                    return E_FAIL;
                const auto nested =
                    renderStatusMenu(view, nullptr, child, node.nodeId);
                if (FAILED(nested))
                    return nested;
            }
        } else {
            HMENU child = submenu ? CreatePopupMenu() : nullptr;
            if (submenu && !child)
                return E_OUTOFMEMORY;
            if (child) {
                const auto hr =
                    renderStatusMenu(view, child, nullptr, node.nodeId);
                if (FAILED(hr)) {
                    DestroyMenu(child);
                    return hr;
                }
            }
            const UINT flags = (submenu ? MF_POPUP : MF_STRING) |
                               (separator ? MF_SEPARATOR : 0) |
                               (!node.enabled && !separator ? MF_GRAYED : 0) |
                               (node.checked ? MF_CHECKED : 0);
            if (!AppendMenuW(native, flags,
                             submenu ? reinterpret_cast<UINT_PTR>(child) : id,
                             text.c_str())) {
                if (child)
                    DestroyMenu(child);
                return E_FAIL;
            }
        }
    }
    return S_OK;
}
struct PopupResources {
    HMENU menu = CreatePopupMenu();
    HWND auxiliary = nullptr;
    ~PopupResources() {
        if (menu)
            DestroyMenu(menu);
        if (auxiliary)
            DestroyWindow(auxiliary);
    }
};
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
                     TF_LBI_STYLE_HIDDENSTATUSCONTROL;
    pInfo->ulSort = 0;
    wcsncpy_s(pInfo->szDescription, kDescription, _TRUNCATE);
    return S_OK;
}

STDMETHODIMP LangBarItem::GetStatus(DWORD *pdwStatus) {
    if (!pdwStatus) {
        return E_INVALIDARG;
    }
    *pdwStatus = shown_ ? 0 : TF_LBI_STATUS_HIDDEN;
    if (!available_)
        *pdwStatus |= TF_LBI_STATUS_DISABLED;
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
    *pbstrToolTip = nullptr;
    try {
        const win32::i18n::Translator english;
        const auto snapshot = translation_;
        const auto text =
            methodName_.empty()
                ? (snapshot ? *snapshot : english)
                      .wide("menu",
                            chineseMode_ ? kTooltipChinese : kTooltipEnglish)
                : methodName_;
        *pbstrToolTip = SysAllocString(text.c_str());
    } catch (...) {
        return E_OUTOFMEMORY;
    }
    return *pbstrToolTip ? S_OK : E_OUTOFMEMORY;
}

std::shared_ptr<StatusMenuView> LangBarItem::beginStatusMenu() {
    statusIds_.clear();
    const bool usable = statusContext_ && !restricted_ && dispatchWindow_;
    statusIds_.open(usable ? statusCache_ : nullptr);
    statusView_ = std::make_shared<StatusMenuView>(statusIds_);
    if (usable && (!statusCache_ || statusCache_->reply.error !=
                                        win32::ipc::StatusMenuError::None))
        PostMessageW(dispatchWindow_, kStatusMenuMessage, 0, 0);
    return statusView_;
}
void LangBarItem::invalidateStatusMenu() {
    statusCache_.reset();
    statusView_.reset();
    statusIds_.clear();
    statusContext_ = false;
    if (ownMenuOpen_)
        EndMenu();
}
STDMETHODIMP LangBarItem::OnClick(TfLBIClick click, POINT point, const RECT *) {
    if (click == TF_LBI_CLK_LEFT && dispatchWindow_)
        return PostMessageW(dispatchWindow_, kToggleMessage, 0, 0) ? S_OK
                                                                   : E_FAIL;
    if (click != TF_LBI_CLK_RIGHT || !dispatchWindow_ || systemMenu_)
        return S_OK;
    CComPtr<LangBarItem> lifetime(this);
    try {
        const auto view = beginStatusMenu();
        AuxiliaryUI permission;
        if (!permission.begin(uiManager_, L"Fcitx5 menu", [] { EndMenu(); }))
            return S_OK;
        PopupResources resources;
        if (!resources.menu)
            return E_OUTOFMEMORY;
        HWND owner = GetForegroundWindow();
        DWORD process = 0;
        const auto thread = GetWindowThreadProcessId(owner, &process);
        if (!owner || thread != GetCurrentThreadId() ||
            process != GetCurrentProcessId()) {
            resources.auxiliary = CreateWindowExW(
                WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"", WS_POPUP,
                point.x, point.y, 0, 0, nullptr, nullptr, nullptr, nullptr);
            owner = resources.auxiliary;
        }
        if (!owner)
            return E_FAIL;
        auto hr = renderStatusMenu(*view, resources.menu, nullptr);
        if (FAILED(hr)) {
            statusView_.reset();
            return hr;
        }
        const auto translation = translation_;
        const win32::i18n::Translator english;
        const auto &tr = translation ? *translation : english;
        const bool nodes = view->cache() && !view->cache()->reply.nodes.empty();
        if (statusContext_ && !restricted_ && !view->cache()) {
            const auto text = tr.wide(
                "menu", statusCache_ ? "Unable to read input method menu"
                                     : "Reading input method menu...");
            if (!AppendMenuW(resources.menu, MF_STRING | MF_GRAYED, 0,
                             text.c_str()))
                return E_FAIL;
        }
        if (nodes || (statusContext_ && !restricted_ && !view->cache()))
            if (!AppendMenuW(resources.menu, MF_SEPARATOR, 0, nullptr))
                return E_FAIL;
        for (const auto &command : kCommands) {
            if (restricted_ && command.id != kSettingsMenuId)
                continue;
            const auto text = tr.wide("menu", command.text);
            if (!AppendMenuW(resources.menu, MF_STRING, command.id,
                             text.c_str()))
                return E_FAIL;
        }
        ownMenuOpen_ = true;
        permission.setVisible(true);
        const auto selected = TrackPopupMenuEx(
            resources.menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            point.x, point.y, owner, nullptr);
        ownMenuOpen_ = false;
        permission.reset();
        hr = selected && statusView_ == view ? OnMenuSelect(selected) : S_OK;
        if (statusView_ == view)
            statusView_.reset();
        return hr;
    } catch (...) {
        ownMenuOpen_ = false;
        statusView_.reset();
        return E_OUTOFMEMORY;
    }
}
STDMETHODIMP LangBarItem::InitMenu(ITfMenu *menu) {
    if (!menu)
        return E_INVALIDARG;
    CComPtr<LangBarItem> lifetime(this);
    try {
        const auto view = beginStatusMenu();
        auto hr = renderStatusMenu(*view, nullptr, menu);
        if (FAILED(hr)) {
            statusView_.reset();
            return hr;
        }
        const auto translation = translation_;
        const win32::i18n::Translator english;
        const auto &tr = translation ? *translation : english;
        const bool loading = statusContext_ && !restricted_ && !view->cache();
        if (loading) {
            const auto text = tr.wide(
                "menu", statusCache_ ? "Unable to read input method menu"
                                     : "Reading input method menu...");
            hr = menu->AddMenuItem(0, TF_LBMENUF_GRAYED, nullptr, nullptr,
                                   text.c_str(),
                                   static_cast<ULONG>(text.size()), nullptr);
            if (FAILED(hr)) {
                statusView_.reset();
                return hr;
            }
        }
        if (loading || (view->cache() && !view->cache()->reply.nodes.empty())) {
            hr = menu->AddMenuItem(0, TF_LBMENUF_SEPARATOR, nullptr, nullptr,
                                   L"", 0, nullptr);
            if (FAILED(hr)) {
                statusView_.reset();
                return hr;
            }
        }
        for (const auto &command : kCommands) {
            if ((restricted_ && command.id != kSettingsMenuId) ||
                (systemMenu_ && command.id == kAboutMenuId))
                continue;
            const auto text = tr.wide("menu", command.text);
            hr =
                menu->AddMenuItem(command.id, 0, nullptr, nullptr, text.c_str(),
                                  static_cast<ULONG>(text.size()), nullptr);
            if (FAILED(hr)) {
                statusView_.reset();
                return hr;
            }
        }
        return S_OK;
    } catch (...) {
        statusView_.reset();
        return E_OUTOFMEMORY;
    }
}
STDMETHODIMP LangBarItem::OnMenuSelect(UINT id) {
    if ((restricted_ && id != kSettingsMenuId) ||
        (systemMenu_ && id == kAboutMenuId))
        return E_ACCESSDENIED;
    for (const auto &command : kCommands)
        if (id == command.id)
            return !dispatchWindow_ ||
                           PostMessageW(dispatchWindow_, command.message, 0, 0)
                       ? S_OK
                       : E_FAIL;
    StatusMenuSelection selection;
    try {
        if (dispatchWindow_ && !restricted_ && statusView_ && statusHandler_ &&
            statusView_->select(id, selection)) {
            const auto handler = statusHandler_;
            return handler(selection);
        }
    } catch (const std::bad_alloc &) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
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
    *pbstrText =
        SysAllocString(iconLabel_.empty() ? (chineseMode_ ? L"\u62fc" : L"A")
                                          : iconLabel_.c_str());
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

void LangBarItem::setTranslation(win32::i18n::Translation translation) {
    translation_ = std::move(translation);
    notify(TF_LBI_TEXT | TF_LBI_TOOLTIP);
}

void LangBarItem::setAvailable(bool available) {
    if (available_ == available)
        return;
    available_ = available;
    notify(TF_LBI_STATUS);
}

void LangBarItem::setMode(bool chineseMode) {
    if (chineseMode_ != chineseMode) {
        chineseMode_ = chineseMode;
        notify(TF_LBI_BTNALL | TF_LBI_STATUS);
    }
}

void LangBarItem::setInputMethod(bool enabled, std::wstring name,
                                 std::wstring label, std::wstring icon) {
    if (chineseMode_ == enabled && methodName_ == name && iconLabel_ == label &&
        iconPath_ == icon) {
        return;
    }
    chineseMode_ = enabled;
    methodName_ = std::move(name);
    iconLabel_ = std::move(label);
    iconPath_ = std::move(icon);
    notify(TF_LBI_BTNALL | TF_LBI_STATUS);
}

void LangBarItem::detach() {
    invalidateStatusMenu();
    statusHandler_ = {};
    uiManager_.Release();
    dispatchWindow_ = nullptr;
    shown_ = false;
    notify(TF_LBI_STATUS);
    sink_.Release();
    sinkCookie_ = TF_INVALID_COOKIE;
}

HICON LangBarItem::createIcon() const {
    if (auto icon = loadInputMethodIcon(iconPath_)) {
        return icon;
    }
    return makeTextIcon(iconLabel_.empty() ? (chineseMode_ ? L"\u62fc" : L"A")
                                           : iconLabel_.c_str());
}

} // namespace fcitx
