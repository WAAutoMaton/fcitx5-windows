#include "../dll/util.h"
#include "../tsf/pipeclient.h"
#include "textstore.h"
#include "threadmgradapter.h"
#include <algorithm>
#include <atlcomcli.h>
#include <iostream>
#include <msctf.h>
#include <stdexcept>
#include <string>
#include <textstor.h>

namespace {

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

using fcitx::test::TextStore;

void pump(DWORD duration = 50) {
    const auto deadline = GetTickCount64() + duration;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    } while (GetTickCount64() < deadline);
}

void press(ITfKeyEventSink *keys, ITfContext *context, uint32_t key,
           bool runPump = true) {
    static unsigned sequence = 0;
    ++sequence;
    BOOL handled = FALSE;
    require(SUCCEEDED(keys->OnTestKeyDown(context, key, 0, &handled)) &&
                handled,
            ("key routing failed: " + std::to_string(key)).c_str());
    require(SUCCEEDED(keys->OnKeyDown(context, key, 0, &handled)) && handled,
            ("key processing failed: " + std::to_string(key) +
             " sequence=" + std::to_string(sequence))
                .c_str());
    if (runPump)
        pump();
}

void requirePassedThrough(ITfKeyEventSink *keys, ITfContext *context,
                          uint32_t key) {
    BOOL handled = TRUE;
    require(SUCCEEDED(keys->OnTestKeyDown(context, key, 0, &handled)) &&
                !handled,
            "direct-input key was routed to the engine");
    require(SUCCEEDED(keys->OnKeyDown(context, key, 0, &handled)) && !handled,
            "direct-input key was consumed");
}

class KeyboardState {
  public:
    KeyboardState() {
        require(GetKeyboardState(saved_), "keyboard state query failed");
    }
    ~KeyboardState() { SetKeyboardState(saved_); }
    void control(int side) {
        BYTE state[256]{};
        state[VK_CONTROL] = 0x80;
        state[side] = 0x80;
        require(SetKeyboardState(state), "test modifier setup failed");
    }
    void shortcut(UINT key, unsigned modifiers) {
        BYTE state[256]{};
        state[key] = 0x80;
        if (modifiers & 1)
            state[VK_SHIFT] = state[VK_LSHIFT] = 0x80;
        if (modifiers & 2)
            state[VK_CONTROL] = state[VK_LCONTROL] = 0x80;
        if (modifiers & 4)
            state[VK_MENU] = state[VK_LMENU] = 0x80;
        require(SetKeyboardState(state), "custom shortcut state failed");
    }
    void controlSpace() {
        BYTE state[256]{};
        state[VK_CONTROL] = state[VK_LCONTROL] = state[VK_SPACE] = 0x80;
        require(SetKeyboardState(state), "test shortcut setup failed");
    }
    void clear() {
        BYTE state[256]{};
        require(SetKeyboardState(state), "test keyboard reset failed");
    }

  private:
    BYTE saved_[256]{};
};

} // namespace

