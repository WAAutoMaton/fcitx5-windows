#include "../dll/util.h"
#include "../ipc/service.h"
#include "../tsf/langbaritem.h"
#include "../tsf/pipeclient.h"
#include "threadmgradapter.h"
#include <iostream>

namespace ipc = fcitx::win32::ipc;
namespace fs = std::filesystem;

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void pump(DWORD duration = 100) {
    const auto end = GetTickCount64() + duration;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    } while (GetTickCount64() < end);
}

template <typename Predicate>
void waitFor(Predicate condition, const char *message) {
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        pump();
        if (condition()) {
            pump(200);
            return;
        }
    }
    require(false, message);
}

DWORD settingsPid() {
    DWORD pid = 0;
    const auto property = ipc::settingsActivationBase() + L"-window";
    struct Search {
        const wchar_t *property;
        DWORD *pid;
    } search{property.c_str(), &pid};
    EnumWindows(
        [](HWND window, LPARAM argument) -> BOOL {
            const auto *search = reinterpret_cast<const Search *>(argument);
            if (GetPropW(window, search->property)) {
                GetWindowThreadProcessId(window, search->pid);
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return pid;
}
} // namespace

int wmain(int count, wchar_t **arguments) {
    if (count != 2 &&
        (count != 3 || std::wstring(arguments[2]) != L"--settings")) {
        return 2;
    }
    const auto withSettings =
        count == 3 && std::wstring(arguments[2]) == L"--settings";
    const auto dll = fs::absolute(arguments[1]);
    const auto core = dll.parent_path().parent_path() / "bin" / "Fcitx5.exe";
    const auto identity = ipc::serviceIdentity();
    const auto statePath = ipc::serviceStatePath(identity);
    const auto hadState = fs::exists(statePath);
    const auto previousState = ipc::readServiceState(identity);
    {
        ipc::ServiceController controller(core);
        fcitx::PipeClient connection;
        require(!controller.running(L"Core") &&
                    !controller.running(L"Settings") && !connection.connect(),
                "Stop existing services before this isolated probe");
    }
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)),
            "COM initialization failed");
    const auto module = LoadLibraryW(dll.c_str());
    require(module != nullptr, "Cannot load the deployed TSF DLL");
    int result = 1;
    CComPtr<ITfTextInputProcessorEx> tip;
    CComPtr<ITfThreadMgrEx> manager;
    try {
        const ipc::ServiceState enabled{};
        require(ipc::replaceFile(
                    statePath,
                    std::string_view(reinterpret_cast<const char *>(&enabled),
                                     sizeof(enabled))),
                "Cannot prepare isolated service state");
        using GetFactory = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
        const auto getFactory = reinterpret_cast<GetFactory>(
            GetProcAddress(module, "DllGetClassObject"));
        CComPtr<IClassFactory> factory;
        require(
            getFactory &&
                SUCCEEDED(
                    getFactory(fcitx::FCITX_CLSID, IID_PPV_ARGS(&factory))) &&
                SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&tip))),
            "Cannot create the actual TIP");
        require(SUCCEEDED(manager.CoCreateInstance(CLSID_TF_ThreadMgr)),
                "Cannot create TSF manager");
        TfClientId client = TF_CLIENTID_NULL;
        require(SUCCEEDED(manager->ActivateEx(&client, TF_TMAE_NOACTIVATETIP)),
                "Cannot activate TSF manager");
        CComPtr<ThreadMgrAdapter> adapter;
        adapter.Attach(new ThreadMgrAdapter(manager));
        require(SUCCEEDED(tip->ActivateEx(adapter, client, 0)),
                "Cannot activate TIP");
        const auto coreReady = [] {
            fcitx::PipeClient connection;
            return connection.connect();
        };
        waitFor(
            coreReady,
            "TIP did not automatically start Core without a focused document");
        require(settingsPid() == 0, "Automatic Core startup opened settings");
        CComPtr<ITfLangBarItemButton> button;
        require(SUCCEEDED(adapter->langBarItem.QueryInterface(&button)),
                "Language bar button missing");
        const auto stopped = [&] {
            ipc::ServiceController controller(core);
            return !controller.running(L"Core") &&
                   !controller.running(L"Settings");
        };
        const auto select = [&](UINT command) {
            require(SUCCEEDED(button->OnMenuSelect(command)),
                    "Menu command failed");
        };
        select(fcitx::LangBarItem::kStopMenuId);
        waitFor(stopped, "Stop command did not stop services");
        require(SUCCEEDED(tip->Deactivate()) &&
                    SUCCEEDED(tip->ActivateEx(adapter, client, 0)),
                "TIP reactivation failed");
        pump(2500);
        require(stopped() && !coreReady(),
                "TIP reactivation undid the manual service stop");
        button.Release();
        require(SUCCEEDED(adapter->langBarItem.QueryInterface(&button)),
                "Reactivated button missing");
        select(fcitx::LangBarItem::kRestartMenuId);
        waitFor(coreReady, "Restart did not start a stopped Core");
        require(settingsPid() == 0,
                "Restart opened settings that had been closed");
        if (withSettings) {
            select(fcitx::LangBarItem::kSettingsMenuId);
            waitFor([] { return settingsPid() != 0; },
                    "Settings command did not open the WinUI window");
            const auto firstSettings = settingsPid();
            select(fcitx::LangBarItem::kRestartMenuId);
            waitFor(
                [&] {
                    const auto pid = settingsPid();
                    return pid && pid != firstSettings && coreReady();
                },
                "Restart did not reopen the previously open settings window");
            select(fcitx::LangBarItem::kStopMenuId);
            waitFor(stopped, "Stop before the Settings-host test failed");
            select(fcitx::LangBarItem::kRestartMenuId);
            waitFor(coreReady,
                    "Core restart before the Settings-host test failed");
            {
                // Model a TSF thread hosted by the Settings service process.
                ipc::ServiceProcess selfSettings(L"Settings");
                require(selfSettings.acquire(),
                        "Cannot model the Settings host");
                select(fcitx::LangBarItem::kRestartMenuId);
                waitFor([&] { return selfSettings.stopping(); },
                        "Settings-host restart did not delegate its shutdown");
            }
            waitFor([&] { return settingsPid() != 0 && coreReady(); },
                    "Settings-host restart did not reopen services after owner "
                    "exit");
        }
        select(fcitx::LangBarItem::kStopMenuId);
        waitFor(stopped, "Final stop failed");
        pump(1000);
        require(stopped(), "Stopped Core was automatically relaunched");
        if (withSettings) {
            ipc::launchServiceExecutable(core.parent_path().parent_path() /
                                             "settings" / "Fcitx5Settings.exe",
                                         false);
            pump(500);
            require(stopped() && settingsPid() == 0,
                    "Settings launched after manual stop did not exit");
        }
        require(SUCCEEDED(tip->Deactivate()), "TIP deactivation failed");
        button.Release();
        adapter.Release();
        factory.Release();
        tip.Release();
        manager->Deactivate();
        manager.Release();
        using CanUnload = HRESULT(WINAPI *)();
        const auto canUnload = reinterpret_cast<CanUnload>(
            GetProcAddress(module, "DllCanUnloadNow"));
        require(canUnload && canUnload() == S_OK,
                "Service task leaked a DLL reference");
        std::cout
            << "PASS: actual TIP automatic Core startup, menu stop/restart, "
               "manual-stop persistence, on-demand settings and DLL lifetime\n";
        result = 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        if (tip) {
            tip->Deactivate();
        }
        if (manager) {
            manager->Deactivate();
        }
    }
    {
        ipc::ServiceController controller(core);
        controller.stop();
    }
    if (hadState) {
        ipc::replaceFile(
            statePath,
            std::string_view(reinterpret_cast<const char *>(&previousState),
                             sizeof(previousState)));
    } else {
        fs::remove(statePath);
    }
    tip.Release();
    manager.Release();
    if (!result) {
        FreeLibrary(module);
    }
    CoUninitialize();
    return result;
}
