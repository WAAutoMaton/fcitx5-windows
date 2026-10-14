#include <windows.h>
#undef GetCurrentTime
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.h>

using namespace winrt;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;

struct FixtureApp : ApplicationT<FixtureApp> {
    void OnLaunched(LaunchActivatedEventArgs const &) {
        StackPanel panel;
        panel.Margin({24, 24, 24, 24});
        panel.Spacing(16);
        TextBlock title;
        title.Text(L"Fcitx5 Modern Input Fixture");
        title.FontSize(24);
        title.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(title);
        TextBox first;
        first.Header(box_value(L"TextBox"));
        first.Name(L"FirstInput");
        first.AcceptsReturn(true);
        first.MinHeight(80);
        panel.Children().Append(first);
        TextBox second;
        second.Header(box_value(L"Second TextBox"));
        second.Name(L"SecondInput");
        panel.Children().Append(second);
        RichEditBox rich;
        rich.Header(box_value(L"RichEditBox"));
        rich.Name(L"RichInput");
        rich.MinHeight(100);
        panel.Children().Append(rich);
        PasswordBox password;
        password.Header(box_value(L"PasswordBox"));
        password.Name(L"PasswordInput");
        panel.Children().Append(password);
        ScrollViewer scroll;
        scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        scroll.Content(panel);
        Window::Current().Content(scroll);
        Window::Current().Activate();
    }
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    init_apartment(apartment_type::single_threaded);
    Application::Start([](ApplicationInitializationCallbackParams const &) {
        make<FixtureApp>();
    });
    return 0;
}
