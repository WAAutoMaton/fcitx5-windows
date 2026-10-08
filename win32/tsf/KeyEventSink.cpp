#include "keypolicy.h"
#include "tsf.h"

namespace fcitx {
namespace {
constexpr uint32_t kModifierShift = 1U << 0;
constexpr uint32_t kModifierControl = 1U << 1;
constexpr uint32_t kModifierAlt = 1U << 2;
constexpr uint32_t kModifierSuper = 1U << 3;
constexpr uint32_t kModifierCaps = 1U << 4;
constexpr uint32_t kModifierRepeat = 1U << 5;

uint32_t currentModifiers(LPARAM lParam) {
    uint32_t result = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) {
        result |= kModifierShift;
    }
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        result |= kModifierControl;
    }
    if (GetKeyState(VK_MENU) & 0x8000) {
        result |= kModifierAlt;
    }
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) {
        result |= kModifierSuper;
    }
    if (GetKeyState(VK_CAPITAL) & 1) {
        result |= kModifierCaps;
    }
    if ((lParam & (1LL << 30)) != 0) {
        result |= kModifierRepeat;
    }
    return result;
}

uint32_t scanCode(LPARAM lParam) {
    auto value = static_cast<uint32_t>((lParam >> 16) & 0xff);
    if (lParam & (1LL << 24)) {
        value |= 0x100;
    }
    return value;
}

uint32_t unicodeForKey(WPARAM key, LPARAM details) {
    BYTE keyboard[256]{};
    WCHAR text[4]{};
    if (!GetKeyboardState(keyboard) || (keyboard[VK_CONTROL] & 0x80) ||
        (keyboard[VK_MENU] & 0x80)) {
        return 0;
    }
    const auto count =
        ToUnicodeEx(static_cast<UINT>(key), scanCode(details) & 0xff, keyboard,
                    text, 4, 4, GetKeyboardLayout(0));
    if (count == 1 && text[0] >= 0x20 && text[0] != 0x7f &&
        !(text[0] >= 0xd800 && text[0] <= 0xdfff)) {
        return text[0];
    }
    if (count == 2 && text[0] >= 0xd800 && text[0] <= 0xdbff &&
        text[1] >= 0xdc00 && text[1] <= 0xdfff) {
        return 0x10000 + ((text[0] - 0xd800) << 10) + text[1] - 0xdc00;
    }
    return 0;
}

} // namespace

HRESULT Tsf::initKeyEventSink() {
    CComPtr<ITfKeystrokeMgr> keystrokeMgr;
    auto result = threadMgr_->QueryInterface(&keystrokeMgr);
    if (FAILED(result)) {
        return result;
    }
    result = keystrokeMgr->AdviseKeyEventSink(clientId_,
                                              (ITfKeyEventSink *)this, TRUE);
    keyEventSinkAdvised_ = SUCCEEDED(result);
    return result;
}

void Tsf::uninitKeyEventSink() {
    if (!keyEventSinkAdvised_) {
        return;
    }
    CComPtr<ITfKeystrokeMgr> keystrokeMgr;
    if (threadMgr_->QueryInterface(&keystrokeMgr) != S_OK) {
        return;
    }
    keystrokeMgr->UnadviseKeyEventSink(clientId_);
    keyEventSinkAdvised_ = false;
}

BOOL Tsf::processKey(ITfContext *context, WPARAM wParam, LPARAM lParam,
                     bool release) {
    if (context == nullptr || context != textEditSinkContext_ ||
        remoteContextId_ == 0 || !pipe_.connected() || !foreground_) {
        return FALSE;
    }
    PipeClient::KeyReply reply;
    if (!pipe_.key(remoteContextId_, release, static_cast<uint32_t>(wParam),
                   scanCode(lParam), currentModifiers(lParam),
                   static_cast<uint32_t>(GetMessageTime()), reply,
                   unicodeForKey(wParam, lParam))) {
        clearRemoteContext();
        pipe_.disconnect();
        return FALSE;
    }
    bool consumed = reply.consumed;
    if (wParam < handledKeys_.size()) {
        if (release) {
            consumed = consumed || handledKeys_.test(wParam);
            handledKeys_.reset(wParam);
        } else if (consumed) {
            handledKeys_.set(wParam);
        }
    }
    submitSnapshot(std::move(reply), true);
    return consumed ? TRUE : FALSE;
}

STDMETHODIMP Tsf::OnSetFocus(BOOL foreground) {
    foreground_ = foreground != FALSE;
    if (!foreground_) {
        clearRemoteContext();
    } else {
        initRemoteContext();
    }
    return S_OK;
}

STDMETHODIMP Tsf::OnTestKeyDown(ITfContext *context, WPARAM key, LPARAM details,
                                BOOL *pfEaten) {
    if (pfEaten == nullptr) {
        return E_INVALIDARG;
    }
    *pfEaten = context && context == textEditSinkContext_ && foreground_ &&
               remoteContextId_ && pipe_.connected() &&
               routesKey(static_cast<uint32_t>(key), currentModifiers(details),
                         state_.enabled,
                         !state_.preedit.empty() || !state_.candidates.empty());
    return S_OK;
}

STDMETHODIMP Tsf::OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                            BOOL *pfEaten) {
    if (pfEaten == nullptr) {
        return E_INVALIDARG;
    }
    *pfEaten = processKey(pContext, wParam, lParam, false);
    return S_OK;
}

STDMETHODIMP Tsf::OnTestKeyUp(ITfContext *context, WPARAM key, LPARAM,
                              BOOL *pfEaten) {
    if (pfEaten == nullptr) {
        return E_INVALIDARG;
    }
    *pfEaten = context && context == textEditSinkContext_ && foreground_ &&
               remoteContextId_ && pipe_.connected() &&
               key < handledKeys_.size() && handledKeys_.test(key);
    return S_OK;
}

STDMETHODIMP Tsf::OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                          BOOL *pfEaten) {
    if (pfEaten == nullptr) {
        return E_INVALIDARG;
    }
    *pfEaten = processKey(pContext, wParam, lParam, true);
    return S_OK;
}

STDMETHODIMP Tsf::OnPreservedKey(ITfContext *, REFGUID, BOOL *pfEaten) {
    if (pfEaten == nullptr) {
        return E_INVALIDARG;
    }
    *pfEaten = FALSE;
    return S_OK;
}
} // namespace fcitx
