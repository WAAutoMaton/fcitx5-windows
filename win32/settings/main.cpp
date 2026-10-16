#include "../i18n/translator.h"
#include "../ipc/diagnostic_log.h"
#include "../ipc/service.h"
#include "../tsf/pipeclient.h"
#include "App.h"
#include "diagnostics.h"
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>
#include <cstdio>
#include <shellapi.h>

#ifdef FCITX5_UI_TEST
static winrt::fire_and_forget diagnosticAsyncFailure() try {
    const fcitx::settings::diagnostic::Operation operation("test_async");
    co_await winrt::resume_background();
    operation.stage("background");
    throw winrt::hresult_error(E_ACCESSDENIED,
                               L"private-diagnostic-test-value");
} catch (...) {
    fcitx::settings::diagnostic::exception("test_async", true);
    throw;
}

static int diagnosticTest(std::wstring_view mode) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    auto &logger = fcitx::win32::diagnostic::settingsLogger();
    logger.setEnabled(mode != L"off");
    logger.event(fcitx::win32::diagnostic::Level::Info, "settings_start");
    if (mode == L"async") {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        diagnosticAsyncFailure();
        Sleep(INFINITE);
    }
    if (mode == L"terminate") {
        try {
            throw std::runtime_error("private-diagnostic-test-value");
        } catch (...) {
            std::terminate();
        }
    }
    try {
        const fcitx::settings::diagnostic::Operation operation(
            "test_recoverable");
        operation.stage("throw_hresult");
        throw winrt::hresult_error(E_ACCESSDENIED,
                                   L"private-diagnostic-test-value");
    } catch (...) {
        fcitx::settings::diagnostic::exception("test_recoverable");
    }
    return fcitx::settings::diagnostic::exit("test_completed", 0);
}
#endif

namespace fcitx {
namespace {
ipc::ServiceProcess *settingsService = nullptr;
}
bool settingsServiceStopping() {
    return settingsService && settingsService->stopping();
}
} // namespace fcitx

static int runSettings() {
    fcitx::ipc::GlobalSettings languageSettings;
    try {
        fcitx::PipeClient client;
        client.setLogger(&fcitx::win32::diagnostic::settingsLogger());
        fcitx::ipc::GlobalSettingsReply reply;
        if (client.connect() && client.getGlobalSettings(reply) &&
            reply.error == fcitx::ipc::SettingsError::None)
            languageSettings = reply.settings;
    } catch (...) {
        fcitx::settings::diagnostic::exception("startup_global_settings");
    }
    const fcitx::win32::i18n::Translator translator(languageSettings.language);
    fcitx::win32::diagnostic::settingsLogger().setEnabled(
        languageSettings.detailedLogging);
    fcitx::win32::diagnostic::settingsLogger().event(
        fcitx::win32::diagnostic::Level::Info, "settings_start");
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
        return fcitx::settings::diagnostic::exit("existing_instance", 0);
    }
    if (!fcitx::ipc::serviceAutoStartAllowed()) {
        return fcitx::settings::diagnostic::exit("service_stopped", 0);
    }
    fcitx::settingsService = &service;
    const PACKAGE_VERSION minimum{WINDOWSAPPSDK_RUNTIME_VERSION_UINT64};
    const auto initialized = MddBootstrapInitialize2(
        WINDOWSAPPSDK_RELEASE_MAJORMINOR, WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
        minimum, MddBootstrapInitializeOptions_None);
    if (FAILED(initialized)) {
        fcitx::win32::diagnostic::settingsLogger().event(
            fcitx::win32::diagnostic::Level::Error, "settings_runtime_error",
            {{"hr", static_cast<uint32_t>(initialized)}});
        wchar_t message[256]{};
        swprintf_s(message,
                   L"%ls\n"
                   L"Framework / DDLM >= %ls\nHRESULT: 0x%08X",
                   translator
                       .wide("settings",
                             "The Windows App SDK 1.8 x64 runtime is required.")
                       .c_str(),
                   WINDOWSAPPSDK_RUNTIME_VERSION_DOTQUADSTRING_W,
                   static_cast<unsigned>(initialized));
        MessageBoxW(nullptr, message, L"Fcitx5 settings", MB_OK | MB_ICONERROR);
        fcitx::settingsService = nullptr;
        return fcitx::settings::diagnostic::exit(
            "runtime_initialization_failed", 1);
    }
    int result = 0;
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Microsoft::UI::Xaml::Application::Start([](auto &&) {
            winrt::make<winrt::Fcitx5Settings::implementation::App>();
        });
        fcitx::win32::diagnostic::settingsLogger().event(
            fcitx::win32::diagnostic::Level::Info,
            "settings_dispatcher_returned");
    } catch (const winrt::hresult_error &error) {
        fcitx::win32::diagnostic::settingsLogger().event(
            fcitx::win32::diagnostic::Level::Error, "settings_ui_error",
            {{"hr", static_cast<uint32_t>(error.code())}});
        MessageBoxW(nullptr, error.message().c_str(), L"Fcitx5 settings",
                    MB_OK | MB_ICONERROR);
        result = 1;
    }
    fcitx::win32::diagnostic::settingsLogger().event(
        fcitx::win32::diagnostic::Level::Info,
        "settings_runtime_shutdown_begin");
    MddBootstrapShutdown();
    fcitx::settingsService = nullptr;
    return fcitx::settings::diagnostic::exit(
        result ? "ui_error" : "dispatcher_returned", result);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const fcitx::settings::diagnostic::TerminateHandler terminateHandler;
    int argumentCount = 0;
    const std::unique_ptr<wchar_t *, decltype(&LocalFree)> arguments(
        CommandLineToArgvW(GetCommandLineW(), &argumentCount), LocalFree);
    if (!arguments)
        return static_cast<int>(GetLastError());
    const std::wstring_view command =
        argumentCount == 2 ? arguments.get()[1] : L"";
    if (command == L"--check-settings-diagnostics-test") {
#ifdef FCITX5_UI_TEST
        return 0;
#else
        return ERROR_NOT_SUPPORTED;
#endif
    }
#ifdef FCITX5_UI_TEST
    constexpr std::wstring_view diagnosticPrefix = L"--diagnostic-test-";
    if (command.starts_with(diagnosticPrefix))
        return diagnosticTest(command.substr(diagnosticPrefix.size()));
#endif
    if (fcitx::ipc::hostOnlyClient()) {
        OutputDebugStringW(
            L"Fcitx5: Settings requires an unrestricted host process.\n");
        return ERROR_ACCESS_DENIED;
    }
    try {
        fcitx::ipc::requireHostServices();
        if (command == L"--runtime-check") {
            const PACKAGE_VERSION minimum{WINDOWSAPPSDK_RUNTIME_VERSION_UINT64};
            const auto result = MddBootstrapInitialize2(
                WINDOWSAPPSDK_RELEASE_MAJORMINOR,
                WINDOWSAPPSDK_RELEASE_VERSION_TAG_W, minimum,
                MddBootstrapInitializeOptions_None);
            if (SUCCEEDED(result)) {
                MddBootstrapShutdown();
            }
            return static_cast<int>(result);
        }
        return runSettings();
    } catch (const std::exception &) {
        fcitx::settings::diagnostic::exception("settings_main");
        MessageBoxW(nullptr, L"Cannot initialize the Fcitx5 settings service.",
                    L"Fcitx5 settings", MB_OK | MB_ICONERROR);
        return fcitx::settings::diagnostic::exit("initialization_exception", 1);
    } catch (...) {
        fcitx::settings::diagnostic::exception("settings_main", true);
        winrt::terminate();
    }
}
