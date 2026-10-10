#include "../ipc/service.h"
#include "App.h"
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>
#include <cstdio>

namespace fcitx {
namespace {
ipc::ServiceProcess *settingsService = nullptr;
}
bool settingsServiceStopping() {
    return settingsService && settingsService->stopping();
}
} // namespace fcitx

static int runSettings() {
    const auto activationName = fcitx::ipc::settingsActivationBase();
    fcitx::ipc::ServiceSecurity security;
    fcitx::ipc::ServiceHandle activation(CreateEventW(
        security.get(), FALSE, FALSE, (activationName + L"-activate").c_str()));
    if (!activation.get()) {
        fcitx::ipc::serviceError();
    }
    fcitx::ipc::ServiceProcess service(L"Settings");
    if (!service.acquire()) {
        const auto property = activationName + L"-window";
        EnumWindows(
            [](HWND window, LPARAM argument) -> BOOL {
                if (GetPropW(window,
                             reinterpret_cast<const wchar_t *>(argument))) {
                    DWORD process = 0;
                    GetWindowThreadProcessId(window, &process);
                    AllowSetForegroundWindow(process);
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(property.c_str()));
        SetEvent(activation.get());
        return 0;
    }
    if (!fcitx::ipc::serviceAutoStartAllowed()) {
        return 0;
    }
    fcitx::settingsService = &service;
    const PACKAGE_VERSION minimum{WINDOWSAPPSDK_RUNTIME_VERSION_UINT64};
    const auto initialized = MddBootstrapInitialize2(
        WINDOWSAPPSDK_RELEASE_MAJORMINOR, WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
        minimum, MddBootstrapInitializeOptions_None);
    if (FAILED(initialized)) {
        wchar_t message[256]{};
        swprintf_s(message,
                   L"\u9700\u8981\u5b8c\u6574\u7684 Windows App SDK 1.8 x64 "
                   L"\u8fd0\u884c\u65f6\u3002\n"
                   L"Framework / DDLM >= %ls\nHRESULT: 0x%08X",
                   WINDOWSAPPSDK_RUNTIME_VERSION_DOTQUADSTRING_W,
                   static_cast<unsigned>(initialized));
        MessageBoxW(nullptr, message, L"Fcitx5 settings", MB_OK | MB_ICONERROR);
        return 1;
    }
    int result = 0;
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Microsoft::UI::Xaml::Application::Start([](auto &&) {
            winrt::make<winrt::Fcitx5Settings::implementation::App>();
        });
    } catch (const winrt::hresult_error &error) {
        MessageBoxW(nullptr, error.message().c_str(), L"Fcitx5 settings",
                    MB_OK | MB_ICONERROR);
        result = 1;
    }
    MddBootstrapShutdown();
    fcitx::settingsService = nullptr;
    return result;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        return runSettings();
    } catch (const std::exception &) {
        MessageBoxW(nullptr, L"Cannot initialize the Fcitx5 settings service.",
                    L"Fcitx5 settings", MB_OK | MB_ICONERROR);
        return 1;
    }
}
