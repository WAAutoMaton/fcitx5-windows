#include "diagnostics.h"
#include "dpiawareness.h"
#include "textutil.h"
#include "tsf.h"
#include <new>
#include <textstor.h>

extern void DllAddRef();
extern void DllRelease();

namespace fcitx {

class SnapshotEditSession final : public ITfEditSession {
  public:
    SnapshotEditSession(Tsf *owner, ITfContext *context,
                        PipeClient::KeyReply reply, uint64_t generation)
        : owner_(owner), context_(context), reply_(std::move(reply)),
          generation_(generation) {
        owner_->AddRef();
    }
    ~SnapshotEditSession() { owner_->Release(); }
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result) {
            return E_INVALIDARG;
        }
        *result = nullptr;
        if (id == IID_IUnknown || id == IID_ITfEditSession) {
            *result = static_cast<ITfEditSession *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&references_);
    }
    STDMETHODIMP_(ULONG) Release() override {
        const auto remaining = InterlockedDecrement(&references_);
        if (!remaining) {
            delete this;
        }
        return remaining;
    }
    STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
        const auto started = GetTickCount64();
        const auto result =
            owner_->applySnapshot(cookie, context_, reply_, generation_);
        if (win32::diagnostic::tsfLogger().enabled())
            win32::diagnostic::tsfLogger().event(
                win32::diagnostic::Level::Debug, "edit_execute",
                {{"generation", generation_},
                 {"valid_context",
                  context_ == owner_->textEditSinkContext_ ? "true" : "false"},
                 {"hr", static_cast<uint32_t>(result)},
                 {"elapsed_ms", GetTickCount64() - started}});
        owner_->finishEdit(generation_, result);
        return result;
    }

  private:
    LONG references_ = 1;
    Tsf *owner_;
    CComPtr<ITfContext> context_;
    PipeClient::KeyReply reply_;
    uint64_t generation_;
};

class CancelEditSession final : public ITfEditSession {
  public:
    explicit CancelEditSession(ITfComposition *composition)
        : composition_(composition) {
        DllAddRef();
    }
    ~CancelEditSession() { DllRelease(); }
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result) {
            return E_INVALIDARG;
        }
        *result = nullptr;
        if (id == IID_IUnknown || id == IID_ITfEditSession) {
            *result = static_cast<ITfEditSession *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&references_);
    }
    STDMETHODIMP_(ULONG) Release() override {
        const auto remaining = InterlockedDecrement(&references_);
        if (!remaining) {
            delete this;
        }
        return remaining;
    }
    STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
        CComPtr<ITfRange> range;
        const auto result = composition_->GetRange(&range);
        if (FAILED(result) || !range) {
            return E_FAIL;
        }
        const auto textResult = range->SetText(cookie, 0, L"", 0);
        const auto endResult = composition_->EndComposition(cookie);
        return FAILED(textResult) ? textResult : endResult;
    }

  private:
    LONG references_ = 1;
    CComPtr<ITfComposition> composition_;
};

class CandidatePositionEditSession final : public ITfEditSession {
  public:
    CandidatePositionEditSession(Tsf *owner, ITfContext *context,
                                 uint64_t generation)
        : owner_(owner), context_(context), generation_(generation) {
        owner_->AddRef();
    }
    ~CandidatePositionEditSession() { owner_->Release(); }
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result) {
            return E_INVALIDARG;
        }
        *result = nullptr;
        if (id == IID_IUnknown || id == IID_ITfEditSession) {
            *result = static_cast<ITfEditSession *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&references_);
    }
    STDMETHODIMP_(ULONG) Release() override {
        const auto remaining = InterlockedDecrement(&references_);
        if (!remaining) {
            delete this;
        }
        return remaining;
    }
    STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
        if (generation_ == owner_->generation_) {
            owner_->candidatePositionRequested_ = false;
        }
        owner_->refreshCandidatePosition(cookie, context_, generation_);
        return S_OK;
    }

  private:
    LONG references_ = 1;
    Tsf *owner_;
    CComPtr<ITfContext> context_;
    uint64_t generation_;
};

