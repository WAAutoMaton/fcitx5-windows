#include "keypolicy.h"
#include "tsf.h"

namespace fcitx {

HRESULT Tsf::initKeyboardCompartment() {
    CComPtr<ITfCompartmentMgr> manager;
    auto result = threadMgr_->QueryInterface(&manager);
    if (FAILED(result)) {
        return result;
    }
    result = manager->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
                                     &keyboardCompartment_);
    if (FAILED(result)) {
        return result;
    }
    CComVariant value;
    result = keyboardCompartment_->GetValue(&value);
    if (FAILED(result)) {
        return result;
    }
    keyboardOpen_ = value.vt != VT_I4 || value.lVal != 0;
    if (value.vt != VT_I4) {
        result = setKeyboardOpen(keyboardOpen_);
        if (FAILED(result)) {
            return result;
        }
    }
    CComPtr<ITfSource> source;
    result = keyboardCompartment_->QueryInterface(&source);
    if (FAILED(result)) {
        return result;
    }
    return source->AdviseSink(IID_ITfCompartmentEventSink,
                              static_cast<ITfCompartmentEventSink *>(this),
                              &keyboardCompartmentCookie_);
}

void Tsf::uninitKeyboardCompartment() {
    if (keyboardCompartment_ &&
        keyboardCompartmentCookie_ != TF_INVALID_COOKIE) {
        CComPtr<ITfSource> source;
        if (SUCCEEDED(keyboardCompartment_->QueryInterface(&source))) {
            source->UnadviseSink(keyboardCompartmentCookie_);
        }
    }
    keyboardCompartmentCookie_ = TF_INVALID_COOKIE;
    keyboardCompartment_.Release();
}

HRESULT Tsf::setKeyboardOpen(bool enabled) {
    if (!keyboardCompartment_) {
        return E_UNEXPECTED;
    }
    const auto previous = keyboardOpen_;
    keyboardOpen_ = enabled;
    CComVariant value(static_cast<LONG>(enabled));
    const auto result = keyboardCompartment_->SetValue(clientId_, &value);
    if (FAILED(result)) {
        keyboardOpen_ = previous;
    } else {
        notifyLangBarItem();
    }
    return result;
}

STDMETHODIMP Tsf::OnChange(REFGUID guid) {
    if (guid != GUID_COMPARTMENT_KEYBOARD_OPENCLOSE || !keyboardCompartment_) {
        return S_OK;
    }
    CComVariant value;
    const auto result = keyboardCompartment_->GetValue(&value);
    if (FAILED(result)) {
        return result;
    }
    if (value.vt == VT_I4 && keyboardOpen_ != (value.lVal != 0)) {
        // Windows may handle the IME hotkey through the compartment before
        // delivering a key sink callback. Coalesce both routes for this press.
        if (modeSwitchPressed() && foreground_ && textEditSinkContext_ &&
            remoteContextId_ && pipe_.connected() && messageWindow_ &&
            PostMessageW(messageWindow_, kModeSwitchMessage, 0, 0)) {
            if (!modeSwitchKeyDown_) {
                modeSwitchPending_ = true;
                modeSwitchKeyDown_ = true;
            }
            return S_OK;
        }
        keyboardOpen_ = value.lVal != 0;
        notifyLangBarItem();
        if (messageWindow_) {
            PostMessageW(messageWindow_, WM_APP + 2, 0, 0);
        }
    }
    return S_OK;
}

void Tsf::toggleMode() { cycleMode(false); }

bool Tsf::cycleMode(bool synchronous) {
    if (!remoteContextId_ || !pipe_.connected() || !foreground_ ||
        !textEditSinkContext_) {
        return false;
    }
    PipeClient::KeyReply reply;
    if (!pipe_.cycleInputMethod(remoteContextId_, reply)) {
        clearRemoteContext();
        pipe_.disconnect();
        return false;
    }
    setKeyboardOpen(reply.enabled);
    submitSnapshot(std::move(reply), synchronous);
    return true;
}

bool Tsf::applyKeyboardMode(bool synchronous) {
    if (!remoteContextId_ || !pipe_.connected() || !foreground_ ||
        !textEditSinkContext_) {
        return false;
    }
    if (state_.enabled == keyboardOpen_) {
        return true;
    }
    PipeClient::KeyReply reply;
    if (!pipe_.setMode(remoteContextId_, keyboardOpen_, reply)) {
        clearRemoteContext();
        pipe_.disconnect();
        return false;
    }
    submitSnapshot(std::move(reply), synchronous);
    return true;
}

} // namespace fcitx
