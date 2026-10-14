#include "candidateuielement.h"
#include "textutil.h"
#include <algorithm>

extern void DllAddRef();
extern void DllRelease();

namespace fcitx {
namespace {
constexpr GUID kCandidateElement = {
    0xa7c40e35,
    0x154b,
    0x48c6,
    {0x94, 0xd3, 0x77, 0x6a, 0x31, 0x7d, 0xbe, 0x29}};
constexpr DWORD kAllChanges = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT |
                              TF_CLUIE_SELECTION | TF_CLUIE_STRING |
                              TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
UINT currentPage(const std::vector<UINT> &pages, UINT selection) {
    if (pages.empty() || selection == UINT_MAX)
        return 0;
    return static_cast<UINT>(
        std::upper_bound(pages.begin(), pages.end(), selection) -
        pages.begin() - 1);
}
} // namespace

CandidateUIElement::CandidateUIElement(std::function<void(bool)> show,
                                       Action action)
    : show_(std::move(show)), action_(std::move(action)) {
    DllAddRef();
}
CandidateUIElement::~CandidateUIElement() { DllRelease(); }

STDMETHODIMP CandidateUIElement::QueryInterface(REFIID id, void **result) {
    if (!result)
        return E_INVALIDARG;
    *result = nullptr;
    if (id != IID_IUnknown && id != IID_ITfUIElement &&
        id != IID_ITfCandidateListUIElement &&
        (id != IID_ITfCandidateListUIElementBehavior || !action_))
        return E_NOINTERFACE;
    *result = static_cast<ITfCandidateListUIElement *>(this);
    AddRef();
    return S_OK;
}
STDMETHODIMP_(ULONG) CandidateUIElement::AddRef() {
    return InterlockedIncrement(&references_);
}
STDMETHODIMP_(ULONG) CandidateUIElement::Release() {
    const auto count = InterlockedDecrement(&references_);
    if (!count)
        delete this;
    return count;
}
STDMETHODIMP CandidateUIElement::GetDescription(BSTR *description) {
    if (!description)
        return E_INVALIDARG;
    *description = SysAllocString(L"Fcitx5 candidates");
    return *description ? S_OK : E_OUTOFMEMORY;
}
STDMETHODIMP CandidateUIElement::GetGUID(GUID *guid) {
    if (!guid)
        return E_INVALIDARG;
    *guid = kCandidateElement;
    return S_OK;
}
STDMETHODIMP CandidateUIElement::Show(BOOL show) {
    if (!show_)
        return E_UNEXPECTED;
    const auto callback = show_;
    ++showSerial_;
    if (!show)
        visible_ = false;
    callback(show != FALSE);
    return S_OK;
}
STDMETHODIMP CandidateUIElement::IsShown(BOOL *shown) {
    if (!shown)
        return E_INVALIDARG;
    *shown = visible_ ? TRUE : FALSE;
    return S_OK;
}
STDMETHODIMP CandidateUIElement::GetUpdatedFlags(DWORD *flags) {
    if (!flags)
        return E_INVALIDARG;
    *flags = updated_;
    return S_OK;
}
STDMETHODIMP CandidateUIElement::GetDocumentMgr(ITfDocumentMgr **document) {
    if (!document)
        return E_INVALIDARG;
    *document = nullptr;
    if (!snapshot_ || !snapshot_->document)
        return E_UNEXPECTED;
    *document = snapshot_->document;
    (*document)->AddRef();
    return S_OK;
}
STDMETHODIMP CandidateUIElement::GetCount(UINT *count) {
    if (!count)
        return E_INVALIDARG;
    *count = snapshot_ ? static_cast<UINT>(snapshot_->strings.size()) : 0;
    return S_OK;
}
STDMETHODIMP CandidateUIElement::GetSelection(UINT *selection) {
    if (!selection)
        return E_INVALIDARG;
    *selection = 0;
    if (!snapshot_ || snapshot_->selection == UINT_MAX)
        return S_FALSE;
    *selection = snapshot_->selection;
    return S_OK;
}
STDMETHODIMP CandidateUIElement::GetString(UINT index, BSTR *text) {
    if (!text)
        return E_INVALIDARG;
    *text = nullptr;
    const auto snapshot = snapshot_;
    if (!snapshot || index >= snapshot->strings.size())
        return E_INVALIDARG;
    *text =
        SysAllocStringLen(snapshot->strings[index].data(),
                          static_cast<UINT>(snapshot->strings[index].size()));
    return *text ? S_OK : E_OUTOFMEMORY;
}
STDMETHODIMP CandidateUIElement::GetPageIndex(UINT *indices, UINT capacity,
                                              UINT *count) {
    if (!count)
        return E_INVALIDARG;
    *count = 0;
    if (capacity && !indices)
        return E_INVALIDARG;
    const auto snapshot = snapshot_;
    const auto required =
        snapshot ? static_cast<UINT>(snapshot->pages.size()) : 0;
    *count = required;
    if (capacity < required)
        return S_FALSE;
    if (required)
        std::copy(snapshot->pages.begin(), snapshot->pages.end(), indices);
    return S_OK;
}
STDMETHODIMP CandidateUIElement::SetPageIndex(UINT *indices, UINT count) {
    const auto snapshot = snapshot_;
    if (!show_ || !snapshot)
        return E_UNEXPECTED;
    if (!indices || !count || count > snapshot->strings.size() ||
        indices[0] != 0)
        return E_INVALIDARG;
    for (UINT i = 0; i < count; ++i)
        if (indices[i] >= snapshot->strings.size() ||
            (i && indices[i] <= indices[i - 1]))
            return E_INVALIDARG;
    try {
        auto next = std::make_shared<Snapshot>(*snapshot);
        next->pages.assign(indices, indices + count);
        snapshot_ = std::move(next);
    } catch (const std::bad_alloc &) {
        return E_OUTOFMEMORY;
    }
    updated_ = TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
    return S_OK;
}
STDMETHODIMP CandidateUIElement::GetCurrentPage(UINT *page) {
    if (!page)
        return E_INVALIDARG;
    *page = snapshot_ ? currentPage(snapshot_->pages, snapshot_->selection) : 0;
    return S_OK;
}
DWORD CandidateUIElement::publish(const win32::ipc::KeyReply &reply,
                                  ITfDocumentMgr *document) {
    auto next = std::make_shared<Snapshot>();
    next->document = document;
    for (const auto &candidate : reply.candidates) {
        std::wstring text;
        if (!utf8ToWide(candidate.text, text))
            return 0;
        next->strings.push_back(std::move(text));
    }
    next->selection =
        reply.selected < next->strings.size() ? reply.selected : UINT_MAX;
    next->generation = reply.candidateGeneration;
    if (pendingGeneration_ != next->generation) {
        pendingGeneration_ = 0;
        pendingSelection_ = UINT_MAX;
    }
    const auto previous = snapshot_;
    DWORD flags = previous ? 0 : kAllChanges;
    if (previous) {
        if (previous->document != next->document)
            flags |= TF_CLUIE_DOCUMENTMGR;
        if (previous->strings.size() != next->strings.size())
            flags |= TF_CLUIE_COUNT;
        if (previous->strings != next->strings)
            flags |=
                TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
        else
            next->pages = previous->pages;
        if (previous->selection != next->selection)
            flags |= TF_CLUIE_SELECTION | TF_CLUIE_CURRENTPAGE;
    }
    if (next->pages.empty() && !next->strings.empty())
        next->pages = {0};
    snapshot_ = std::move(next);
    updated_ = flags;
    return flags;
}
void CandidateUIElement::detach() {
    visible_ = false;
    show_ = {};
    action_ = {};
    snapshot_.reset();
    updated_ = kAllChanges;
}

STDMETHODIMP CandidateUIElement::SetSelection(UINT index) {
    const auto snapshot = snapshot_;
    if (!action_ || !snapshot || !snapshot->generation)
        return E_UNEXPECTED;
    if (index >= snapshot->strings.size())
        return E_INVALIDARG;
    const auto action = action_;
    const auto result = action(win32::ipc::CandidateAction::Select,
                               snapshot->generation, index);
    if (SUCCEEDED(result) && snapshot_ == snapshot) {
        pendingSelection_ = index;
        pendingGeneration_ = snapshot->generation;
    }
    return result;
}
STDMETHODIMP CandidateUIElement::Finalize() {
    const auto snapshot = snapshot_;
    if (!action_ || !snapshot || !snapshot->generation)
        return E_UNEXPECTED;
    const auto selection = pendingGeneration_ == snapshot->generation
                               ? pendingSelection_
                               : snapshot->selection;
    if (selection >= snapshot->strings.size())
        return E_INVALIDARG;
    const auto action = action_;
    return action(win32::ipc::CandidateAction::Finalize, snapshot->generation,
                  selection);
}
STDMETHODIMP CandidateUIElement::Abort() {
    const auto snapshot = snapshot_;
    if (!action_ || !snapshot || !snapshot->generation)
        return E_UNEXPECTED;
    const auto action = action_;
    return action(win32::ipc::CandidateAction::Abort, snapshot->generation, 0);
}

} // namespace fcitx