void Tsf::cancelComposition() {
    if (!composition_ || !textEditSinkContext_) {
        return;
    }
    CComPtr<ITfEditSession> session;
    session.Attach(new (std::nothrow) CancelEditSession(composition_));
    composition_.Release();
    if (session) {
        HRESULT result = E_FAIL;
        textEditSinkContext_->RequestEditSession(
            clientId_, session, TF_ES_ASYNC | TF_ES_READWRITE, &result);
    }
}

bool Tsf::submitSnapshot(PipeClient::KeyReply reply, bool synchronous) {
    if (!textEditSinkContext_ ||
        (reply.revision && reply.revision <= state_.revision)) {
        win32::diagnostic::tsfLogger().event(win32::diagnostic::Level::Debug,
                                             "snapshot_rejected");
        return false;
    }
    acceptInputMethod(reply);
    state_ = reply;
    state_.commit.clear();
    edits_.push_back({textEditSinkContext_, std::move(reply), generation_});
    return editRequested_ || requestNextEdit(synchronous);
}

bool Tsf::requestCandidatePosition() {
    if (candidateLayoutPending_ &&
        GetTickCount64() >= candidateLayoutDeadline_) {
        candidates_.hide();
        if (candidateElement_)
            candidateElement_->setVisible(false);
    }
    if (!textEditSinkContext_ || displayedCandidates_.candidates.empty() ||
        candidatePositionRequested_) {
        return false;
    }
    CComPtr<ITfEditSession> session;
    CComPtr<ITfContext> context = textEditSinkContext_;
    const auto generation = generation_;
    session.Attach(new (std::nothrow)
                       CandidatePositionEditSession(this, context, generation));
    if (!session) {
        return false;
    }
    HRESULT result = E_FAIL;
    candidatePositionRequested_ = true;
    auto request = context->RequestEditSession(
        clientId_, session, TF_ES_SYNC | TF_ES_READ, &result);
    if (generation != generation_ || context != textEditSinkContext_) {
        return false;
    }
    if (FAILED(request) || result == TF_E_SYNCHRONOUS ||
        result == TF_E_LOCKED) {
        result = E_FAIL;
        request = context->RequestEditSession(
            clientId_, session, TF_ES_ASYNC | TF_ES_READ, &result);
    }
    if (FAILED(request) || FAILED(result)) {
        candidatePositionRequested_ = false;
        return false;
    }
    return true;
}

void Tsf::refreshCandidatePosition(TfEditCookie cookie, ITfContext *context,
                                   uint64_t generation) {
    if (generation != generation_ || context != textEditSinkContext_ ||
        !foreground_ || displayedCandidates_.candidates.empty()) {
        win32::diagnostic::tsfLogger().event(win32::diagnostic::Level::Debug,
                                             "candidate_refresh_rejected");
        return;
    }
    if (displayedCandidates_.settingsRevision < state_.settingsRevision) {
        hideCandidates();
        return;
    }
    updateCandidates(cookie, context, displayedCandidates_);
}

void Tsf::hideCandidates() {
    candidates_.hide();
    auto element = candidateElement_;
    candidateElement_.Release();
    const auto id = candidateElementId_;
    const auto active = candidateElementActive_;
    candidateElementActive_ = false;
    showOwnCandidates_ = !modernUi_;
    if (element)
        element->detach();
    candidateLayoutPending_ = false;
    candidateLayoutDeadline_ = 0;
    displayedCandidates_ = {};
    candidateView_.Release();
    candidateOwner_ = nullptr;
    if (active && uiElementMgr_) {
        CComPtr<ITfUIElementMgr> manager = uiElementMgr_;
        const auto result = manager->EndUIElement(id);
        diagnosticResult(L"EndUIElement", result);
    }
}

void Tsf::showCandidateUi(bool show) {
    showOwnCandidates_ = show;
    if (!show) {
        candidates_.hide();
        if (candidateElement_)
            candidateElement_->setVisible(false);
    } else if (messageWindow_ && !candidateRefreshPosted_) {
        candidateRefreshPosted_ =
            PostMessageW(messageWindow_, kRefreshCandidateMessage, 0, 0) !=
            FALSE;
    }
}

