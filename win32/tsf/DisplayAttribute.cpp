#include "displayattribute.h"
#include "tsf.h"
#include <new>

extern void DllAddRef();
extern void DllRelease();

namespace fcitx {
namespace {

class AttributeInfo final : public ITfDisplayAttributeInfo {
  public:
    AttributeInfo() { DllAddRef(); }
    ~AttributeInfo() { DllRelease(); }
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result)
            return E_INVALIDARG;
        *result = nullptr;
        if (id != IID_IUnknown && id != IID_ITfDisplayAttributeInfo)
            return E_NOINTERFACE;
        *result = static_cast<ITfDisplayAttributeInfo *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&references_);
    }
    STDMETHODIMP_(ULONG) Release() override {
        const auto remaining = InterlockedDecrement(&references_);
        if (!remaining)
            delete this;
        return remaining;
    }
    STDMETHODIMP GetGUID(GUID *guid) override {
        if (!guid)
            return E_INVALIDARG;
        *guid = kPreeditAttribute;
        return S_OK;
    }
    STDMETHODIMP GetDescription(BSTR *description) override {
        if (!description)
            return E_INVALIDARG;
        *description = SysAllocString(L"Fcitx5 preedit");
        return *description ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE *attribute) override {
        if (!attribute)
            return E_INVALIDARG;
        *attribute = value_;
        return S_OK;
    }
    STDMETHODIMP
    SetAttributeInfo(const TF_DISPLAYATTRIBUTE *attribute) override {
        if (!attribute)
            return E_INVALIDARG;
        value_ = *attribute;
        return S_OK;
    }
    STDMETHODIMP Reset() override {
        value_ = defaultValue();
        return S_OK;
    }

  private:
    static TF_DISPLAYATTRIBUTE defaultValue() {
        TF_DISPLAYATTRIBUTE value{};
        value.lsStyle = TF_LS_DOT;
        value.crLine.type = TF_CT_SYSCOLOR;
        value.crLine.nIndex = COLOR_WINDOWTEXT;
        value.bAttr = TF_ATTR_INPUT;
        return value;
    }
    LONG references_ = 1;
    TF_DISPLAYATTRIBUTE value_ = defaultValue();
};

class AttributeEnumerator final : public IEnumTfDisplayAttributeInfo {
  public:
    AttributeEnumerator() { DllAddRef(); }
    ~AttributeEnumerator() { DllRelease(); }
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result)
            return E_INVALIDARG;
        *result = nullptr;
        if (id != IID_IUnknown && id != IID_IEnumTfDisplayAttributeInfo)
            return E_NOINTERFACE;
        *result = static_cast<IEnumTfDisplayAttributeInfo *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&references_);
    }
    STDMETHODIMP_(ULONG) Release() override {
        const auto remaining = InterlockedDecrement(&references_);
        if (!remaining)
            delete this;
        return remaining;
    }
    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo **result) override {
        if (!result)
            return E_INVALIDARG;
        *result = nullptr;
        auto *copy = new (std::nothrow) AttributeEnumerator;
        if (!copy)
            return E_OUTOFMEMORY;
        copy->done_ = done_;
        *result = copy;
        return S_OK;
    }
    STDMETHODIMP Next(ULONG count, ITfDisplayAttributeInfo **items,
                      ULONG *fetched) override {
        if (fetched)
            *fetched = 0;
        if (!items || (count != 1 && !fetched))
            return E_INVALIDARG;
        for (ULONG index = 0; index < count; ++index)
            items[index] = nullptr;
        if (!count)
            return S_OK;
        if (done_)
            return S_FALSE;
        items[0] = new (std::nothrow) AttributeInfo;
        if (!items[0])
            return E_OUTOFMEMORY;
        done_ = true;
        if (fetched)
            *fetched = 1;
        return count == 1 ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override {
        done_ = false;
        return S_OK;
    }
    STDMETHODIMP Skip(ULONG count) override {
        if (!count)
            return S_OK;
        const bool available = !done_;
        done_ = true;
        return available && count == 1 ? S_OK : S_FALSE;
    }

  private:
    LONG references_ = 1;
    bool done_ = false;
};

} // namespace

STDMETHODIMP
Tsf::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo **result) {
    if (!result)
        return E_INVALIDARG;
    *result = new (std::nothrow) AttributeEnumerator;
    return *result ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP Tsf::GetDisplayAttributeInfo(REFGUID guid,
                                          ITfDisplayAttributeInfo **result) {
    if (!result)
        return E_INVALIDARG;
    *result = nullptr;
    if (guid != kPreeditAttribute)
        return E_INVALIDARG;
    *result = new (std::nothrow) AttributeInfo;
    return *result ? S_OK : E_OUTOFMEMORY;
}

} // namespace fcitx
