#include "../ipc/transport.h"
#include "../tsf/tsf.h"
#include "textstore.h"
#include "threadmgradapter.h"
#include <iostream>
#include <stdexcept>
#include <userenv.h>

LONG dllReferences = 0;
void DllAddRef() { ++dllReferences; }
void DllRelease() { --dllReferences; }
namespace ipc = fcitx::win32::ipc;

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
class UIElementSink : public ITfUIElementSink {
  public:
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result)
            return E_INVALIDARG;
        *result = nullptr;
        if (id != IID_IUnknown && id != IID_ITfUIElementSink)
            return E_NOINTERFACE;
        *result = static_cast<ITfUIElementSink *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references; }
    STDMETHODIMP_(ULONG) Release() override { return --references; }
    STDMETHODIMP BeginUIElement(DWORD id, BOOL *show) override {
        *show = FALSE;
        lastId = id;
        ++begins;
        return S_OK;
    }
    STDMETHODIMP UpdateUIElement(DWORD) override {
        ++updates;
        return S_OK;
    }
    STDMETHODIMP EndUIElement(DWORD) override {
        ++ends;
        return S_OK;
    }
    ULONG references = 1;
    unsigned begins = 0, updates = 0, ends = 0;
    DWORD lastId = 0;
};

void key(ITfKeyEventSink *sink, ITfContext *context, UINT value) {
    BOOL eaten = FALSE;
    require(sink->OnTestKeyDown(context, value, 0, &eaten) == S_OK && eaten,
            "TSF key routing failed");
    require(sink->OnKeyDown(context, value, 0, &eaten) == S_OK && eaten,
            "TSF key processing failed");
    sink->OnKeyUp(context, value, 0, &eaten);
}

void tsfInput() {
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)),
            "COM initialization failed");
    CComPtr<ITfThreadMgrEx> manager;
    require(SUCCEEDED(manager.CoCreateInstance(CLSID_TF_ThreadMgr)),
            "TSF manager unavailable in AppContainer");
    TfClientId client = TF_CLIENTID_NULL;
    require(SUCCEEDED(manager->ActivateEx(
                &client, TF_TMAE_NOACTIVATETIP | TF_TMAE_UIELEMENTENABLEDONLY)),
            "TSF manager activation failed");
    CComPtr<ThreadMgrAdapter> adapter;
    adapter.Attach(new ThreadMgrAdapter(manager));
    UIElementSink ui;
    CComPtr<ITfSource> source;
    require(SUCCEEDED(manager.QueryInterface(&source)), "TSF source missing");
    DWORD uiCookie = TF_INVALID_COOKIE;
    require(SUCCEEDED(source->AdviseSink(IID_ITfUIElementSink, &ui, &uiCookie)),
            "UIElement sink unavailable");
    CComPtr<fcitx::test::TextStore> store;
    store.Attach(new fcitx::test::TextStore);
    store->nullWindow = true;
    CComPtr<ITfDocumentMgr> document;
    CComPtr<ITfContext> context;
    TfEditCookie cookie = 0;
    require(SUCCEEDED(manager->CreateDocumentMgr(&document)) &&
                SUCCEEDED(document->CreateContext(
                    client, 0, static_cast<ITextStoreACP *>(store.p), &context,
                    &cookie)) &&
                SUCCEEDED(document->Push(context)) &&
                SUCCEEDED(manager->SetFocus(document)),
            "TSF context creation failed");
    CComPtr<fcitx::Tsf> service;
    service.Attach(new fcitx::Tsf);
    require(SUCCEEDED(service->ActivateEx(adapter, client,
                                          TF_TMAE_UIELEMENTENABLEDONLY)),
            "TIP activation failed");
    // The actual TSF implementation talks to Core; candidate text is never used
    // as a substitute for the engine's commit response.
    for (UINT value : {'N', 'I', 'H', 'A', 'O'})
        key(service, context, value);
    require(!store->text.empty() && ui.begins > 0,
            "composition/candidate UIElement missing");
    key(service, context, VK_SPACE);
    require(store->text == L"\u4f60\u597d" && ui.ends > 0,
            "real Pinyin TSF commit failed");
    for (UINT value : {'N', 'I'})
        key(service, context, value);
    CComPtr<ITfUIElementMgr> elements;
    CComPtr<ITfUIElement> element;
    CComPtr<ITfCandidateListUIElementBehavior> behavior;
    require(SUCCEEDED(manager.QueryInterface(&elements)) &&
                SUCCEEDED(elements->GetUIElement(ui.lastId, &element)) &&
                SUCCEEDED(element.QueryInterface(&behavior)),
            "host candidate behavior missing");
    CComBSTR chosen;
    require(behavior->GetString(0, &chosen) == S_OK, "candidate getter failed");
    require(behavior->SetSelection(0) == S_OK && behavior->Finalize() == S_OK,
            "candidate action queue failed");
    const auto expected =
        std::wstring(L"\u4f60\u597d") + std::wstring(chosen, chosen.Length());
    const auto deadline = GetTickCount64() + 3000;
    while (GetTickCount64() < deadline && store->text != expected) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }
    require(store->text == expected,
            "queued host candidate selection did not commit through Core");
    for (UINT value : {'N', 'I'})
        key(service, context, value);
    element.Release();
    behavior.Release();
    require(SUCCEEDED(elements->GetUIElement(ui.lastId, &element)) &&
                SUCCEEDED(element.QueryInterface(&behavior)) &&
                behavior->Abort() == S_OK,
            "candidate abort queue failed");
    for (int i = 0; i < 100 && store->text != expected; ++i) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }
    require(store->text == expected, "host candidate abort left preedit");
    store->setScope(IS_PASSWORD);
    BOOL eaten = TRUE;
    require(service->OnKeyDown(context, 'N', 0, &eaten) == S_OK && !eaten,
            "password key was consumed");
    require(store->text == expected, "password context was edited");
    require(SUCCEEDED(service->Deactivate()), "TIP deactivation failed");
    service.Release();
    behavior.Release();
    element.Release();
    elements.Release();
    source->UnadviseSink(uiCookie);
    document->Pop(TF_POPF_ALL);
    manager->Deactivate();
    context.Release();
    document.Release();
    store.Release();
    adapter.Release();
    source.Release();
    manager.Release();
    CoUninitialize();
}