bool Tsf::publishCandidates(ITfContext *context,
                            const PipeClient::KeyReply &reply) {
    if (!uiElementMgr_)
        return !modernUi_;
    CComPtr<ITfDocumentMgr> document;
    if (FAILED(context->GetDocumentMgr(&document)))
        return !modernUi_;
    const auto generation = generation_;
    CComPtr<ITfUIElementMgr> manager = uiElementMgr_;
    if (!candidateElement_) {
        CComPtr<CandidateUIElement> element;
        element.Attach(new (std::nothrow) CandidateUIElement(
            [this](bool show) { showCandidateUi(show); },
            [this](ipc::CandidateAction action, uint64_t identity,
                   uint32_t index) {
                return queueCandidateAction(action, identity, index);
            }));
        if (!element)
            return false;
        element->publish(reply, document);
        candidateElement_ = element;
        BOOL show = TRUE;
        DWORD id = 0;
        const auto showSerial = element->showSerial();
        const auto result = manager->BeginUIElement(element, &show, &id);
        if (win32::diagnostic::tsfLogger().enabled())
            win32::diagnostic::tsfLogger().event(
                win32::diagnostic::Level::Debug, "ui_element_begin",
                {{"hr", static_cast<uint32_t>(result)},
                 {"show", show ? "true" : "false"},
                 {"element", id}});
        diagnosticResult(show ? L"BeginUIElement own UI"
                              : L"BeginUIElement host UI",
                         result);
        if (generation != generation_ || context != textEditSinkContext_ ||
            candidateElement_ != element) {
            element->detach();
            if (SUCCEEDED(result))
                manager->EndUIElement(id);
            return false;
        }
        if (FAILED(result)) {
            element->detach();
            candidateElement_.Release();
            showOwnCandidates_ = !modernUi_;
            OutputDebugStringW(L"Fcitx5: BeginUIElement failed.\n");
            return !modernUi_;
        }
        candidateElementId_ = id;
        candidateElementActive_ = true;
        if (!show || element->showSerial() == showSerial)
            showCandidateUi(show != FALSE);
    } else {
        CComPtr<CandidateUIElement> element = candidateElement_;
        const auto changes = element->publish(reply, document);
        if (changes && candidateElementActive_) {
            const auto result = manager->UpdateUIElement(candidateElementId_);
            diagnosticResult(L"UpdateUIElement", result);
            if (FAILED(result)) {
                showCandidateUi(false);
                OutputDebugStringW(L"Fcitx5: UpdateUIElement failed.\n");
                return false;
            }
        }
        if (generation != generation_ || context != textEditSinkContext_ ||
            candidateElement_ != element)
            return false;
    }
    return showOwnCandidates_;
}

HRESULT Tsf::queueCandidateAction(ipc::CandidateAction action,
                                  uint64_t identity, uint32_t index) {
    if (!messageWindow_ || !foreground_ ||
        !contextAllowsInput(textEditSinkContext_) || !remoteContextId_ ||
        !identity || identity != displayedCandidates_.candidateGeneration)
        return E_UNEXPECTED;
    if (candidateActions_.size() >= 32)
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    try {
        candidateActions_.push_back({textEditSinkContext_, generation_,
                                     remoteContextId_, identity, action,
                                     index});
    } catch (const std::bad_alloc &) {
        return E_OUTOFMEMORY;
    }
    if (!PostMessageW(messageWindow_, kCandidateActionMessage, 0, 0)) {
        candidateActions_.pop_back();
        return E_FAIL;
    }
    return S_OK;
}

