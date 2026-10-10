#include "App.h"
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>
#include <cstdio>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
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
    return result;
}
