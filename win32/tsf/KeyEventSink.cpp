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
    if ((GetKeyState(VK_LWIN) & 0x8000) ||
        (GetKeyState(VK_RWIN) & 0x8000)) {
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

} // namespace

bool Tsf::initKeyEventSink() {
    CComPtr<ITfKeystrokeMgr> keystrokeMgr;
    if (threadMgr_->QueryInterface(&keystrokeMgr) != S_OK) {
        return false;
    }
    return keystrokeMgr->AdviseKeyEventSink(clientId_, (ITfKeyEventSink *)this,
                                            TRUE) == S_OK;
}

void Tsf::uninitKeyEventSink() {
    CComPtr<ITfKeystrokeMgr> keystrokeMgr;
    if (threadMgr_->QueryInterface(&keystrokeMgr) != S_OK) {
        return;
    }
    keystrokeMgr->UnadviseKeyEventSink(clientId_);
}

BOOL Tsf::processKey(ITfContext *context, WPARAM wParam, LPARAM lParam,
                     bool release) {
    if (context == nullptr || remoteContextId_ == 0 || !pipe_.connected()) {
        return FALSE;
    }
    PipeClient::KeyReply reply;
    if (!pipe_.key(remoteContextId_, release, static_cast<uint32_t>(wParam),
                   scanCode(lParam), currentModifiers(lParam),
                   static_cast<uint32_t>(GetMessageTime()), reply)) {
        return FALSE;
    }
    if (release) {
        return reply.consumed ? TRUE : FALSE;
    }

    pendingEditContext_ = context;
    pendingCommit_ = std::move(reply.commit);
    pendingPreedit_ = std::move(reply.preedit);
    pendingPreeditCursor_ = reply.preeditCursor;
    if (!pendingCommit_.empty() || !pendingPreedit_.empty() || composition_) {
        HRESULT sessionResult = E_FAIL;
        const auto requestResult = context->RequestEditSession(
            clientId_, this, TF_ES_SYNC | TF_ES_READWRITE, &sessionResult);
        if (FAILED(requestResult) || FAILED(sessionResult)) {
            pendingEditContext_ = nullptr;
            pendingCommit_.clear();
            pendingPreedit_.clear();
            composition_ = nullptr;
            return FALSE;
        }
    }
    pendingEditContext_ = nullptr;
    return reply.consumed ? TRUE : FALSE;
}

STDMETHODIMP Tsf::OnSetFocus(BOOL) { return S_OK; }

STDMETHODIMP Tsf::OnTestKeyDown(ITfContext *, WPARAM, LPARAM,
                                BOOL *pfEaten) {
    if (pfEaten == nullptr) {
        return E_INVALIDARG;
    }
    *pfEaten = (remoteContextId_ != 0 && pipe_.connected()) ? TRUE : FALSE;
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

STDMETHODIMP Tsf::OnTestKeyUp(ITfContext *, WPARAM, LPARAM,
                              BOOL *pfEaten) {
    if (pfEaten == nullptr) {
        return E_INVALIDARG;
    }
    *pfEaten = (remoteContextId_ != 0 && pipe_.connected()) ? TRUE : FALSE;
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