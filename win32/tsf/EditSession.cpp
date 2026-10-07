#include "tsf.h"
#include <string>

namespace fcitx {
namespace {

std::wstring toWide(const std::string &text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(length, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), result.data(),
                            length) != length) {
        return {};
    }
    return result;
}

bool setCompositionText(TfEditCookie ec, ITfComposition *composition,
                        const std::wstring &text) {
    CComPtr<ITfRange> range;
    if (composition == nullptr || FAILED(composition->GetRange(&range)) ||
        range == nullptr) {
        return false;
    }
    return SUCCEEDED(range->SetText(ec, 0, text.c_str(),
                                    static_cast<ULONG>(text.size())));
}

} // namespace

STDMETHODIMP Tsf::DoEditSession(TfEditCookie ec) {
    if (pendingEditContext_ == nullptr) {
        return E_UNEXPECTED;
    }

    if (composition_ != nullptr && !pendingCommit_.empty()) {
        setCompositionText(ec, composition_, L"");
        composition_->EndComposition(ec);
        composition_ = nullptr;
    }

    if (!pendingCommit_.empty()) {
        const auto commit = toWide(pendingCommit_);
        if (commit.empty()) {
            return E_INVALIDARG;
        }
        CComPtr<ITfInsertAtSelection> insertAtSelection;
        if (FAILED(pendingEditContext_->QueryInterface(
                IID_ITfInsertAtSelection, (void **)&insertAtSelection))) {
            return E_FAIL;
        }
        if (FAILED(insertAtSelection->InsertTextAtSelection(
                ec, TF_IAS_NOQUERY, commit.c_str(),
                static_cast<ULONG>(commit.size()), nullptr))) {
            return E_FAIL;
        }
    }

    if (pendingPreedit_.empty()) {
        if (composition_ != nullptr) {
            setCompositionText(ec, composition_, L"");
            composition_->EndComposition(ec);
            composition_ = nullptr;
        }
        return S_OK;
    }

    const auto preedit = toWide(pendingPreedit_);
    if (preedit.empty()) {
        return E_INVALIDARG;
    }
    if (composition_ != nullptr) {
        return setCompositionText(ec, composition_, preedit) ? S_OK : E_FAIL;
    }

    CComPtr<ITfInsertAtSelection> insertAtSelection;
    if (FAILED(pendingEditContext_->QueryInterface(
            IID_ITfInsertAtSelection, (void **)&insertAtSelection))) {
        return E_FAIL;
    }
    CComPtr<ITfRange> range;
    if (FAILED(insertAtSelection->InsertTextAtSelection(
            ec, TF_IAS_QUERYONLY, nullptr, 0, &range)) || range == nullptr) {
        return E_FAIL;
    }
    CComPtr<ITfContextComposition> contextComposition;
    if (FAILED(pendingEditContext_->QueryInterface(
            IID_ITfContextComposition, (void **)&contextComposition))) {
        return E_FAIL;
    }
    if (FAILED(contextComposition->StartComposition(
            ec, range, static_cast<ITfCompositionSink *>(this), &composition_)) ||
        composition_ == nullptr) {
        return E_FAIL;
    }
    return setCompositionText(ec, composition_, preedit) ? S_OK : E_FAIL;
}
} // namespace fcitx