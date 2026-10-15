#include "../tsf/tsf.h"
#include "textstore.h"
#include <cassert>
#include <cstdio>
#include <fstream>

namespace {
LONG dllReferences = 0;
}

void DllAddRef() { ++dllReferences; }
void DllRelease() { --dllReferences; }

namespace fcitx {
struct CandidateWindowTestAccess {
    static HWND window(const CandidateWindow &view) { return view.window_; }
    static std::wstring text(const CandidateWindow &view) {
        return view.rows_.front().text.value;
    }
};

struct TsfCandidateTestAccess {
    static void initialize(Tsf &tsf, TfClientId client,
                           ITfDocumentMgr *document) {
        tsf.clientId_ = client;
        assert(tsf.initMessageWindow());
        // Exercise layout dispatch without polling or starting a Core process.
        KillTimer(tsf.messageWindow_, 1);
        assert(tsf.initTextEditSink(document));
        assert(tsf.textLayoutSinkCookie_ != TF_INVALID_COOKIE);
    }
    static bool submit(Tsf &tsf, PipeClient::KeyReply reply) {
        return tsf.submitSnapshot(std::move(reply), true);
    }
    static CandidateWindow &candidates(Tsf &tsf) { return tsf.candidates_; }
    static void refresh(Tsf &tsf) { tsf.requestCandidatePosition(); }
    static bool positionPending(const Tsf &tsf) {
        return tsf.candidatePositionRequested_;
    }
    static void detach(Tsf &tsf) { tsf.initTextEditSink(nullptr); }
    static void attach(Tsf &tsf, ITfDocumentMgr *document) {
        assert(tsf.initTextEditSink(document));
    }
    static void queueFutureState(Tsf &tsf, const PipeClient::KeyReply &reply) {
        tsf.state_ = reply;
    }
    static void uiManager(Tsf &tsf, ITfUIElementMgr *manager) {
        tsf.uiElementMgr_ = manager;
        tsf.modernUi_ = true;
    }
    static bool inputAllowed(Tsf &tsf) { return tsf.refreshContextPolicy(); }
    static void staleEdit(Tsf &tsf) {
        assert(tsf.applySnapshot(0, nullptr, {}, tsf.generation_ + 1) == S_OK);
    }
};
} // namespace fcitx

namespace {
using Access = fcitx::TsfCandidateTestAccess;
using WindowAccess = fcitx::CandidateWindowTestAccess;
using Reply = fcitx::PipeClient::KeyReply;

class UIElementManager : public ITfUIElementMgr {
  public:
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result)
            return E_INVALIDARG;
        *result = nullptr;
        if (id != IID_IUnknown && id != IID_ITfUIElementMgr)
            return E_NOINTERFACE;
        *result = static_cast<ITfUIElementMgr *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references; }
    STDMETHODIMP_(ULONG) Release() override { return --references; }
    STDMETHODIMP BeginUIElement(ITfUIElement *value, BOOL *show,
                                DWORD *id) override {
        ++begins;
        element = value;
        if (candidateOnly) {
            CComPtr<ITfCandidateListUIElement> list;
            assert(SUCCEEDED(value->QueryInterface(IID_PPV_ARGS(&list))));
            UINT count = 0;
            assert(list->GetCount(&count) == S_OK && count);
        }
        *show = ownUi;
        *id = 23;
        if (beginCallback)
            beginCallback();
        return beginResult;
    }
    STDMETHODIMP UpdateUIElement(DWORD id) override {
        assert(id == 23);
        ++updates;
        CComPtr<ITfCandidateListUIElement> list;
        assert(SUCCEEDED(element->QueryInterface(IID_PPV_ARGS(&list))));
        assert(list->GetUpdatedFlags(&lastFlags) == S_OK);
        return updateResult;
    }
    STDMETHODIMP EndUIElement(DWORD id) override {
        assert(id == 23);
        ++ends;
        if (endCallback)
            endCallback();
        return S_OK;
    }
    STDMETHODIMP GetUIElement(DWORD, ITfUIElement **result) override {
        return element.CopyTo(result);
    }
    STDMETHODIMP EnumUIElements(IEnumTfUIElements **result) override {
        if (result)
            *result = nullptr;
        return E_NOTIMPL;
    }
    ULONG references = 1;
    unsigned begins = 0, updates = 0, ends = 0;
    DWORD lastFlags = 0;
    BOOL ownUi = FALSE;
    bool candidateOnly = true;
    HRESULT beginResult = S_OK, updateResult = S_OK;
    CComPtr<ITfUIElement> element;
    std::function<void()> beginCallback, endCallback;
};