void Tsf::processCandidateActions() {
    if (candidateActions_.empty())
        return;
    const auto operation = candidateActions_.front();
    candidateActions_.pop_front();
    if (operation.context != textEditSinkContext_ ||
        operation.generation != generation_ ||
        operation.remoteContext != remoteContextId_ || !foreground_)
        return;
    if (!refreshContextPolicy()) {
        clearRemoteContext();
        return;
    }
    const auto previous = state_;
    PipeClient::KeyReply reply;
    if (!pipe_.candidateAction(operation.remoteContext,
                               operation.candidateGeneration, operation.action,
                               operation.index, reply)) {
        candidateActions_.clear();
        hideCandidates();
        OutputDebugStringW(
            L"Fcitx5: candidate action rejected or transport unavailable.\n");
        if (!pipe_.connected())
            clearRemoteContext();
        return;
    }
    if (operation.context != textEditSinkContext_ ||
        operation.generation != generation_ ||
        operation.remoteContext != remoteContextId_)
        return;
    if (operation.action == ipc::CandidateAction::Select &&
        reply.candidates == previous.candidates &&
        reply.preedit == previous.preedit &&
        reply.inputMethod == previous.inputMethod &&
        reply.settingsRevision == previous.settingsRevision) {
        for (auto &queued : candidateActions_)
            if (queued.context == operation.context &&
                queued.generation == operation.generation &&
                queued.candidateGeneration == operation.candidateGeneration)
                queued.candidateGeneration = reply.candidateGeneration;
    }
    submitSnapshot(std::move(reply), false);
}

