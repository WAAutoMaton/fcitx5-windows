#include "tsf.h"
#include <initguid.h>
#include <inputscope.h>
#include <new>

namespace fcitx {
namespace {
bool scopeAllowsInput(ITfContext *context, TfEditCookie cookie) {
    CComPtr<ITfReadOnlyProperty> property;
    const auto result =
        context->GetAppProperty(GUID_PROP_INPUTSCOPE, &property);
    if (result == S_FALSE || (SUCCEEDED(result) && !property))
        return true;
    if (FAILED(result))
        return false;
    TF_SELECTION selection{};
    ULONG fetched = 0;
    if (FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1,
                                     &selection, &fetched)) ||
        !fetched)
        return false;
    CComPtr<ITfRange> range;
    range.Attach(selection.range);
    CComVariant value;
    const auto valueResult = property->GetValue(cookie, range, &value);
    // TSF returns an empty E_FAIL when the text store supplies no InputScope.
    if (valueResult == E_FAIL && value.vt == VT_EMPTY)
        return true;
    if (FAILED(valueResult))
        return false;
    if (value.vt == VT_EMPTY)
        return true;
    if (value.vt != VT_UNKNOWN || !value.punkVal)
        return false;
    CComPtr<ITfInputScope> scope;
    if (FAILED(value.punkVal->QueryInterface(IID_PPV_ARGS(&scope))))
        return false;
    InputScope *scopes = nullptr;
    UINT count = 0;
    const auto query = scope->GetInputScopes(&scopes, &count);
    bool allowed = SUCCEEDED(query) && (!count || scopes);
    if (allowed) {
        for (UINT i = 0; i < count; ++i)
            if (scopes[i] == IS_PASSWORD || scopes[i] == IS_NUMERIC_PASSWORD)
                allowed = false;
    }
    CoTaskMemFree(scopes);
    return allowed;
}

class ContextPolicyEditSession : public ITfEditSession {
  public:
    explicit ContextPolicyEditSession(ITfContext *context)
        : context_(context) {}
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result)
            return E_INVALIDARG;
        *result = nullptr;
        if (id != IID_IUnknown && id != IID_ITfEditSession)
            return E_NOINTERFACE;
        *result = static_cast<ITfEditSession *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override {
        const auto count = --references_;
        if (!count)
            delete this;
        return count;
    }
    STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
        allowed = scopeAllowsInput(context_, cookie);
        return S_OK;
    }
    bool allowed = false;

  private:
    ULONG references_ = 1;
    CComPtr<ITfContext> context_;
};

bool compartmentDisabled(ITfCompartmentMgr *manager, REFGUID id) {
    CComPtr<ITfCompartment> compartment;
    CComVariant value;
    if (FAILED(manager->GetCompartment(id, &compartment)) || !compartment ||
        FAILED(compartment->GetValue(&value)))
        return true;
    return value.vt != VT_EMPTY && (value.vt != VT_I4 || value.lVal != 0);
}
} // namespace

bool Tsf::contextAllowsInput(ITfContext *context) const {
    if (!inputAllowed_ || !scopeAllowed_ || !context)
        return false;
    TF_STATUS status{};
    if (FAILED(context->GetStatus(&status)) ||
        (status.dwDynamicFlags & TF_SD_READONLY))
        return false;
    CComPtr<ITfCompartmentMgr> manager;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(&manager))))
        return false;
    return !compartmentDisabled(manager, GUID_COMPARTMENT_KEYBOARD_DISABLED) &&
           !compartmentDisabled(manager, GUID_COMPARTMENT_EMPTYCONTEXT);
}

bool Tsf::refreshContextPolicy() {
    CComPtr<ITfContext> context = textEditSinkContext_;
    if (!inputAllowed_ || !context)
        return false;
    CComPtr<ContextPolicyEditSession> session;
    session.Attach(new (std::nothrow) ContextPolicyEditSession(context));
    if (!session)
        return false;
    const auto generation = generation_;
    HRESULT result = E_FAIL;
    const auto request = context->RequestEditSession(
        clientId_, session, TF_ES_SYNC | TF_ES_READ, &result);
    if (generation != generation_ || context != textEditSinkContext_)
        return false;
    scopeAllowed_ = SUCCEEDED(request) && result == S_OK && session->allowed;
    return contextAllowsInput(context);
}

bool Tsf::scopeAllowsInputAtEdit(ITfContext *context,
                                 TfEditCookie cookie) const {
    return contextAllowsInput(context) && scopeAllowsInput(context, cookie);
}

} // namespace fcitx
