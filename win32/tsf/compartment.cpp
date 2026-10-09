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
        keyboardOpen_ = value.lVal != 0;
        if (messageWindow_) {
            PostMessageW(messageWindow_, WM_APP + 2, 0, 0);
        }
    }
    return S_OK;
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
    return submitSnapshot(std::move(reply), synchronous);
}

} // namespace fcitx