namespace {
void cycleProbe(HMODULE module) {
    fcitx::PipeClient client;
    fcitx::ipc::InputMethodsReply catalog;
    require(client.connect() && client.getInputMethods(catalog),
            "cycle catalog unavailable");
    std::vector<fcitx::ipc::InputMethodInfo> methods;
    for (const auto &id : catalog.selection.enabled) {
        const auto found =
            std::find_if(catalog.methods.begin(), catalog.methods.end(),
                         [&](const auto &method) { return method.id == id; });
        if (found != catalog.methods.end() && found->available) {
            methods.push_back(*found);
        }
    }
    require(!methods.empty(), "no available cycle methods");
    using GetFactory = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
    const auto getFactory = reinterpret_cast<GetFactory>(
        GetProcAddress(module, "DllGetClassObject"));
    require(getFactory != nullptr, "cycle factory export missing");
    CComPtr<IClassFactory> factory;
    CComPtr<ITfTextInputProcessorEx> service;
    CComPtr<ITfKeyEventSink> keys;
    require(SUCCEEDED(getFactory(fcitx::FCITX_CLSID, IID_PPV_ARGS(&factory))) &&
                SUCCEEDED(
                    factory->CreateInstance(nullptr, IID_PPV_ARGS(&service))) &&
                SUCCEEDED(service.QueryInterface(&keys)),
            "cycle TIP creation failed");
    CComPtr<ITfThreadMgrEx> manager;
    require(SUCCEEDED(manager.CoCreateInstance(CLSID_TF_ThreadMgr)),
            "cycle thread manager missing");
    TfClientId clientId = TF_CLIENTID_NULL;
    require(SUCCEEDED(manager->ActivateEx(&clientId, TF_TMAE_NOACTIVATETIP)),
            "cycle thread activation failed");
    CComPtr<ThreadMgrAdapter> adapter;
    adapter.Attach(new ThreadMgrAdapter(manager));
    CComPtr<TextStore> store;
    store.Attach(new TextStore);
    CComPtr<ITfDocumentMgr> document;
    CComPtr<ITfContext> context;
    TfEditCookie cookie = 0;
    require(SUCCEEDED(manager->CreateDocumentMgr(&document)) &&
                SUCCEEDED(document->CreateContext(
                    clientId, 0, static_cast<ITextStoreACP *>(store.p),
                    &context, &cookie)) &&
                SUCCEEDED(document->Push(context)) &&
                SUCCEEDED(manager->SetFocus(document)),
            "cycle context setup failed");
    CComPtr<ITfCompartmentMgr> compartments;
    CComPtr<ITfCompartment> keyboard;
    require(SUCCEEDED(manager.QueryInterface(&compartments)) &&
                SUCCEEDED(compartments->GetCompartment(
                    GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &keyboard)),
            "cycle keyboard compartment missing");
    CComVariant open(static_cast<LONG>(1));
    require(SUCCEEDED(keyboard->SetValue(clientId, &open)) &&
                SUCCEEDED(service->ActivateEx(adapter, clientId, 0)),
            "cycle TIP activation failed");
    CComPtr<ITfLangBarItemButton> button;
    require(SUCCEEDED(adapter->langBarItem.QueryInterface(&button)),
            "cycle indicator missing");
    pump(200);
    auto currentIndex = [&]() {
        CComBSTR name;
        require(SUCCEEDED(button->GetTooltipString(&name)) && name,
                "cycle indicator name missing");
        const auto found = std::find_if(
            methods.begin(), methods.end(), [&](const auto &method) {
                return fcitx::stringToWString(method.name, CP_UTF8) ==
                       std::wstring(name);
            });
        require(found != methods.end(), "cycle indicator method not enabled");
        return static_cast<size_t>(found - methods.begin());
    };
    auto index = currentIndex();
    int currentRoute = 0;
    const auto check = [&](size_t expected) {
        require(currentIndex() == expected,
                ("TSF shortcut skipped " + methods[expected].id + " (actual " +
                 methods[currentIndex()].id + ") on route " +
                 std::to_string(currentRoute))
                    .c_str());
        CComVariant value;
        require(SUCCEEDED(keyboard->GetValue(&value)) && value.vt == VT_I4,
                "cycle compartment value missing");
        require((value.lVal != 0) == !methods[expected].keyboard,
                ("cycle compartment disagrees with " + methods[expected].id +
                 ": " + std::to_string(value.lVal))
                    .c_str());
        require(store->text.empty(), "cycle unexpectedly edited the document");
    };
    KeyboardState keyboardState;
    keyboardState.controlSpace();
    const bool simulatedSpace = (GetKeyState(VK_SPACE) & 0x8000) != 0;
    keyboardState.clear();
    if (!simulatedSpace)
        std::cout << "SKIP: compartment/key coalescing routes require a "
                     "simulated held Space key, unavailable in this session\n";
    for (const int route : {0, 2, 1, 3, 4, 5}) {
        if (!simulatedSpace && (route == 2 || route == 3 || route == 4))
            continue;
        currentRoute = route;
        for (size_t step = 0; step < methods.size() * 2; ++step) {
            BOOL handled = FALSE;
            const auto preserved = [&]() {
                require(SUCCEEDED(keys->OnPreservedKey(
                            context, adapter->preservedGuid, &handled)) &&
                            handled,
                        "cycle preserved callback failed");
            };
            const auto systemHotkey = [&]() {
                CComVariant value;
                require(SUCCEEDED(keyboard->GetValue(&value)),
                        "cycle mode query failed");
                keyboardState.controlSpace();
                CComVariant toggled(static_cast<LONG>(value.lVal == 0));
                require(SUCCEEDED(keyboard->SetValue(clientId, &toggled)),
                        "system shortcut notification failed");
            };
            keyboardState.controlSpace();
            if (route == 0 || route == 3) {
                if (route == 3) {
                    systemHotkey();
                }
                require(SUCCEEDED(keys->OnTestKeyDown(context, VK_SPACE, 0,
                                                      &handled)) &&
                            handled &&
                            SUCCEEDED(keys->OnKeyDown(context, VK_SPACE, 0,
                                                      &handled)) &&
                            handled &&
                            SUCCEEDED(keys->OnKeyDown(
                                context, VK_SPACE, static_cast<LPARAM>(1) << 30,
                                &handled)) &&
                            handled,
                        "cycle ordinary/repeat callback failed");
            }
            if (route == 1 || route == 3 || route == 4) {
                preserved();
                if (route == 4) {
                    systemHotkey();
                }
                preserved();
            }
            if (route == 2) {
                systemHotkey();
                pump(200);
                systemHotkey();
                pump(200);
            }
            if (route == 5) {
                keyboardState.clear();
                require(
                    SUCCEEDED(button->OnClick(TF_LBI_CLK_LEFT, {}, nullptr)),
                    "cycle indicator click failed");
            }
            pump(200);
            index = (index + 1) % methods.size();
            check(index);
            if (route != 2 && route != 5) {
                require(
                    SUCCEEDED(keys->OnKeyUp(context, VK_SPACE, 0, &handled)) &&
                        handled,
                    "cycle key release failed");
            }
            keyboardState.clear();
            pump(200);
            check(index);
        }
    }
    // Explicit system mode changes without Ctrl+Space remain binary controls.
    const auto english =
        std::find_if(methods.begin(), methods.end(),
                     [](const auto &method) { return method.keyboard; });
    const auto chinese =
        std::find_if(methods.begin(), methods.end(),
                     [](const auto &method) { return !method.keyboard; });
    if (english != methods.end() && chinese != methods.end()) {
        CComVariant closed(static_cast<LONG>(0));
        require(SUCCEEDED(keyboard->SetValue(clientId, &closed)),
                "explicit keyboard close failed");
        pump(200);
        check(static_cast<size_t>(english - methods.begin()));
        require(SUCCEEDED(keyboard->SetValue(clientId, &open)),
                "explicit keyboard open failed");
        pump(200);
        check(static_cast<size_t>(chinese - methods.begin()));
    }
    fcitx::ipc::GlobalSettingsReply global;
    require(client.getGlobalSettings(global), "TSF shortcuts read failed");
    const auto originalShortcuts = global.settings.shortcuts;
    const auto oldPreserved = adapter->preservedGuid;
    auto custom = global.settings;
    custom.shortcuts[0] = "Control+Shift+J";
    custom.shortcuts[1] = "Alt+<59>";
    custom.shortcuts[5].clear();
    require(client.setGlobalSettings(custom, global) &&
                global.error == fcitx::ipc::SettingsError::None,
            "TSF shortcuts save failed");
    pump(1300);
    BOOL handled = TRUE;
    keyboardState.controlSpace();
    require(SUCCEEDED(keys->OnTestKeyDown(context, VK_SPACE, 0, &handled)) &&
                !handled &&
                SUCCEEDED(keys->OnKeyDown(context, VK_SPACE, 0, &handled)) &&
                !handled,
            "TSF kept old Ctrl+Space routing");
    require(SUCCEEDED(keys->OnPreservedKey(context, oldPreserved, &handled)) &&
                !handled,
            "TSF kept a stale preserved key");
    keyboardState.clear();
    const auto quick = [&](UINT key, UINT scan, unsigned modifiers,
                           size_t expected) {
        const LPARAM details = static_cast<LPARAM>(scan & 0xff) << 16 |
                               static_cast<LPARAM>((scan & 0x100) != 0) << 24;
        keyboardState.shortcut(key, modifiers);
        const auto before = currentIndex();
        require(
            SUCCEEDED(keys->OnTestKeyDown(context, key, details, &handled)) &&
                handled,
            "configured shortcut not routed by TSF");
        check(before); // Test callbacks must not change the input method.
        require(SUCCEEDED(keys->OnKeyDown(context, key, details, &handled)) &&
                    handled,
                "configured shortcut not handled by TSF");
        require(SUCCEEDED(keys->OnKeyDown(
                    context, key, details | (static_cast<LPARAM>(1) << 30),
                    &handled)) &&
                    handled,
                "configured shortcut repeat not handled");
        pump();
        check(expected);
        keyboardState.clear();
        require(SUCCEEDED(keys->OnKeyUp(context, key, details, &handled)) &&
                    handled,
                "configured shortcut release not handled");
        pump();
        check(expected);
    };
    quick('J', 0x24, 3, (currentIndex() + 1) % methods.size());
    quick(VK_F1, 0x3b, 4, 0);
    for (size_t i = 2; i <= std::min<size_t>(5, methods.size()); ++i) {
        if (i != 5)
            quick('0' + static_cast<UINT>(i), 0, 6, i - 1);
    }
    keyboardState.shortcut('5', 6);
    require(
        SUCCEEDED(keys->OnTestKeyDown(context, '5', 0, &handled)) && !handled &&
            SUCCEEDED(keys->OnKeyDown(context, '5', 0, &handled)) && !handled,
        "disabled quick switch was consumed");
    keyboardState.clear();
    const auto preserved = std::find_if(
        adapter->preservedKeys.begin(), adapter->preservedKeys.end(),
        [](const auto &item) { return item.second.uVKey == VK_F1; });
    require(preserved != adapter->preservedKeys.end(),
            "recorded key was not preserved");
    require(
        SUCCEEDED(keys->OnPreservedKey(context, preserved->first, &handled)) &&
            handled,
        "custom preserved quick switch failed");
    pump();
    check(0);
    require(SUCCEEDED(keys->OnKeyUp(context, VK_F1, 0x3b << 16, &handled)) &&
                handled,
            "custom preserved release failed");
    custom = global.settings;
    custom.shortcuts = originalShortcuts;
    require(client.setGlobalSettings(custom, global) &&
                global.error == fcitx::ipc::SettingsError::None,
            "TSF shortcuts restore failed");
    require(SUCCEEDED(service->Deactivate()), "cycle deactivation failed");
    manager->SetFocus(nullptr);
    document->Pop(TF_POPF_ALL);
    pump();
    require(SUCCEEDED(manager->Deactivate()),
            "cycle thread deactivation failed");
    std::cout
        << "PASS: " << methods.size()
        << " methods, ordinary/preserved shortcuts, custom bindings, disabled "
           "actions, cache updates, repeat/release suppression and indicator "
           "cycles (simulated callbacks, no registration; settings restored)\n";
}
} // namespace

