#include "../dll/register.h"
#include "../dll/resource.h"
#include "../dll/util.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <shellapi.h>
#include <source_location>
#include <string>
#include <vector>

namespace {
void require(bool condition,
             std::source_location location = std::source_location::current()) {
    if (!condition) {
        std::cerr << "Registration check failed at line " << location.line()
                  << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

template <typename Interface> class ManagerStub : public Interface {
  public:
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result)
            return E_INVALIDARG;
        *result = nullptr;
        if (id != IID_IUnknown && id != __uuidof(Interface))
            return E_NOINTERFACE;
        *result = static_cast<Interface *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override { return --references_; }

  private:
    ULONG references_ = 1;
};

class ProfileManager : public ManagerStub<ITfInputProcessorProfileMgr> {
  public:
    STDMETHODIMP RegisterProfile(REFCLSID clsid, LANGID language,
                                 REFGUID profile, const WCHAR *description,
                                 ULONG descriptionLength, const WCHAR *path,
                                 ULONG pathLength, ULONG index, HKL substitute,
                                 DWORD layout, BOOL enabled,
                                 DWORD flags) override {
        require(clsid == fcitx::FCITX_CLSID && profile == fcitx::PROFILE_GUID);
        require(language == TEXTSERVICE_LANGID_HANS);
        require(descriptionLength == 6 &&
                std::wstring(description, descriptionLength) == L"Fcitx5");
        require(pathLength == expectedPath.size());
        require(std::wstring(path, pathLength) == expectedPath);
        require(index == 0 && !substitute && !layout && enabled && !flags);
        ++calls;
        iconIndex = index;
        return result;
    }
    STDMETHODIMP ActivateProfile(DWORD, LANGID, REFCLSID, REFGUID, HKL,
                                 DWORD) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP DeactivateProfile(DWORD, LANGID, REFCLSID, REFGUID, HKL,
                                   DWORD) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetProfile(DWORD, LANGID, REFCLSID, REFGUID, HKL,
                            TF_INPUTPROCESSORPROFILE *) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP EnumProfiles(LANGID,
                              IEnumTfInputProcessorProfiles **) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP ReleaseInputProcessor(REFCLSID, DWORD) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP UnregisterProfile(REFCLSID, LANGID, REFGUID, DWORD) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetActiveProfile(REFGUID,
                                  TF_INPUTPROCESSORPROFILE *) override {
        return E_NOTIMPL;
    }
    std::wstring expectedPath;
    ULONG iconIndex = 0;
    unsigned int calls = 0;
    HRESULT result = S_OK;
};

class GUIDEnumeration : public IEnumGUID {
  public:
    explicit GUIDEnumeration(std::vector<GUID> values)
        : values_(std::move(values)) {}
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result)
            return E_INVALIDARG;
        *result = nullptr;
        if (id != IID_IUnknown && id != IID_IEnumGUID)
            return E_NOINTERFACE;
        *result = static_cast<IEnumGUID *>(this);
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
    STDMETHODIMP Next(ULONG count, GUID *values, ULONG *fetched) override {
        if (!values || !fetched)
            return E_INVALIDARG;
        *fetched = 0;
        while (*fetched < count && position_ < values_.size())
            values[(*fetched)++] = values_[position_++];
        return *fetched == count ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG) override { return E_NOTIMPL; }
    STDMETHODIMP Reset() override {
        position_ = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumGUID **result) override {
        if (result)
            *result = nullptr;
        return E_NOTIMPL;
    }

  private:
    ULONG references_ = 1;
    size_t position_ = 0;
    std::vector<GUID> values_;
};

class CategoryManager : public ManagerStub<ITfCategoryMgr> {
  public:
    STDMETHODIMP RegisterCategory(REFCLSID clsid, REFGUID category,
                                  REFGUID item) override {
        require(clsid == fcitx::FCITX_CLSID && item == fcitx::FCITX_CLSID);
        require(category != GUID_TFCAT_TIPCAP_SECUREMODE &&
                category != GUID_TFCAT_TIPCAP_COMLESS);
#ifndef FCITX5_MODERN_TSF_CATEGORIES
        require(category != GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT &&
                category != GUID_TFCAT_TIPCAP_UIELEMENTENABLED);
#endif
        categories.push_back(category);
        if (categories.size() == failAt)
            return E_ACCESSDENIED;
        registered.push_back(category);
        return S_OK;
    }
    STDMETHODIMP UnregisterCategory(REFCLSID clsid, REFGUID category,
                                    REFGUID item) override {
        require(clsid == fcitx::FCITX_CLSID && item == fcitx::FCITX_CLSID);
        removed.push_back(category);
        if (removed.size() == failRemoveAt)
            return E_ACCESSDENIED;
        std::erase(registered, category);
        return S_OK;
    }
    STDMETHODIMP EnumCategoriesInItem(REFGUID item,
                                      IEnumGUID **result) override {
        if (!result || item != fcitx::FCITX_CLSID)
            return E_INVALIDARG;
        *result = new GUIDEnumeration(registered);
        return S_OK;
    }
    STDMETHODIMP EnumItemsInCategory(REFGUID, IEnumGUID **) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP FindClosestCategory(REFGUID, GUID *, const GUID **,
                                     ULONG) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP RegisterGUIDDescription(REFCLSID, REFGUID, const WCHAR *,
                                         ULONG) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP UnregisterGUIDDescription(REFCLSID, REFGUID) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetGUIDDescription(REFGUID, BSTR *) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP RegisterGUIDDWORD(REFCLSID, REFGUID, DWORD) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP UnregisterGUIDDWORD(REFCLSID, REFGUID) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetGUIDDWORD(REFGUID, DWORD *) override { return E_NOTIMPL; }
    STDMETHODIMP RegisterGUID(REFGUID, TfGuidAtom *) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetGUID(TfGuidAtom, GUID *) override { return E_NOTIMPL; }
    STDMETHODIMP IsEqualTfGuidAtom(TfGuidAtom, REFGUID, BOOL *) override {
        return E_NOTIMPL;
    }
    std::vector<GUID> categories;
    std::vector<GUID> registered, removed;
    size_t failAt = 0;
    size_t failRemoveAt = 0;
};
} // namespace

