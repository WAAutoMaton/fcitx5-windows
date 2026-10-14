#pragma once

#include <atlcomcli.h>
#include <functional>
#include <msctf.h>
#include <new>
#include <string>

extern void DllAddRef();
extern void DllRelease();

namespace fcitx {

class AuxiliaryUIElement : public ITfUIElement {
  public:
    AuxiliaryUIElement(std::wstring description, std::function<void()> close)
        : description_(std::move(description)), close_(std::move(close)) {
        DllAddRef();
    }
    ~AuxiliaryUIElement() { DllRelease(); }
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result)
            return E_INVALIDARG;
        *result = nullptr;
        if (id != IID_IUnknown && id != IID_ITfUIElement)
            return E_NOINTERFACE;
        *result = static_cast<ITfUIElement *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&references_);
    }
    STDMETHODIMP_(ULONG) Release() override {
        const auto count = InterlockedDecrement(&references_);
        if (!count)
            delete this;
        return count;
    }
    STDMETHODIMP GetDescription(BSTR *text) override {
        if (!text)
            return E_INVALIDARG;
        *text = SysAllocString(description_.c_str());
        return *text ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetGUID(GUID *guid) override {
        if (!guid)
            return E_INVALIDARG;
        *guid = {0x9f0077aa,
                 0xcb7e,
                 0x4d42,
                 {0xb3, 0xd5, 0xf9, 0x49, 0xfa, 0x35, 0x46, 0x2d}};
        return S_OK;
    }
    STDMETHODIMP Show(BOOL show) override {
        if (!close_)
            return E_UNEXPECTED;
        if (!show) {
            suppressed_ = true;
            visible_ = false;
            const auto close = close_;
            close();
        }
        return !show || visible_ ? S_OK : E_NOTIMPL;
    }
    STDMETHODIMP IsShown(BOOL *shown) override {
        if (!shown)
            return E_INVALIDARG;
        *shown = visible_ ? TRUE : FALSE;
        return S_OK;
    }
    void setVisible(bool visible) { visible_ = visible; }
    bool suppressed() const { return suppressed_; }
    void detach() {
        visible_ = false;
        close_ = {};
    }

  private:
    LONG references_ = 1;
    bool visible_ = false;
    bool suppressed_ = false;
    std::wstring description_;
    std::function<void()> close_;
};

class AuxiliaryUI {
  public:
    ~AuxiliaryUI() { reset(); }
    bool begin(ITfUIElementMgr *manager, std::wstring description,
               std::function<void()> close) {
        reset();
        if (!manager)
            return true;
        CComPtr<ITfUIElementMgr> lifetime = manager;
        CComPtr<AuxiliaryUIElement> element;
        element.Attach(new (std::nothrow) AuxiliaryUIElement(
            std::move(description), std::move(close)));
        if (!element)
            return false;
        manager_ = manager;
        element_ = element;
        BOOL show = TRUE;
        DWORD id = 0;
        const auto result = manager->BeginUIElement(element, &show, &id);
        if (element_ != element) {
            element->detach();
            if (SUCCEEDED(result))
                manager->EndUIElement(id);
            return false;
        }
        if (FAILED(result)) {
            reset();
            return false;
        }
        id_ = id;
        active_ = true;
        if (!show || element->suppressed()) {
            reset();
            return false;
        }
        return true;
    }
    void setVisible(bool visible) {
        if (element_)
            element_->setVisible(visible);
    }
    bool active() const { return active_; }
    void reset() {
        const auto manager = manager_;
        const auto element = element_;
        const auto id = id_;
        const auto active = active_;
        active_ = false;
        manager_.Release();
        element_.Release();
        if (element)
            element->detach();
        if (active && manager)
            manager->EndUIElement(id);
    }

  private:
    CComPtr<ITfUIElementMgr> manager_;
    CComPtr<AuxiliaryUIElement> element_;
    DWORD id_ = 0;
    bool active_ = false;
};

} // namespace fcitx