int wmain(int count, wchar_t **arguments) {
    if (count != 2 &&
        !(count == 3 && std::wstring(arguments[2]) == L"--cycles"))
        return 2;
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)),
            "COM initialization failed");
    HMODULE module = LoadLibraryW(arguments[1]);
    if (!module)
        return 3;
    int exit = 1;
    try {
        if (count == 3) {
            cycleProbe(module);
            FreeLibrary(module);
            CoUninitialize();
            return 0;
        }
        using GetFactory = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
        using CanUnload = HRESULT(WINAPI *)();
        auto getFactory = reinterpret_cast<GetFactory>(
            GetProcAddress(module, "DllGetClassObject"));
        auto canUnload = reinterpret_cast<CanUnload>(
            GetProcAddress(module, "DllCanUnloadNow"));
        require(getFactory && canUnload && canUnload() == S_OK,
                "DLL exports/unload state failed");
        CComPtr<IClassFactory> factory;
        require(getFactory(GUID_NULL, IID_IClassFactory,
                           reinterpret_cast<void **>(&factory)) ==
                    CLASS_E_CLASSNOTAVAILABLE,
                "DLL accepted the wrong CLSID");
        require(SUCCEEDED(getFactory(fcitx::FCITX_CLSID, IID_IClassFactory,
                                     reinterpret_cast<void **>(&factory))),
                "class factory failed");
        CComPtr<ITfTextInputProcessorEx> service;
        require(
            SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&service))),
            "TIP creation failed");
        CComPtr<ITfKeyEventSink> keys;
        CComPtr<ITfCompositionSink> compositionSink;
        CComPtr<ITfThreadMgrEventSink> events;
        CComPtr<ITfDisplayAttributeProvider> attributes;
        require(SUCCEEDED(service.QueryInterface(&keys)) &&
                    SUCCEEDED(service.QueryInterface(&compositionSink)) &&
                    SUCCEEDED(service.QueryInterface(&attributes)),
                "TIP interfaces missing");
        require(SUCCEEDED(service.QueryInterface(&events)),
                "thread event interface missing");
        require(canUnload() == S_FALSE,
                "live TIP incorrectly permits unloading");
        CComPtr<IEnumTfDisplayAttributeInfo> attributeEnumerator;
        CComPtr<ITfDisplayAttributeInfo> attributeInfo;
        require(SUCCEEDED(
                    attributes->EnumDisplayAttributeInfo(&attributeEnumerator)),
                "display attribute enumeration failed");
        ULONG fetchedAttributes = 0;
        require(SUCCEEDED(attributeEnumerator->Next(1, &attributeInfo,
                                                    &fetchedAttributes)) &&
                    fetchedAttributes == 1,
                "preedit display attribute missing");
        TF_DISPLAYATTRIBUTE display{};
        require(SUCCEEDED(attributeInfo->GetAttributeInfo(&display)) &&
                    display.lsStyle == TF_LS_DOT,
                "preedit underline missing");
        CComPtr<ITfThreadMgrEx> manager;
        require(SUCCEEDED(manager.CoCreateInstance(CLSID_TF_ThreadMgr)),
                "thread manager creation failed");
        TfClientId client = TF_CLIENTID_NULL;
        require(SUCCEEDED(manager->ActivateEx(&client, TF_TMAE_NOACTIVATETIP)),
                "thread manager activation failed");
        CComPtr<ThreadMgrAdapter> adapter;
        adapter.Attach(new ThreadMgrAdapter(manager));
        CComPtr<TextStore> store;
        store.Attach(new TextStore);
        CComPtr<ITfDocumentMgr> document;
        CComPtr<ITfContext> context;
        TfEditCookie cookie = 0;
        require(SUCCEEDED(manager->CreateDocumentMgr(&document)) &&
                    SUCCEEDED(document->CreateContext(
                        client, 0, static_cast<ITextStoreACP *>(store.p),
                        &context, &cookie)) &&
                    SUCCEEDED(document->Push(context)) &&
                    SUCCEEDED(manager->SetFocus(document)),
                "real TSF context creation failed");
        CComPtr<ITfCompartmentMgr> compartments;
        CComPtr<ITfCompartment> keyboard;
        require(SUCCEEDED(manager.QueryInterface(&compartments)) &&
                    SUCCEEDED(compartments->GetCompartment(
                        GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &keyboard)),
                "keyboard compartment missing");
        CComVariant open(static_cast<LONG>(1));
        CComVariant closed(static_cast<LONG>(0));
        require(SUCCEEDED(keyboard->SetValue(client, &open)),
                "keyboard compartment initialization failed");
        const auto activation = service->ActivateEx(adapter, client, 0);
        if (FAILED(activation))
            std::cerr << "ActivateEx HRESULT: 0x" << std::hex << activation
                      << std::dec << '\n';
        require(SUCCEEDED(activation), "TIP activation failed");
        BOOL handled = FALSE;
        require(adapter->addItemCalls == 1 && adapter->langBarItem,
                "input mode item was not added on activation");
        CComPtr<ITfLangBarItemButton> modeButton;
        require(SUCCEEDED(adapter->langBarItem.QueryInterface(&modeButton)),
                "input mode button interface missing");
        TF_LANGBARITEMINFO modeInfo{};
        require(SUCCEEDED(modeButton->GetInfo(&modeInfo)) &&
                    modeInfo.clsidService == fcitx::FCITX_CLSID &&
                    modeInfo.guidItem == GUID_LBI_INPUTMODE,
                "input mode item has the wrong identity");
        auto requireMode = [&](bool chinese) {
            CComBSTR text;
            DWORD status = 0;
            require(SUCCEEDED(modeButton->GetText(&text)) && text &&
                        std::wstring(text) == (chinese ? L"\u62fc" : L"A") &&
                        SUCCEEDED(modeButton->GetStatus(&status)) &&
                        !(status & TF_LBI_STATUS_HIDDEN) &&
                        ((status & TF_LBI_STATUS_BTN_TOGGLED) != 0) == chinese,
                    "input mode text/status is out of sync");
        };
        requireMode(true);
        HICON methodIcon = nullptr;
        ICONINFO methodIconInfo{};
        require(SUCCEEDED(modeButton->GetIcon(&methodIcon)) && methodIcon &&
                    GetIconInfo(methodIcon, &methodIconInfo) &&
                    methodIconInfo.hbmColor,
                "deployed input method PNG icon did not render");
        DeleteObject(methodIconInfo.hbmColor);
        DeleteObject(methodIconInfo.hbmMask);
        DestroyIcon(methodIcon);
        require(SUCCEEDED(modeButton->OnClick(TF_LBI_CLK_LEFT, {}, nullptr)),
                "input mode click failed");
        pump(200);
        requireMode(false);
        requirePassedThrough(keys, context, 'N');
        require(SUCCEEDED(modeButton->OnClick(TF_LBI_CLK_LEFT, {}, nullptr)),
                "input mode reopen click failed");
        pump(200);
        requireMode(true);
        require(SUCCEEDED(keys->OnTestKeyDown(context, 'N', 0, &handled)) &&
                    handled,
                "input mode click did not reopen Pinyin");
        require(adapter->preserveCalls == 6 &&
                    adapter->preservedGuid != GUID_NULL,
                "exact Ctrl+Space preserved key was not requested");
        const uint32_t directKeys[] = {VK_SPACE, VK_OEM_PLUS, VK_OEM_MINUS, '0',
                                       '9'};
        for (const auto key : directKeys)
            requirePassedThrough(keys, context, key);
        require(store->text.empty(), "pass-through key mutated the text store");
        require(SUCCEEDED(keyboard->SetValue(client, &closed)),
                "system keyboard close failed");
        pump(200);
        requireMode(false);
        require(
            (adapter->langBarUpdateFlags & (TF_LBI_BTNALL | TF_LBI_STATUS)) ==
                (TF_LBI_BTNALL | TF_LBI_STATUS),
            "input mode changes did not notify the language bar");
        requirePassedThrough(keys, context, 'N');
        requirePassedThrough(keys, context, VK_SPACE);
        require(SUCCEEDED(keyboard->SetValue(client, &open)),
                "system keyboard open failed");
        pump(200);
        requireMode(true);
        require(SUCCEEDED(keys->OnTestKeyDown(context, 'N', 0, &handled)) &&
                    handled,
                "system keyboard reopen did not restore Pinyin");
        require(SUCCEEDED(keys->OnPreservedKey(context, adapter->preservedGuid,
                                               &handled)) &&
                    handled,
                "preserved mode switch failed");
        pump();
        requirePassedThrough(keys, context, 'N');
        requireMode(false);
        CComVariant mode;
        require(SUCCEEDED(keyboard->GetValue(&mode)) && mode.vt == VT_I4 &&
                    mode.lVal == 0,
                "preserved key did not update the system keyboard mode");
        require(SUCCEEDED(keys->OnTestKeyUp(context, VK_SPACE, 0, &handled)) &&
                    handled &&
                    SUCCEEDED(keys->OnKeyUp(context, VK_SPACE, 0, &handled)) &&
                    handled,
                "mode switch release was not consumed");
        requirePassedThrough(keys, context, 'N');
        require(SUCCEEDED(keys->OnPreservedKey(context, adapter->preservedGuid,
                                               &handled)) &&
                    handled,
                "preserved Chinese mode switch failed");
        require(SUCCEEDED(keys->OnKeyUp(context, VK_SPACE, 0, &handled)) &&
                    handled,
                "preserved Chinese switch release failed");
        pump();
        {
            KeyboardState keyboardState;
            for (const auto side : {VK_LCONTROL, VK_RCONTROL}) {
                keyboardState.control(side);
                require(SUCCEEDED(keys->OnTestKeyDown(context, VK_SPACE, 0,
                                                      &handled)) &&
                            handled &&
                            SUCCEEDED(keys->OnKeyDown(context, VK_SPACE, 0,
                                                      &handled)) &&
                            handled,
                        "ordinary Ctrl+Space route failed");
                require(SUCCEEDED(keys->OnKeyDown(context, VK_SPACE,
                                                  static_cast<LPARAM>(1) << 30,
                                                  &handled)) &&
                            handled,
                        "Ctrl+Space repeat was not consumed");
                require(SUCCEEDED(keyboard->GetValue(&mode)) &&
                            mode.vt == VT_I4 && mode.lVal == 0,
                        "repeated Ctrl+Space toggled mode more than once");
                require(
                    SUCCEEDED(keys->OnKeyUp(context, VK_SPACE, 0, &handled)) &&
                        handled,
                    "ordinary Ctrl+Space release failed");
                require(SUCCEEDED(keyboard->SetValue(client, &open)),
                        "mode restoration failed");
                pump(200);
            }
        }
        requirePassedThrough(keys, context, VK_SPACE);
        const auto writes = store->writes;
        require(SUCCEEDED(keys->OnTestKeyDown(context, 'N', 0, &handled)) &&
                    handled &&
                    SUCCEEDED(keys->OnTestKeyDown(context, 'N', 0, &handled)) &&
                    handled && store->writes == writes,
                "test-key callback mutated the document");
        for (const auto letter : std::string("NIHAO"))
            press(keys, context, letter);
        require(!store->text.empty(),
                "composition did not reach the TSF text store");
        press(keys, context, VK_SPACE);
        const std::wstring hello = L"\u4f60\u597d";
        require(store->text == hello,
                "pinyin Chinese commit did not reach the TSF text store");
        press(keys, context, 'N');
        require(SUCCEEDED(keyboard->SetValue(client, &closed)),
                "system close during composition failed");
        pump(200);
        require(store->text == hello,
                "system keyboard close did not cancel preedit");
        requirePassedThrough(keys, context, 'N');
        require(SUCCEEDED(keyboard->SetValue(client, &open)),
                "system reopen after cancellation failed");
        pump(200);
        store->denySynchronous = true;
        press(keys, context, 'N');
        press(keys, context, 'I');
        press(keys, context, VK_BACK);
        require(store->text.size() == hello.size() + 1,
                "TSF backspace did not shrink preedit");
        press(keys, context, VK_ESCAPE);
        pump(200);
        require(store->text == hello,
                "async cancellation corrupted committed text");
        press(keys, context, 'N');
        CComPtr<TextStore> otherStore;
        otherStore.Attach(new TextStore);
        CComPtr<ITfDocumentMgr> otherDocument;
        CComPtr<ITfContext> otherContext;
        require(SUCCEEDED(manager->CreateDocumentMgr(&otherDocument)) &&
                    SUCCEEDED(otherDocument->CreateContext(
                        client, 0, static_cast<ITextStoreACP *>(otherStore.p),
                        &otherContext, &cookie)) &&
                    SUCCEEDED(otherDocument->Push(otherContext)) &&
                    SUCCEEDED(manager->SetFocus(otherDocument)),
                "focus switch failed");
        require(SUCCEEDED(events->OnSetFocus(otherDocument, document)),
                "focus callback failed");
        pump(200);
        require(store->text == hello && otherStore->text.empty(),
                "focus switch leaked composition");
        require(SUCCEEDED(keyboard->SetValue(client, &closed)),
                "keyboard close before focus switch failed");
        pump(200);
        require(SUCCEEDED(manager->SetFocus(document)) &&
                    SUCCEEDED(events->OnSetFocus(document, otherDocument)),
                "English mode focus switch failed");
        pump(200);
        requirePassedThrough(keys, context, 'N');
        requirePassedThrough(keys, context, VK_SPACE);
        require(SUCCEEDED(manager->SetFocus(otherDocument)) &&
                    SUCCEEDED(events->OnSetFocus(otherDocument, document)) &&
                    SUCCEEDED(keyboard->SetValue(client, &open)),
                "Pinyin focus restoration failed");
        pump(200);
        press(keys, otherContext, 'N', false);
        require(SUCCEEDED(manager->SetFocus(document)),
                "second focus switch failed");
        require(SUCCEEDED(events->OnSetFocus(document, otherDocument)),
                "second focus callback failed");
        pump(200);
        require(store->text == hello && otherStore->text.empty(),
                "late edit reached the wrong context");
        fcitx::PipeClient configuration;
        fcitx::ipc::SettingsReply originalSettings, appliedSettings;
        require(configuration.connect() &&
                    configuration.getSettings(originalSettings),
                "TSF settings connection failed");
        auto newSettings = originalSettings.settings;
        newSettings.scheme = fcitx::ipc::PinyinScheme::Double;
        newSettings.profile = fcitx::ipc::ShuangpinProfile::Xiaohe;
        press(keys, context, 'N', false);
        require(configuration.setSettings(newSettings, appliedSettings) &&
                    appliedSettings.error == fcitx::ipc::SettingsError::None,
                "TSF Xiaohe switch failed");
        pump(250);
        require(store->text == hello,
                "old asynchronous preedit survived scheme change");
        for (const auto letter : std::string("NIHC")) {
            press(keys, context, letter);
        }
        press(keys, context, VK_SPACE, false);
        newSettings = appliedSettings.settings;
        newSettings.profile = fcitx::ipc::ShuangpinProfile::Ziranma;
        require(configuration.setSettings(newSettings, appliedSettings) &&
                    appliedSettings.error == fcitx::ipc::SettingsError::None,
                "TSF Ziranma switch failed");
        pump(200);
        require(store->text == hello + hello,
                "settings change lost the queued real Xiaohe commit");
        for (const auto letter : std::string("NIHK")) {
            press(keys, context, letter);
        }
        press(keys, context, VK_SPACE);
        require(store->text == hello + hello + hello,
                "TSF did not commit real Ziranma Chinese text");
        newSettings = originalSettings.settings;
        newSettings.revision = appliedSettings.settings.revision;
        require(configuration.setSettings(newSettings, appliedSettings) &&
                    appliedSettings.error == fcitx::ipc::SettingsError::None,
                "TSF settings restoration failed");
        pump(200);
        fcitx::ipc::InputMethodsReply originalMethods, methods;
        require(configuration.getInputMethods(originalMethods),
                "method catalog failed");
        auto selection = originalMethods.selection;
        selection.enabled = {"keyboard-us", "pinyin", "shuangpin", "wbx", "db"};
        require(configuration.setInputMethods(selection, methods) &&
                    methods.error == fcitx::ipc::SettingsError::None,
                "multi-method TSF setup failed");
        pump(200);
        for (const auto *expected :
             {L"\u53cc", L"\u4e94", L"\u7535", L"A", L"\u62fc"}) {
            require(SUCCEEDED(keys->OnPreservedKey(
                        context, adapter->preservedGuid, &handled)) &&
                        handled,
                    "multi-method preserved key failed");
            pump(200);
            CComBSTR label;
            require(SUCCEEDED(modeButton->GetText(&label)) && label == expected,
                    "multi-method indicator did not follow cycle");
            if (std::wstring(expected) == L"\u4e94") {
                const auto previousText = store->text;
                press(keys, context, 'W');
                press(keys, context, 'Q');
                press(keys, context, '1');
                require(store->text == previousText + L"\u4f60",
                        "real Wubi commit did not reach the TSF text store");
            }
            if (std::wstring(expected) == L"\u7535") {
                const auto previousText = store->text;
                for (const auto key : std::string("0132")) {
                    press(keys, context, key);
                }
                if (store->text == previousText) {
                    press(keys, context, VK_SPACE);
                }
                require(store->text == previousText + L"\u4f60",
                        "numeric table input did not reach the TSF text store");
            }
        }
        require(SUCCEEDED(modeButton->OnClick(TF_LBI_CLK_LEFT, {}, nullptr)),
                "multi-method click failed");
        pump(200);
        CComBSTR shuangpinLabel;
        require(SUCCEEDED(modeButton->GetText(&shuangpinLabel)) &&
                    shuangpinLabel == L"\u53cc",
                "click did not cycle to shuangpin");
        require(SUCCEEDED(manager->SetFocus(otherDocument)) &&
                    SUCCEEDED(events->OnSetFocus(otherDocument, document)),
                "multi-method focus switch failed");
        pump(200);
        CComBSTR focusedLabel;
        require(SUCCEEDED(modeButton->GetText(&focusedLabel)) &&
                    focusedLabel == L"\u53cc",
                "focus lost selected shuangpin");
        selection = originalMethods.selection;
        selection.revision = methods.selection.revision;
        require(configuration.setInputMethods(selection, methods) &&
                    methods.error == fcitx::ipc::SettingsError::None,
                "method list restore failed");
        pump(200);
        require(SUCCEEDED(manager->SetFocus(document)) &&
                    SUCCEEDED(events->OnSetFocus(document, otherDocument)),
                "restore focus after multi-method test failed");
        pump(200);
        require(SUCCEEDED(service->Deactivate()), "TIP deactivation failed");
        require(adapter->removeItemCalls == 1 && !adapter->langBarItem,
                "input mode item was not removed on deactivation");
        DWORD inactiveStatus = 0;
        require(SUCCEEDED(modeButton->GetStatus(&inactiveStatus)) &&
                    (inactiveStatus & TF_LBI_STATUS_HIDDEN),
                "retained input mode item was not detached");
        adapter->addItemResult = E_FAIL;
        require(adapter->unpreserveCalls == 6,
                "initial preserved key cleanup failed");
        require(SUCCEEDED(service->ActivateEx(adapter, client, 0)) &&
                    !adapter->langBarItem,
                "optional language bar failure disabled input");
        require(SUCCEEDED(service->Deactivate()),
                "activation without language bar did not clean up");
        adapter->addItemResult = S_OK;
        require(adapter->unpreserveCalls == 12,
                "preserved key was not removed on deactivation");
        adapter->preserveResult = E_FAIL;
        require(SUCCEEDED(service->ActivateEx(adapter, client, 0)),
                "preserved-key failure disabled the TIP");
        require(adapter->langBarItem && adapter->langBarItem != modeButton,
                "reactivation did not create a fresh input mode item");
        require(SUCCEEDED(modeButton->OnClick(TF_LBI_CLK_LEFT, {}, nullptr)),
                "detached input mode click failed");
        pump(200);
        require(SUCCEEDED(keyboard->GetValue(&mode)) && mode.vt == VT_I4 &&
                    mode.lVal == 1,
                "detached input mode item changed the new activation");
        require(SUCCEEDED(keyboard->SetValue(client, &closed)),
                "system close without preserved key failed");
        pump(200);
        requirePassedThrough(keys, context, 'N');
        require(SUCCEEDED(keyboard->SetValue(client, &open)),
                "system reopen without preserved key failed");
        pump(200);
        require(SUCCEEDED(keys->OnTestKeyDown(context, 'N', 0, &handled)) &&
                    handled,
                "system mode sync depends on preserved-key registration");
        require(SUCCEEDED(service->Deactivate()), "second deactivation failed");
        require(adapter->removeItemCalls == 2 && !adapter->langBarItem,
                "reactivated input mode item leaked");
        require(adapter->unpreserveCalls == 12,
                "failed preserved-key registration was unregistered");
        require(SUCCEEDED(keyboard->SetValue(client, &closed)),
                "keyboard close after deactivation failed");
        require(SUCCEEDED(keyboard->SetValue(client, &open)),
                "keyboard reopen after deactivation failed");
        manager->SetFocus(nullptr);
        document->Pop(TF_POPF_ALL);
        otherDocument->Pop(TF_POPF_ALL);
        pump();
        manager->Deactivate();
        attributes.Release();
        events.Release();
        compositionSink.Release();
        keys.Release();
        service.Release();
        factory.Release();
        require(canUnload() == S_FALSE,
                "display attribute objects did not keep DLL loaded");
        attributeInfo.Release();
        attributeEnumerator.Release();
        require(canUnload() == S_FALSE,
                "retained input mode item did not keep DLL loaded");
        modeButton.Release();
        require(canUnload() == S_OK, "TIP/edit session reference leak");
        std::cout << "PASS: actual DLL factory, TSF context, system keyboard "
                     "open/close, input mode item/click/lifetime, "
                     "preserved-key modes, "
                     "pass-through, Chinese commit, "
                     "async edits, cancellation and focus isolation (key-sink "
                     "and language-bar adapters, no registration)\n";
        exit = 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
    }
    if (exit == 0)
        FreeLibrary(module);
    CoUninitialize();
    return exit;
}
