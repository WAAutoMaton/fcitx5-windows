#pragma once

#include "App.g.h"
#include "SettingsWindow.h"
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>

namespace winrt::Fcitx5Settings::implementation {
struct App : AppT<App, Microsoft::UI::Xaml::Markup::IXamlMetadataProvider> {
    App();
    void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const &);
    Microsoft::UI::Xaml::Markup::IXamlType
    GetXamlType(Windows::UI::Xaml::Interop::TypeName const &type);
    Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(hstring const &name);
    com_array<Microsoft::UI::Xaml::Markup::XmlnsDefinition>
    GetXmlnsDefinitions();

  private:
    Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider
        metadata_;
    std::shared_ptr<fcitx::SettingsWindow> window_;
};
} // namespace winrt::Fcitx5Settings::implementation

namespace winrt::Fcitx5Settings::factory_implementation {
struct App : AppT<App, implementation::App> {};
} // namespace winrt::Fcitx5Settings::factory_implementation
