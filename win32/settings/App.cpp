#include "App.h"
#include "App.g.cpp"
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

namespace winrt::Fcitx5Settings::implementation {
App::App() = default;

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const &) {
    try {
        Microsoft::UI::Xaml::ResourceDictionary resources;
        resources.MergedDictionaries().Append(
            Microsoft::UI::Xaml::Controls::XamlControlsResources());
        Resources(resources);
        window_ = std::make_shared<fcitx::SettingsWindow>();
        if (!window_->open()) {
            Exit();
        }
    } catch (const hresult_error &error) {
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