void Tsf::updateCandidates(TfEditCookie cookie, ITfContext *context,
                           const PipeClient::KeyReply &snapshot) {
    const auto reply = snapshot;
    if (reply.candidates.empty()) {
        hideCandidates();
        return;
    }
    displayedCandidates_ = reply;
    displayedCandidates_.commit.clear();
    if (!publishCandidates(context, reply)) {
        candidates_.hide();
        return;
    }
    if (candidateElement_)
        candidateElement_->setVisible(false);
    CComPtr<ITfContextView> view;
    HWND window = nullptr;
    DWORD ownerProcess = 0;
    const auto viewResult = context->GetActiveView(&view);
    const auto windowResult =
        SUCCEEDED(viewResult) && view ? view->GetWnd(&window) : E_FAIL;
    diagnosticResult(L"GetActiveView", viewResult);
    diagnosticResult(L"GetWnd", windowResult);
    if (FAILED(viewResult) || !view || FAILED(windowResult) || !window ||
        !IsWindowVisible(window) || IsIconic(window) ||
        !GetWindowThreadProcessId(window, &ownerProcess) ||
        ownerProcess != GetCurrentProcessId()) {
        if (win32::diagnostic::tsfLogger().enabled())
            win32::diagnostic::tsfLogger().event(
                win32::diagnostic::Level::Debug, "candidate_hidden",
                {{"reason", "owner_unavailable"},
                 {"window_present", window ? "true" : "false"},
                 {"visible",
                  window && IsWindowVisible(window) ? "true" : "false"},
                 {"minimized", window && IsIconic(window) ? "true" : "false"},
                 {"owner_matches",
                  ownerProcess == GetCurrentProcessId() ? "true" : "false"}});
        candidates_.hide();
        return;
    }
    if (view != candidateView_ || window != candidateOwner_) {
        candidates_.hide();
        candidateView_ = view;
        candidateOwner_ = window;
    }
    CComPtr<ITfRange> range;
    if (composition_) {
        composition_->GetRange(&range);
    }
    TF_SELECTION selection{};
    ULONG fetched = 0;
    CComPtr<ITfRange> caret;
    if (SUCCEEDED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1,
                                        &selection, &fetched)) &&
        fetched) {
        caret.Attach(selection.range);
    }
    if (!range) {
        range = caret;
    }
    if (!range) {
        win32::diagnostic::tsfLogger().event(win32::diagnostic::Level::Debug,
                                             "candidate_hidden",
                                             {{"reason", "no_range"}});
        candidates_.hide();
        return;
    }
    RECT anchor{};
    BOOL clipped = FALSE;
    ScopedDpiAwareness scope(window ? GetWindowDpiAwarenessContext(window)
                                    : GetThreadDpiAwarenessContext());
    if (!scope.valid()) {
        candidates_.hide();
        return;
    }
    auto result = view->GetTextExt(cookie, range, &anchor, &clipped);
    diagnosticResult(L"GetTextExt", result);
    bool fallback = false;
    // A clipped or empty composition can still have a usable caret.
    if ((result == TS_E_NOLAYOUT ||
         (SUCCEEDED(result) && (clipped || anchor.bottom <= anchor.top ||
                                anchor.right < anchor.left))) &&
        caret && SUCCEEDED(caret->Collapse(cookie, TF_ANCHOR_END))) {
        result = view->GetTextExt(cookie, caret, &anchor, &clipped);
        fallback = true;
    }
    if (win32::diagnostic::tsfLogger().enabled())
        win32::diagnostic::tsfLogger().event(
            win32::diagnostic::Level::Debug, "candidate_extent",
            {{"hr", static_cast<uint32_t>(result)},
             {"clipped", clipped ? "true" : "false"},
             {"caret_fallback", fallback ? "true" : "false"},
             {"left", anchor.left},
             {"top", anchor.top},
             {"right", anchor.right},
             {"bottom", anchor.bottom}});
    if (result == TS_E_NOLAYOUT) {
        win32::diagnostic::tsfLogger().event(win32::diagnostic::Level::Debug,
                                             "candidate_layout_pending",
                                             {{"reason", "no_layout"}});
        if (!candidateLayoutPending_) {
            candidateLayoutDeadline_ = GetTickCount64() + 250;
            if (messageWindow_ && !candidateRefreshPosted_) {
                candidateRefreshPosted_ =
                    PostMessageW(messageWindow_, kRefreshCandidateMessage, 0,
                                 0) != FALSE;
            }
        }
        candidateLayoutPending_ = true;
        if (GetTickCount64() < candidateLayoutDeadline_) {
            candidates_.update(reply);
        } else {
            candidates_.hide();
        }
        if (candidateElement_)
            candidateElement_->setVisible(candidates_.visible());
        return;
    }
    // Some game hosts provide only an insertion point, even for the caret.
    // Preserve its coordinates when it lies inside the document display. The
    // all-zero rectangle is TSF's sentinel for text that is not visible.
    bool pointVisible = false;
    if (SUCCEEDED(result) && !clipped && anchor.left == anchor.right &&
        anchor.top == anchor.bottom && (anchor.left || anchor.top)) {
        RECT screen{};
        const auto screenResult = view->GetScreenExt(&screen);
        diagnosticResult(L"GetScreenExt", screenResult);
        pointVisible = SUCCEEDED(screenResult) && anchor.left >= screen.left &&
                       anchor.left < screen.right && anchor.top >= screen.top &&
                       anchor.top < screen.bottom;
        win32::diagnostic::tsfLogger().event(
            win32::diagnostic::Level::Debug, "candidate_point_anchor",
            {{"hr", static_cast<uint32_t>(screenResult)},
             {"visible", pointVisible ? "true" : "false"},
             {"left", screen.left},
             {"top", screen.top},
             {"right", screen.right},
             {"bottom", screen.bottom}});
    }
    bool clippedCaretVisible = false;
    if (SUCCEEDED(result) && clipped && fallback &&
        anchor.bottom > anchor.top && anchor.right >= anchor.left) {
        // Some hosts flag even a visible collapsed caret as clipped. Validate
        // its geometry against the document display before hiding the popup.
        RECT screen{};
        const auto screenResult = view->GetScreenExt(&screen);
        diagnosticResult(L"GetScreenExt", screenResult);
        const bool horizontalOverlap =
            anchor.left == anchor.right
                ? anchor.left >= screen.left && anchor.left < screen.right
                : anchor.left < screen.right && anchor.right > screen.left;
        clippedCaretVisible =
            SUCCEEDED(screenResult) && screen.right > screen.left &&
            screen.bottom > screen.top && horizontalOverlap &&
            anchor.top < screen.bottom && anchor.bottom > screen.top;
        win32::diagnostic::tsfLogger().event(
            win32::diagnostic::Level::Debug, "candidate_clipped_caret",
            {{"hr", static_cast<uint32_t>(screenResult)},
             {"visible", clippedCaretVisible ? "true" : "false"},
             {"left", screen.left},
             {"top", screen.top},
             {"right", screen.right},
             {"bottom", screen.bottom}});
    }
    if (FAILED(result) || (clipped && !clippedCaretVisible) ||
        (anchor.bottom <= anchor.top && !pointVisible) ||
        anchor.right < anchor.left) {
        win32::diagnostic::tsfLogger().event(
            win32::diagnostic::Level::Debug, "candidate_hidden",
            {{"reason", FAILED(result) ? "extent_failed"
                        : clipped      ? "clipped"
                                       : "empty_rectangle"}});
        candidates_.hide();
        return;
    }
    candidateLayoutPending_ = false;
    candidateLayoutDeadline_ = 0;
    candidates_.show(reply, window, anchor);
    if (candidateElement_)
        candidateElement_->setVisible(candidates_.visible());
}