int runChild() {
    const auto environment = ipc::clientEnvironment();
    require(environment.tokenKnown && environment.appContainer &&
                environment.lowIntegrity,
            "fixture is not a real AppContainer");
    fcitx::PipeClient client;
    if (!client.connect()) {
        std::cerr << "connect stage=" << client.connectionStage()
                  << " error=" << client.connectionError() << "\n";
        return 10;
    }
    uint64_t context = 0;
    require(client.createContext(2, context) && client.focusIn(context),
            "restricted context unavailable");
    std::vector<uint64_t> extraContexts;
    for (int i = 0; i < 3; ++i) {
        uint64_t extra = 0;
        require(client.createContext(2, extra),
                "allowed context allocation failed");
        extraContexts.push_back(extra);
    }
    uint64_t excess = 0;
    require(!client.createContext(2, excess) && !excess,
            "restricted context quota was not enforced");
    for (const auto extra : extraContexts)
        require(client.destroyContext(extra), "extra context cleanup failed");
    ipc::KeyReply reply;
    require(client.selectInputMethod(context, "pinyin", reply),
            "Pinyin unavailable");
    for (UINT value : {'N', 'I', 'H', 'A', 'O'})
        require(client.key(context, false, value, 0, 0, 0, reply) &&
                    reply.consumed,
                "Pinyin key failed");
    require(!reply.candidates.empty() && reply.inputMethodIcon.empty(),
            "candidate batch or path sanitization failed");
    const auto identity = reply.candidateGeneration;
    require(identity && client.poll(context, reply) &&
                reply.candidateGeneration == identity,
            "ordinary poll invalidated candidate identity");
    fcitx::PipeClient otherClient;
    require(otherClient.connect() && !otherClient.poll(context, reply) &&
                !otherClient.candidateAction(context, identity,
                                             ipc::CandidateAction::Finalize, 0,
                                             reply),
            "foreign connection accessed context/candidates");
    otherClient.disconnect();
    require(!client.candidateAction(context, identity + 1,
                                    ipc::CandidateAction::Finalize, 0, reply),
            "stale candidate was selected");
    require(!client.candidateAction(context, identity,
                                    ipc::CandidateAction::Finalize, UINT32_MAX,
                                    reply),
            "invalid candidate index was selected");
    require(client.candidateAction(context, identity,
                                   ipc::CandidateAction::Select, 0, reply),
            "candidate cursor action failed");
    require(client.candidateAction(context, reply.candidateGeneration,
                                   ipc::CandidateAction::Finalize, 0, reply) &&
                reply.commit == "\xe4\xbd\xa0\xe5\xa5\xbd",
            "real Pinyin candidate commit failed");
    require(client.destroyContext(context), "context cleanup failed");
    ipc::GlobalSettingsReply language;
    require(client.getGlobalSettings(language), "host UI language unavailable");
    ipc::DictionariesReply dictionaries;
    require(!client.getDictionaries(dictionaries),
            "restricted caller obtained dictionaries");
    client.disconnect();
    require(client.connect(), "reconnect after denied operation failed");
    ipc::GlobalSettings settings;
    require(!client.setGlobalSettings(settings, language),
            "restricted caller modified settings");
    client.disconnect();
    require(client.connect(), "second reconnect failed");
    DWORD error = 0;
    require(!client.hostCommand(ipc::MessageType::StopServices, error),
            "restricted caller controlled service");
    tsfInput();
    return 0;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    try {
        if (argc == 2 && std::wstring(argv[1]) == L"--check-ui-test") {
#ifdef FCITX5_UI_TEST
            return 0;
#else
            return 1;
#endif
        }
        if (argc == 2 && std::wstring(argv[1]) == L"--child")
            return runChild();
        if (argc != 2)
            return 2;
        PSID sid = nullptr;
        require(
            SUCCEEDED(DeriveAppContainerSidFromAppContainerName(argv[1], &sid)),
            "existing profile SID unavailable");
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 2, 0, &size);
        std::vector<BYTE> storage(size);
        auto attributes =
            reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        require(InitializeProcThreadAttributeList(attributes, 2, 0, &size),
                "attribute list failed");
        SECURITY_CAPABILITIES capabilities{};
        capabilities.AppContainerSid = sid;
        require(UpdateProcThreadAttribute(
                    attributes, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                    &capabilities, sizeof(capabilities), nullptr, nullptr),
                "AppContainer token setup failed");
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.lpAttributeList = attributes;
        SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
        HANDLE outputRead = nullptr, outputWrite = nullptr;
        require(CreatePipe(&outputRead, &outputWrite, &inherit, 8192),
                "diagnostic pipe failed");
        SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);
        HANDLE input = CreateFileW(L"NUL", GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                                   OPEN_EXISTING, 0, nullptr);
        require(input != INVALID_HANDLE_VALUE, "diagnostic stdin failed");
        HANDLE inherited[] = {outputWrite, input};
        require(UpdateProcThreadAttribute(
                    attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                    sizeof(inherited), nullptr, nullptr),
                "diagnostic handle list failed");
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdOutput = outputWrite;
        startup.StartupInfo.hStdError = outputWrite;
        startup.StartupInfo.hStdInput = input;
        WCHAR executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        auto command = L"\"" + std::wstring(executable) + L"\" --child";
        PROCESS_INFORMATION process{};
        const auto launched =
            CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE,
                           EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
                           nullptr, nullptr, &startup.StartupInfo, &process);
        DeleteProcThreadAttributeList(attributes);
        FreeSid(sid);
        CloseHandle(outputWrite);
        CloseHandle(input);
        require(launched, "AppContainer process launch failed");
        DWORD exitCode = 99;
        if (WaitForSingleObject(process.hProcess, 20000) != WAIT_OBJECT_0)
            TerminateProcess(process.hProcess, exitCode);
        WaitForSingleObject(process.hProcess, 1000);
        GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        char diagnostic[8192]{};
        DWORD read = 0;
        while (ReadFile(outputRead, diagnostic, sizeof(diagnostic), &read,
                        nullptr) &&
               read)
            std::cout.write(diagnostic, read);
        CloseHandle(outputRead);
        std::cout << "AppContainer input fixture exit=" << exitCode << "\n";
        return static_cast<int>(exitCode);
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
