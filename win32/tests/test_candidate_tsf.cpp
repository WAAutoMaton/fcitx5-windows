#include "../tsf/tsf.h"
#include "textstore.h"
#include <cassert>
#include <cstdio>

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
};
} // namespace fcitx

namespace {
using Access = fcitx::TsfCandidateTestAccess;
using WindowAccess = fcitx::CandidateWindowTestAccess;
using Reply = fcitx::PipeClient::KeyReply;

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
    assert(SUCCEEDED(
        document->CreateContext(client, 0, store, &context, &cookie)));
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
    store->denySynchronous = true;
    Access::refresh(*tsf);
    assert(Access::positionPending(*tsf));
    Access::detach(*tsf);
    pump();
    assert(!window.visible());
    assert(SUCCEEDED(layout->OnLayoutChange(context, TF_LC_CHANGE, nullptr)));
    pump();
    assert(!window.visible());
    store->denySynchronous = false;
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

    Access::detach(*tsf);
    layout.Release();
    tsf->Deactivate();
    tsf.Release();
    document->Pop(TF_POPF_ALL);
    assert(SUCCEEDED(manager->Deactivate()));
    pump();
    assert(dllReferences == 0);
}
} // namespace

int main() {
    assert(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    tests();
    CoUninitialize();
    std::puts("TSF candidate layout recovery and lifecycle tests passed.");
}