bool Tsf::requestNextEdit(bool synchronous) {
    if (editRequested_ || edits_.empty()) {
        return true;
    }
    const auto operation = edits_.front();
    CComPtr<ITfEditSession> session;
    session.Attach(new (std::nothrow) SnapshotEditSession(
        this, operation.context, operation.reply, operation.generation));
    if (!session) {
        finishEdit(operation.generation, E_OUTOFMEMORY);
        return false;
    }
    editRequested_ = true;
    HRESULT result = E_FAIL;
    auto request = operation.context->RequestEditSession(
        clientId_, session,
        (synchronous ? TF_ES_SYNC : TF_ES_ASYNC) | TF_ES_READWRITE, &result);
    if (win32::diagnostic::tsfLogger().enabled())
        win32::diagnostic::tsfLogger().event(
            win32::diagnostic::Level::Debug, "edit_request",
            {{"synchronous", synchronous ? "true" : "false"},
             {"queue_length", edits_.size()},
             {"request_hr", static_cast<uint32_t>(request)},
             {"session_hr", static_cast<uint32_t>(result)}});
    if (synchronous && (FAILED(request) || result == TF_E_SYNCHRONOUS ||
                        result == TF_E_LOCKED)) {
        result = E_FAIL;
        request = operation.context->RequestEditSession(
            clientId_, session, TF_ES_ASYNC | TF_ES_READWRITE, &result);
    }
    if (FAILED(request) || FAILED(result)) {
        if (editRequested_ && operation.generation == generation_) {
            finishEdit(operation.generation,
                       FAILED(request) ? request : result);
        }
        return false;
    }
    return true;
}

void Tsf::finishEdit(uint64_t generation, HRESULT result) {
    if (generation != generation_) {
        return;
    }
    editRequested_ = false;
    if (!edits_.empty()) {
        edits_.pop_front();
    }
    if (FAILED(result)) {
        ++generation_;
        candidateActions_.clear();
        candidatePositionRequested_ = false;
        edits_.clear();
        cancelComposition();
        displayedPreedit_.clear();
        displayedCursor_ = 0;
        PipeClient::KeyReply empty;
        if (remoteContextId_) {
            pipe_.reset(remoteContextId_, empty);
        }
        state_ = std::move(empty);
        hideCandidates();
        OutputDebugStringW(
            L"Fcitx5: document edit failed; input state reset\n");
    } else if (!edits_.empty() && messageWindow_) {
        PostMessageW(messageWindow_, WM_APP + 1, 0, 0);
    }
}

