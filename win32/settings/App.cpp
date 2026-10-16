#include "App.h"
#include "App.g.cpp"
#include "diagnostics.h"
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

namespace winrt::Fcitx5Settings::implementation {
App::App() {
    UnhandledException(
        [](auto &&,
           const Microsoft::UI::Xaml::UnhandledExceptionEventArgs &args) {
            auto &logger = fcitx::win32::diagnostic::settingsLogger();
            logger.event(fcitx::win32::diagnostic::Level::Error,
                         "settings_xaml_unhandled_exception",
                         {{"hr", static_cast<uint32_t>(args.Exception())}});
            logger.flush(std::chrono::milliseconds(250));
        });
}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const &) {
    try {
        const fcitx::settings::diagnostic::Operation operation("app_launch");
        Microsoft::UI::Xaml::ResourceDictionary resources;
        resources.MergedDictionaries().Append(
            Microsoft::UI::Xaml::Controls::XamlControlsResources());
        Resources(resources);
        window_ = std::make_shared<fcitx::SettingsWindow>();
        if (!window_->open()) {
            fcitx::win32::diagnostic::settingsLogger().event(
                fcitx::win32::diagnostic::Level::Error, "settings_app_exit",
                {{"reason", "window_open_failed"}});
            Exit();
        }
    } catch (const hresult_error &error) {
        fcitx::settings::diagnostic::exception("app_launch");
        fcitx::win32::diagnostic::settingsLogger().event(
            fcitx::win32::diagnostic::Level::Error, "settings_app_exit",
            {{"reason", "launch_exception"}});
        MessageBoxW(nullptr, error.message().c_str(), L"Fcitx5 settings",
                    MB_OK | MB_ICONERROR);
        Exit();
    }
}

Microsoft::UI::Xaml::Markup::IXamlType
App::GetXamlType(Windows::UI::Xaml::Interop::TypeName const &type) {
    return metadata_.GetXamlType(type);
}
Microsoft::UI::Xaml::Markup::IXamlType App::GetXamlType(hstring const &name) {
    return metadata_.GetXamlType(name);
}
com_array<Microsoft::UI::Xaml::Markup::XmlnsDefinition>
App::GetXmlnsDefinitions() {
    return metadata_.GetXmlnsDefinitions();
}
} // namespace winrt::Fcitx5Settings::implementation
