#include "keypolicy.h"
#include "tsf.h"

namespace fcitx {
namespace {
constexpr GUID kModeSwitchPreservedKey = {
    0x4d9f2d1a,
    0x6d73,
    0x4a6d,
    {0x9d, 0x4f, 0x8a, 0x61, 0x72, 0x5e, 0x31, 0x09}};
constexpr wchar_t kModeSwitchDescription[] = L"Fcitx5 Pinyin mode switch";
constexpr uint32_t kModifierCaps = 1U << 4;
constexpr uint32_t kModifierRepeat = 1U << 5;

bool keyDown(int key) { return (GetKeyState(key) & 0x8000) != 0; }

uint32_t currentModifiers(LPARAM lParam) {
    uint32_t result = 0;
    if (keyDown(VK_SHIFT)) {
        result |= kModifierShift;
    }
    if (keyDown(VK_CONTROL) || keyDown(VK_LCONTROL) || keyDown(VK_RCONTROL)) {
        result |= kModifierControl;
    }
    if (keyDown(VK_MENU) || keyDown(VK_LMENU) || keyDown(VK_RMENU)) {
        result |= kModifierAlt;
    }
    if (keyDown(VK_LWIN) || keyDown(VK_RWIN)) {
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
    if (keyEventSinkAdvised_) {
        constexpr TF_PRESERVEDKEY key{VK_SPACE, TF_MOD_CONTROL};
        modeSwitchPreserved_ = SUCCEEDED(keystrokeMgr->PreserveKey(
            clientId_, kModeSwitchPreservedKey, &key, kModeSwitchDescription,
            static_cast<ULONG>(std::size(kModeSwitchDescription) - 1)));
    }
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
    if (modeSwitchPreserved_) {
        constexpr TF_PRESERVEDKEY key{VK_SPACE, TF_MOD_CONTROL};
        keystrokeMgr->UnpreserveKey(kModeSwitchPreservedKey, &key);
        modeSwitchPreserved_ = false;
    }
    keystrokeMgr->UnadviseKeyEventSink(clientId_);
    keyEventSinkAdvised_ = false;
}

BOOL Tsf::processKey(ITfContext *context, WPARAM wParam, LPARAM lParam,
                     bool release, uint32_t modifiers) {
    if (context == nullptr || context != textEditSinkContext_ ||
        remoteContextId_ == 0 || !pipe_.connected() || !foreground_) {
        return FALSE;
    }
    PipeClient::KeyReply reply;
    if (modifiers == UINT32_MAX) {
        modifiers = currentModifiers(lParam);
    }
    if (!release && isModeSwitchKey(static_cast<uint32_t>(wParam), modifiers)) {
        if (!(modifiers & kModifierRepeat)) {
            if (FAILED(setKeyboardOpen(!keyboardOpen_))) {
                return FALSE;
            }
            applyKeyboardMode(true);
        }
        handledKeys_.set(VK_SPACE);
        return TRUE;
    }
    if (!release &&
        !routesKey(static_cast<uint32_t>(wParam), modifiers, keyboardOpen_,
                   !state_.preedit.empty() || !state_.candidates.empty())) {
        return FALSE;
    }
    if (state_.enabled != keyboardOpen_ && !applyKeyboardMode(true)) {
        return FALSE;
    }
    if (!pipe_.key(remoteContextId_, release, static_cast<uint32_t>(wParam),
                   scanCode(lParam), modifiers,
                   static_cast<uint32_t>(GetMessageTime()), reply,
                   unicodeForKey(wParam, lParam))) {
        clearRemoteContext();
        pipe_.disconnect();
        return FALSE;
    }
    bool consumed = reply.consumed;
    if (reply.enabled != keyboardOpen_) {
        setKeyboardOpen(reply.enabled);
    }
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
                         keyboardOpen_,
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

STDMETHODIMP Tsf::OnPreservedKey(ITfContext *context, REFGUID guid,
                                 BOOL *pfEaten) {
    if (pfEaten == nullptr) {
        return E_INVALIDARG;
    }
    *pfEaten = FALSE;
    if (guid == kModeSwitchPreservedKey) {
        *pfEaten = processKey(context, VK_SPACE, 0, false, kModifierControl);
    }
    return S_OK;
}
} // namespace fcitx