HRESULT Tsf::applySnapshot(TfEditCookie cookie, ITfContext *context,
                           const PipeClient::KeyReply &snapshot,
                           uint64_t generation) {
    if (generation != generation_ || context != textEditSinkContext_) {
        win32::diagnostic::tsfLogger().event(win32::diagnostic::Level::Debug,
                                             "edit_stale");
        return S_OK;
    }
    if (!scopeAllowsInputAtEdit(context, cookie))
        return E_ACCESSDENIED;
    auto reply = snapshot;
    // Preserve commits while preventing old schemes from recreating preedit.
    if (reply.settingsRevision < state_.settingsRevision) {
        reply.preedit.clear();
        reply.preeditCursor = 0;
        reply.candidates.clear();
    }
    std::wstring commit, preedit;
    ULONG cursor = 0;
    if (!utf8ToWide(reply.commit, commit) ||
        !preeditToWide(reply.preedit, reply.preeditCursor, preedit, cursor)) {
        return E_INVALIDARG;
    }
    CComPtr<ITfRange> range;
    if (!commit.empty()) {
        hideCandidates();
        if (composition_) {
            CComPtr<ITfComposition> composition = composition_;
            if (FAILED(composition->GetRange(&range)) || !range ||
                FAILED(range->SetText(cookie, 0, commit.c_str(),
                                      static_cast<LONG>(commit.size())))) {
                return E_FAIL;
            }
            CComPtr<ITfProperty> attribute;
            if (SUCCEEDED(
                    context->GetProperty(GUID_PROP_ATTRIBUTE, &attribute)) &&
                attribute) {
                attribute->Clear(cookie, range);
            }
            composition_.Release();
            if (FAILED(composition->EndComposition(cookie))) {
                return E_FAIL;
            }
        } else {
            CComPtr<ITfInsertAtSelection> insertion;
            if (FAILED(context->QueryInterface(IID_PPV_ARGS(&insertion))) ||
                FAILED(insertion->InsertTextAtSelection(
                    cookie, 0, commit.c_str(), static_cast<LONG>(commit.size()),
                    &range)) ||
                !range) {
                return E_FAIL;
            }
        }
        if (FAILED(range->Collapse(cookie, TF_ANCHOR_END))) {
            return E_FAIL;
        }
        TF_SELECTION selection{range, {TF_AE_END, FALSE}};
        if (FAILED(context->SetSelection(cookie, 1, &selection))) {
            return E_FAIL;
        }
        displayedPreedit_.clear();
        displayedCursor_ = 0;
    }
    bool textChanged = displayedPreedit_ != reply.preedit;
    bool selectionChanged =
        textChanged || displayedCursor_ != reply.preeditCursor;
    if (preedit.empty() && composition_) {
        hideCandidates();
        CComPtr<ITfComposition> composition = composition_;
        range.Release();
        if (FAILED(composition->GetRange(&range)) || !range ||
            FAILED(range->SetText(cookie, 0, L"", 0))) {
            return E_FAIL;
        }
        composition_.Release();
        if (FAILED(composition->EndComposition(cookie))) {
            return E_FAIL;
        }
    } else if (!preedit.empty()) {
        if (!composition_) {
            textChanged = selectionChanged = true;
            CComPtr<ITfInsertAtSelection> insertion;
            CComPtr<ITfContextComposition> compositions;
            range.Release();
            if (FAILED(context->QueryInterface(IID_PPV_ARGS(&insertion))) ||
                FAILED(insertion->InsertTextAtSelection(
                    cookie, TF_IAS_QUERYONLY, nullptr, 0, &range)) ||
                !range ||
                FAILED(context->QueryInterface(IID_PPV_ARGS(&compositions))) ||
                FAILED(compositions->StartComposition(cookie, range, this,
                                                      &composition_)) ||
                !composition_) {
                return E_FAIL;
            }
        }
        range.Release();
        if (FAILED(composition_->GetRange(&range)) || !range) {
            return E_FAIL;
        }
        if (textChanged) {
            if (FAILED(range->SetText(cookie, 0, preedit.c_str(),
                                      static_cast<LONG>(preedit.size())))) {
                return E_FAIL;
            }
            if (attributeAtom_ != TF_INVALID_GUIDATOM) {
                CComPtr<ITfProperty> attribute;
                if (SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE,
                                                   &attribute)) &&
                    attribute) {
                    CComVariant value(static_cast<LONG>(attributeAtom_));
                    attribute->SetValue(cookie, range, &value);
                }
            }
        }
        if (selectionChanged) {
            // Cursor movement must not replace text or reset its display
            // attributes.
            CComPtr<ITfRange> caret;
            LONG moved = 0;
            if (FAILED(range->Clone(&caret)) ||
                FAILED(caret->Collapse(cookie, TF_ANCHOR_START)) ||
                FAILED(caret->ShiftStart(cookie, static_cast<LONG>(cursor),
                                         &moved, nullptr)) ||
                moved != static_cast<LONG>(cursor) ||
                FAILED(caret->Collapse(cookie, TF_ANCHOR_START))) {
                return E_FAIL;
            }
            TF_SELECTION selection{caret, {TF_AE_END, FALSE}};
            if (FAILED(context->SetSelection(cookie, 1, &selection))) {
                return E_FAIL;
            }
        }
    }
    displayedPreedit_ = reply.preedit;
    displayedCursor_ = reply.preeditCursor;
    updateCandidates(cookie, context, reply);
    return S_OK;
}

} // namespace fcitx
