#include "../dll/util.h"
#include "threadmgradapter.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void exercise(HMODULE module) {
    using GetFactory = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
    using CanUnload = HRESULT(WINAPI *)();
    const auto getFactory = reinterpret_cast<GetFactory>(
        GetProcAddress(module, "DllGetClassObject"));
    const auto canUnload =
        reinterpret_cast<CanUnload>(GetProcAddress(module, "DllCanUnloadNow"));
    require(getFactory && canUnload && canUnload() == S_OK,
            "DLL exports or initial unload state failed");
    {
        CComPtr<IClassFactory> factory;
        require(SUCCEEDED(getFactory(fcitx::FCITX_CLSID, IID_IClassFactory,
                                     reinterpret_cast<void **>(&factory))),
                "class factory failed");
        CComPtr<ITfTextInputProcessorEx> service;
        require(
            SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&service))),
            "TIP creation failed");
        CComPtr<ITfThreadMgrEx> manager;
        require(SUCCEEDED(manager.CoCreateInstance(CLSID_TF_ThreadMgr)),
                "thread manager creation failed");
        TfClientId client = TF_CLIENTID_NULL;
        require(SUCCEEDED(manager->ActivateEx(&client, TF_TMAE_NOACTIVATETIP)),
                "thread manager activation failed");
        CComPtr<ThreadMgrAdapter> adapter;
        adapter.Attach(new ThreadMgrAdapter(manager));
        require(SUCCEEDED(service->ActivateEx(adapter, client, 0)),
                "TIP activation and pipe mutex failed");
        CComPtr<ITfLangBarItemButton> button;
        require(SUCCEEDED(adapter->langBarItem.QueryInterface(&button)),
                "language bar item missing");
        CComBSTR text;
        require(SUCCEEDED(button->GetTooltipString(&text)) && text.Length(),
                "translated language bar text failed");
        require(canUnload() == S_FALSE, "live TIP allowed DLL unloading");
        HWND window = nullptr;
        while ((window = FindWindowExW(HWND_MESSAGE, window,
                                       L"Fcitx5WindowsDispatchV2", nullptr))) {
            DWORD process = 0;
            if (GetWindowThreadProcessId(window, &process) ==
                    GetCurrentThreadId() &&
                process == GetCurrentProcessId())
                break;
        }
        require(window != nullptr, "TIP dispatch window missing");
        // Start the DLL's translation worker without dispatching Core startup.
        SendMessageW(window, WM_TIMER, 1, 0);
        require(SUCCEEDED(service->Deactivate()),
                "TIP deactivation and pipe mutex failed");
        require(adapter->removeItemCalls == 1, "language bar cleanup failed");
        require(SUCCEEDED(manager->Deactivate()),
                "thread manager deactivation failed");
    }
    require(canUnload() == S_OK, "COM references survived deactivation");
}
} // namespace

int wmain(int count, wchar_t **arguments) {
    if (count != 2 && count != 3)
        return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    HMODULE runtime = nullptr;
    HMODULE module = nullptr;
    bool comInitialized = false;
    try {
        if (count == 3) {
            require(!GetModuleHandleW(L"MSVCP140.dll"),
                    "MSVCP140.dll was loaded before the requested runtime");
            wchar_t absolute[32768]{};
            const auto length = GetFullPathNameW(
                arguments[2], ARRAYSIZE(absolute), absolute, nullptr);
            require(length && length < ARRAYSIZE(absolute),
                    "runtime path resolution failed");
            runtime = LoadLibraryExW(absolute, nullptr,
                                     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                         LOAD_LIBRARY_SEARCH_SYSTEM32);
            require(runtime && GetModuleHandleW(L"MSVCP140.dll") == runtime,
                    "requested MSVCP140.dll preload failed");
            wchar_t loaded[32768]{};
            require(GetModuleFileNameW(runtime, loaded, ARRAYSIZE(loaded)) &&
                        _wcsicmp(absolute, loaded) == 0,
                    "a different MSVCP140.dll was loaded");
        }
        require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)),
                "COM initialization failed");
        comInitialized = true;
        for (int cycle = 0; cycle < 3; ++cycle) {
            module = LoadLibraryW(arguments[1]);
            require(module != nullptr, "TSF DLL load failed");
            exercise(module);
            require(FreeLibrary(module), "TSF DLL release failed");
            const auto address = reinterpret_cast<LPCWSTR>(module);
            module = nullptr;
            const auto deadline = GetTickCount64() + 2000;
            HMODULE remaining = nullptr;
            while (GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                address, &remaining)) {
                require(GetTickCount64() < deadline,
                        "translation worker kept DLL loaded after release");
                Sleep(5);
            }
        }
        CoUninitialize();
        if (runtime)
            FreeLibrary(runtime);
        std::cout
            << "PASS: DLL load/create/activate, IPC mutex, translated COM "
               "text, translation worker exit and deactivate/unload cycles"
            << (count == 3 ? " with preloaded MSVCP140.dll" : "")
            << " (no registration or Core startup)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        if (comInitialized)
            CoUninitialize();
        // A failed activation may retain callbacks into the DLL until exit.
        return 1;
    }
}