void pump(DWORD duration = 30) {
    const auto deadline = GetTickCount64() + duration;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    } while (GetTickCount64() < deadline);
}

Reply snapshot(uint64_t revision, const char *preedit, const char *candidate) {
    Reply reply;
    reply.revision = revision;
    reply.preedit = preedit;
    reply.preeditCursor = static_cast<uint32_t>(reply.preedit.size());
    if (candidate) {
        reply.candidates = {{candidate, "1", ""}};
        reply.selected = 0;
    }
    return reply;
}

void tests() {
    CComPtr<ITfThreadMgr> manager;
    assert(SUCCEEDED(manager.CoCreateInstance(CLSID_TF_ThreadMgr)));
    TfClientId client = TF_CLIENTID_NULL;
    assert(SUCCEEDED(manager->Activate(&client)));
    CComPtr<fcitx::test::TextStore> store;
    store.Attach(new fcitx::test::TextStore);
    ShowWindow(store->window(), SW_SHOWNOACTIVATE);
    CComPtr<ITfDocumentMgr> document;
    CComPtr<ITfContext> context;
    TfEditCookie cookie;
    assert(SUCCEEDED(manager->CreateDocumentMgr(&document)));
    assert(SUCCEEDED(document->CreateContext(
        client, 0, static_cast<ITextStoreACP *>(store.p), &context, &cookie)));
    assert(SUCCEEDED(document->Push(context)));

    CComPtr<fcitx::Tsf> tsf;
    tsf.Attach(new fcitx::Tsf);
    Access::initialize(*tsf, client, document);
    CComPtr<ITfTextLayoutSink> layout;
    assert(SUCCEEDED(tsf->QueryInterface(IID_PPV_ARGS(&layout))));
    auto &window = Access::candidates(*tsf);
    const auto foreground = GetForegroundWindow();
    assert(Access::submit(*tsf, snapshot(1, "n", "first")));
    assert(store->text == L"n" && window.visible());
    const auto hwnd = WindowAccess::window(window);
    RECT before{};
    assert(GetWindowRect(hwnd, &before));

    store->geometryResult = TS_E_NOLAYOUT;
    assert(Access::submit(*tsf, snapshot(2, "ni", "new")));
    assert(store->text == L"ni" && window.visible());
    assert(WindowAccess::window(window) == hwnd);
    assert(WindowAccess::text(window) == L"new");
    RECT after{};
    assert(GetWindowRect(hwnd, &after));
    assert(after.left == before.left && after.top == before.top);
    pump();
    assert(window.visible());

    // Layout notifications must display the applied snapshot, not queued input.
    Access::queueFutureState(*tsf, snapshot(3, "future", "unapplied"));
    store->geometryResult = S_OK;
    store->anchorX = 180;
    const auto writes = store->writes;
    const auto locks = store->readLocks;
    const auto geometry = store->geometryCalls;
    assert(SUCCEEDED(store->notifyLayout()));
    assert(SUCCEEDED(store->notifyLayout()));
    pump();
    assert(store->readLocks > locks);
    assert(store->geometryCalls == geometry + 1);
    assert(store->writes == writes && store->text == L"ni");
    assert(window.visible() && WindowAccess::text(window) == L"new");
    assert(GetWindowRect(hwnd, &after) && after.left != before.left);
    assert(GetForegroundWindow() == foreground);

    store->clipComposition = true;
    assert(Access::submit(*tsf, snapshot(4, "nih", "caret")));
    assert(window.visible());
    store->clipCaret = true;
    Access::refresh(*tsf);
    assert(!window.visible());
    store->clipComposition = store->clipCaret = false;
    assert(Access::submit(*tsf, snapshot(5, "niha", "visible")));
    assert(window.visible());
    store->emptyRectangle = true;
    Access::refresh(*tsf);
    assert(!window.visible());
    store->emptyRectangle = false;

    assert(Access::submit(*tsf, snapshot(6, "nihao", "timeout")));
    store->geometryResult = TS_E_NOLAYOUT;
    assert(Access::submit(*tsf, snapshot(7, "nihaoa", "timeout")));
    pump(270);
    Access::refresh(*tsf);
    assert(!window.visible());
    store->geometryResult = S_OK;
    assert(SUCCEEDED(store->notifyLayout()));
    pump();
    assert(window.visible());

    auto committed = snapshot(8, "", nullptr);
    committed.commit = "committed";
    assert(Access::submit(*tsf, committed));
    assert(!window.visible() && store->text == L"committed");
    assert(SUCCEEDED(store->notifyLayout()));
    pump();
    assert(!window.visible() && store->text == L"committed");

    assert(Access::submit(*tsf, snapshot(9, "n", "cancel")));
    assert(window.visible());
    assert(Access::submit(*tsf, snapshot(10, "", nullptr)));
    assert(!window.visible() && store->text == L"committed");

    assert(Access::submit(*tsf, snapshot(11, "n", "owner")));
    ShowWindow(store->window(), SW_HIDE);
    store->geometryResult = TS_E_NOLAYOUT;
    Access::refresh(*tsf);
    assert(!window.visible());
    ShowWindow(store->window(), SW_SHOWNOACTIVATE);
    store->geometryResult = S_OK;
    assert(Access::submit(*tsf, snapshot(12, "ni", "focus")));
    assert(window.visible());
    store->denyReadSynchronous = true;
    Access::refresh(*tsf);
    assert(Access::positionPending(*tsf));
    Access::detach(*tsf);
    pump();
    assert(!window.visible());
    assert(SUCCEEDED(layout->OnLayoutChange(context, TF_LC_CHANGE, nullptr)));
    pump();
    assert(!window.visible());
    store->denyReadSynchronous = false;
    store->geometryResult = TS_E_NOLAYOUT;
    Access::attach(*tsf, document);
    assert(Access::submit(*tsf, snapshot(1, "n", "new focus")));
    assert(!window.visible());
    store->geometryResult = S_OK;
    assert(SUCCEEDED(store->notifyLayout()));
    pump();
    assert(window.visible());

    // Move through UTF-8 preedit without rewriting the document. The host gets
    // a collapsed, active selection at the corresponding UTF-16 position.
    const auto start = store->selection().acpStart - 1;
    auto cursorReply = snapshot(2, "\xe4\xbd\xa0\xf0\x9f\x98\x80hao", "cursor");
    const auto checkCaret = [&](LONG offset) {
        const auto &selection = store->selection();
        assert(selection.acpStart == start + offset);
        assert(selection.acpEnd == selection.acpStart);
        assert(selection.style.ase == TS_AE_END);
        assert(!selection.style.fInterimChar);
    };
    assert(Access::submit(*tsf, cursorReply));
    checkCaret(6);
    const auto cursorWrites = store->writes;
    for (const auto [bytes, units] :
         {std::pair{7u, 3L}, {3u, 1L}, {0u, 0L}, {10u, 6L}}) {
        ++cursorReply.revision;
        cursorReply.preeditCursor = bytes;
        assert(Access::submit(*tsf, cursorReply));
        checkCaret(units);
        assert(store->writes == cursorWrites);
    }
    ++cursorReply.revision;
    assert(Access::submit(*tsf, cursorReply));
    assert(store->writes == cursorWrites);
    // Backspace at an interior position still updates the text and caret.
    cursorReply =
        snapshot(cursorReply.revision + 1, "\xe4\xbd\xa0hao", "delete");
    cursorReply.preeditCursor = 3;
    assert(Access::submit(*tsf, cursorReply));
    checkCaret(1);
    assert(store->writes == cursorWrites + 1);
    store->denySynchronous = true;
    ++cursorReply.revision;
    cursorReply.preeditCursor = 6;
    assert(Access::submit(*tsf, cursorReply));
    pump();
    checkCaret(4);
    assert(store->writes == cursorWrites + 1);
    store->denySynchronous = false;
    cursorReply = snapshot(cursorReply.revision + 1, "", nullptr);
    cursorReply.commit = "\xe4\xbd\xa0";
    assert(Access::submit(*tsf, cursorReply));
    checkCaret(1);
    assert(!window.visible());

    assert(Access::inputAllowed(*tsf));
    store->denyReadSynchronous = true;
    assert(!Access::inputAllowed(*tsf));
    store->denyReadSynchronous = false;
    assert(Access::inputAllowed(*tsf));
    store->readOnly = true;
    assert(!Access::inputAllowed(*tsf));
    store->readOnly = false;
    store->setScope(IS_PASSWORD);
    assert(!Access::inputAllowed(*tsf));
    store->setScope(IS_NUMERIC_PASSWORD);
    assert(!Access::inputAllowed(*tsf));
    store->setScope(IS_DEFAULT);
    assert(Access::inputAllowed(*tsf));
    store->scopeResult = E_ACCESSDENIED;
    assert(!Access::inputAllowed(*tsf));
    store->scopeResult = S_OK;
    assert(Access::inputAllowed(*tsf));
    BOOL eaten = TRUE;
    const auto readsBeforeTest = store->readLocks;
    assert(tsf->OnTestKeyDown(context, 'N', 0, &eaten) == S_OK && !eaten);
    assert(store->readLocks == readsBeforeTest);

    UIElementManager ui;
    Access::uiManager(*tsf, &ui);
    const auto hostedDocument = store->text + L"hosted";
    auto hosted = snapshot(cursorReply.revision + 1, "hosted", "candidate");
    store->nullWindow = true;
    store->geometryResult = TS_E_NOLAYOUT;
    assert(Access::submit(*tsf, hosted));
    assert(ui.begins == 1 && !window.visible() &&
           store->text == hostedDocument);
    CComPtr<ITfCandidateListUIElement> hostedList;
    assert(SUCCEEDED(ui.element.QueryInterface(&hostedList)));
    UINT count = 0;
    assert(hostedList->GetCount(&count) == S_OK && count == 1);
    CComBSTR hostedText;
    assert(hostedList->GetString(0, &hostedText) == S_OK &&
           hostedText == L"candidate");
    assert(ui.element->Show(TRUE) == S_OK);
    pump();
    BOOL shown = TRUE;
    assert(ui.element->IsShown(&shown) == S_OK && !shown);
    store->nullWindow = false;
    store->geometryResult = S_OK;
    store->windowOverride = GetDesktopWindow();
    Access::refresh(*tsf);
    assert(!window.visible() && hostedList->GetCount(&count) == S_OK &&
           count == 1);
    store->windowOverride = nullptr;
    Access::refresh(*tsf);
    assert(window.visible());
    assert(ui.element->IsShown(&shown) == S_OK && shown);
    assert(ui.element->Show(FALSE) == S_OK && !window.visible());
    ++hosted.revision;
    assert(Access::submit(*tsf, hosted));
    assert(!ui.updates && !window.visible());
    hosted.candidates[0].text = "changed";
    ++hosted.revision;
    assert(Access::submit(*tsf, hosted));
    assert(ui.updates == 1 && (ui.lastFlags & TF_CLUIE_STRING));
    ui.updateResult = E_FAIL;
    assert(ui.element->Show(TRUE) == S_OK);
    hosted.candidates[0].text = "failure";
    ++hosted.revision;
    assert(Access::submit(*tsf, hosted));
    assert(!window.visible() && store->text == hostedDocument);
    ui.endCallback = [&] { Access::detach(*tsf); };
    Access::detach(*tsf);
    assert(ui.ends == 1 && ui.element->Show(TRUE) == E_UNEXPECTED);
    assert(hostedList->GetCount(&count) == S_OK && count == 0);
    ui.endCallback = {};
    ui.beginResult = E_FAIL;
    Access::attach(*tsf, document);
    ++hosted.revision;
    assert(Access::submit(*tsf, hosted));
    assert(ui.begins == 2 && ui.ends == 1 && !window.visible());
    ui.beginResult = S_OK;
    ui.beginCallback = [&] { Access::detach(*tsf); };
    ++hosted.revision;
    assert(Access::submit(*tsf, hosted));
    assert(ui.begins == 3 && ui.ends == 2 && !window.visible());
    hostedList.Release();
    ui.element.Release();
    ui.beginCallback = {};
    ui.endCallback = {};
    ui.beginResult = S_OK;
    ui.ownUi = TRUE;
    Access::attach(*tsf, document);
    ui.beginCallback = [&] { ui.element->Show(FALSE); };
    ++hosted.revision;
    assert(Access::submit(*tsf, hosted));
    assert(!window.visible());
    Access::detach(*tsf);
    ui.beginCallback = {};
    ui.candidateOnly = false;
    ui.ownUi = FALSE;
    fcitx::AuxiliaryUI permission;
    unsigned closed = 0;
    assert(!permission.begin(&ui, L"About", [&] { ++closed; }));
    assert(!permission.active() && ui.element->Show(FALSE) == E_UNEXPECTED);
    ui.ownUi = TRUE;
    assert(permission.begin(&ui, L"About", [&] {
        ++closed;
        permission.reset();
    }));
    permission.setVisible(true);
    assert(ui.element->IsShown(&shown) == S_OK && shown);
    assert(ui.element->Show(FALSE) == S_OK && closed == 1 &&
           !permission.active());
    assert(ui.element->Show(TRUE) == E_UNEXPECTED);
    ui.element.Release();

    Access::detach(*tsf);
    layout.Release();
    Access::staleEdit(*tsf);
    tsf->Deactivate();
    tsf.Release();
    document->Pop(TF_POPF_ALL);
    assert(SUCCEEDED(manager->Deactivate()));
    pump();
    assert(dllReferences == 0);
}
} // namespace