int wmain(int count, wchar_t **arguments) {
    require(count == 2);
    // Loading the DLL does not register the TIP or change system settings.
    const auto module = LoadLibraryW(arguments[1]);
    require(module);
    require(FindResourceA(module, MAKEINTRESOURCEA(IDI_FCITX5), RT_GROUP_ICON));
    WCHAR path[32768]{};
    const auto length = GetModuleFileNameW(module, path, ARRAYSIZE(path));
    require(length && length < ARRAYSIZE(path));
    ProfileManager profiles;
    profiles.expectedPath.assign(path, length);
    require(fcitx::RegisterProfile(&profiles, module) == S_OK);
    require(profiles.calls == 1);
    require(ExtractIconExW(path, -1, nullptr, nullptr, 0) == 1);
    HICON smallIcon = nullptr, largeIcon = nullptr;
    const auto extracted =
        ExtractIconExW(path, profiles.iconIndex, &largeIcon, &smallIcon, 1);
    require(extracted > 0 && extracted != UINT_MAX);
    require(smallIcon && largeIcon);
    DestroyIcon(smallIcon);
    DestroyIcon(largeIcon);
    profiles.result = E_ACCESSDENIED;
    require(fcitx::RegisterProfile(&profiles, module) == E_ACCESSDENIED);
    require(fcitx::RegisterProfile(nullptr, module) == E_INVALIDARG);
    require(fcitx::RegisterProfile(&profiles, nullptr) == E_INVALIDARG);
    require(
        FAILED(fcitx::RegisterProfile(&profiles, GetModuleHandleW(nullptr))));
    require(profiles.calls == 2);

    CategoryManager categories;
    require(fcitx::RegisterCategories(&categories) == S_OK);
#ifdef FCITX5_MODERN_TSF_CATEGORIES
    require(categories.categories.size() == 5);
    require(categories.categories[3] == GUID_TFCAT_TIPCAP_UIELEMENTENABLED &&
            categories.categories[4] == GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT);
#else
    require(categories.categories.size() == 3);
#endif
    require(categories.categories[0] == GUID_TFCAT_TIP_KEYBOARD &&
            categories.categories[1] == GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER &&
            categories.categories[2] == GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT);
    require(fcitx::UnregisterCategories(&categories) == S_OK &&
            categories.registered.empty());
    // Remove capabilities from an earlier modern build even after downgrade.
    categories.registered = {GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
                             GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT};
    categories.removed.clear();
    categories.failRemoveAt = 1;
    require(fcitx::UnregisterCategories(&categories) == E_ACCESSDENIED &&
            categories.removed.size() == 2);
    categories.failRemoveAt = 0;
    categories.registered.clear();
    categories.removed.clear();
    categories.categories.clear();
    categories.failAt = 3;
    require(fcitx::RegisterCategories(&categories) == E_ACCESSDENIED &&
            categories.registered.empty());
    require(categories.removed.size() == 2);
    categories.categories.clear();
    categories.failAt = 1;
    require(fcitx::RegisterCategories(&categories) == E_ACCESSDENIED);
    require(categories.categories.size() == 1);
    require(fcitx::RegisterCategories(nullptr) == E_INVALIDARG);
    require(fcitx::UnregisterCategories(nullptr) == E_INVALIDARG);
    FreeLibrary(module);
    std::cout
        << "PASS: embedded DLL branding icon, profile character lengths, "
           "desktop system-tray category and failures (no registration)\n";
}