int main(int argc, char **argv) {
#ifdef FCITX5_UI_TEST
    const bool logging = argc == 2 && std::string_view(argv[1]) == "--logging";
    if (logging) {
        assert(GetEnvironmentVariableW(L"FCITX5_DIAGNOSTIC_TEST_DIRECTORY",
                                       nullptr, 0));
        fcitx::win32::diagnostic::tsfLogger().setEnabled(true);
    }
#else
    if (argc != 1)
        return 2;
    (void)argv;
#endif
    assert(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    tests();
    CoUninitialize();
#ifdef FCITX5_UI_TEST
    if (logging) {
        auto &logger = fcitx::win32::diagnostic::tsfLogger();
        assert(logger.flush(std::chrono::seconds(5)));
        logger.setEnabled(false);
        std::string text;
        for (const auto &file :
             std::filesystem::directory_iterator(logger.directory())) {
            if (!file.path().filename().string().starts_with("tsf-"))
                continue;
            std::ifstream input(file.path());
            text.append(std::istreambuf_iterator<char>(input), {});
        }
        for (const auto event : {"candidate_layout_pending", "candidate_hidden",
                                 "ui_element_begin", "edit_execute",
                                 "popup_position", "edit_stale"})
            assert(text.find(event) != std::string::npos);
        for (const auto secret : {"\"committed\"", "\"unapplied\"", "\"nih\""})
            assert(text.find(secret) == std::string::npos);
    }
#endif
    std::puts("TSF candidate layout recovery and lifecycle tests passed.");
}
