#include "SettingsWindow.h"
#include "../ipc/transport.h"
#include <commctrl.h>
#undef GetCurrentTime
#include <chrono>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

namespace fcitx {
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace {
constexpr const wchar_t *profileNames[] = {
    L"\u81ea\u7136\u7801",
    L"\u5fae\u8f6f",
    L"\u7d2b\u5149",
    L"\u667a\u80fd ABC",
    L"\u4e2d\u6587\u4e4b\u661f",
    L"\u62fc\u97f3\u52a0\u52a0",
    L"\u5c0f\u9e64",
    L"\u56fd\u6807",
    L"\u73b0\u6709\u81ea\u5b9a\u4e49\u65b9\u6848"};

const wchar_t *errorText(ipc::SettingsError error) {
    switch (error) {
    case ipc::SettingsError::Conflict:
        return L"\u8bbe\u7f6e\u5df2\u53d8\u66f4\u3002\u518d\u6b21\u786e\u5b9a"
               L"\u5c06\u5e94\u7528\u5f53\u524d\u9009\u62e9\u3002";
    case ipc::SettingsError::SaveFailed:
        return L"\u65e0\u6cd5\u4fdd\u5b58\u8bbe\u7f6e\u3002\u8bf7\u68c0\u67e5"
               L"\u7528\u6237\u914d\u7f6e\u76ee\u5f55\u7684\u5199\u5165\u6743"
               L"\u9650\u3002";
    case ipc::SettingsError::Unavailable:
        return L"\u62fc\u97f3\u5f15\u64ce\u6216\u8f93\u5165\u65b9\u6848\u4e0d"
               L"\u53ef\u7528\u3002";
    default:
        return L"\u65e0\u6cd5\u5e94\u7528\u8bbe\u7f6e\u3002\u8bf7\u91cd\u8bd5"
               L"\u3002";
    }
}

LRESULT CALLBACK sizing(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                        UINT_PTR id, DWORD_PTR) {
    if (message == WM_GETMINMAXINFO) {
        const auto dpi = GetDpiForWindow(window);
        auto *bounds = reinterpret_cast<MINMAXINFO *>(lParam);
        bounds->ptMinTrackSize = {MulDiv(400, dpi, 96), MulDiv(320, dpi, 96)};
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, sizing, id);
    }
    return DefSubclassProc(window, message, wParam, lParam);
}
} // namespace

SettingsWindow::~SettingsWindow() {
    if (activation_) {
        CloseHandle(activation_);
    }
}

bool SettingsWindow::open() {
    const auto name = ipc::settingsActivationBase();
    activation_ =
        CreateEventW(nullptr, FALSE, FALSE, (name + L"-activate").c_str());
    if (!activation_) {
        throw_last_error();
    }
    activationProperty_ = name + L"-window";
    window_ = Window();
    window_.Title(L"\u8f93\u5165\u6cd5\u8bbe\u7f6e");
    auto root = Markup::XamlReader::Load(LR"(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Padding="24" RowSpacing="16">
  <Grid.RowDefinitions><RowDefinition Height="*"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <ScrollViewer VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled">
    <StackPanel Spacing="16">
      <RadioButtons x:Name="Scheme" Header="&#x8f93;&#x5165;&#x65b9;&#x6848;" MaxColumns="2">
        <RadioButton Content="&#x5168;&#x62fc;"/>
        <RadioButton Content="&#x53cc;&#x62fc;"/>
      </RadioButtons>
      <ComboBox x:Name="Profile" Header="&#x53cc;&#x62fc;&#x952e;&#x4f4d;" HorizontalAlignment="Stretch"/>
      <TextBlock x:Name="Status" TextWrapping="Wrap" AutomationProperties.LiveSetting="Polite"/>
    </StackPanel>
  </ScrollViewer>
  <StackPanel Grid.Row="1" Orientation="Horizontal" HorizontalAlignment="Right" Spacing="8">
    <Button x:Name="Retry" Content="&#x91cd;&#x8bd5;" Visibility="Collapsed"/>
    <Button x:Name="Cancel" Content="&#x53d6;&#x6d88;"/>
    <Button x:Name="Confirm" Content="&#x786e;&#x5b9a;" Style="{StaticResource AccentButtonStyle}"/>
  </StackPanel>
</Grid>)")
                    .as<FrameworkElement>();
    scheme_ = root.FindName(L"Scheme").as<RadioButtons>();
    profile_ = root.FindName(L"Profile").as<ComboBox>();
    status_ = root.FindName(L"Status").as<TextBlock>();
    confirm_ = root.FindName(L"Confirm").as<Button>();
    retry_ = root.FindName(L"Retry").as<Button>();
    root.FindName(L"Cancel").as<Button>().Click(
        [this](auto &&, auto &&) { window_.Close(); });
    scheme_.SelectionChanged([this](auto &&, auto &&) {
        profile_.IsEnabled(loaded_ && scheme_.SelectedIndex() == 1);
    });
    confirm_.Click([this](auto &&, auto &&) { transfer(true); });
    retry_.Click([this](auto &&, auto &&) { transfer(false); });
    window_.Closed([this](auto &&, auto &&) {
        closed_ = true;
        timer_.Stop();
        RemovePropW(handle_, activationProperty_.c_str());
    });
    window_.Content(root);
    window_.SystemBackdrop(Media::MicaBackdrop());
    check_hresult(window_.as<IWindowNative>()->get_WindowHandle(&handle_));
    if (!SetPropW(handle_, activationProperty_.c_str(),
                  reinterpret_cast<HANDLE>(1))) {
        throw_last_error();
    }
    SetWindowSubclass(handle_, sizing, 1, 0);
    const auto dpi = GetDpiForWindow(handle_);
    window_.AppWindow().Resize({MulDiv(520, dpi, 96), MulDiv(360, dpi, 96)});
    timer_ = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread()
                 .CreateTimer();
    timer_.Interval(std::chrono::milliseconds(200));
    timer_.Tick([this](auto &&, auto &&) {
        if (settingsServiceStopping()) {
            window_.Close();
            return;
        }
        if (WaitForSingleObject(activation_, 0) == WAIT_OBJECT_0) {
            ShowWindow(handle_, IsIconic(handle_) ? SW_RESTORE : SW_SHOW);
            window_.Activate();
        }
    });
    timer_.Start();
    window_.Activate();
    transfer(false);
    return true;
}

void SettingsWindow::busy(bool enabled) {
    scheme_.IsEnabled(!enabled && loaded_);
    scheme_.Items().GetAt(1).as<RadioButton>().IsEnabled(doubleAvailable_);
    profile_.IsEnabled(!enabled && loaded_ && scheme_.SelectedIndex() == 1);
    confirm_.IsEnabled(!enabled && loaded_);
    retry_.IsEnabled(!enabled);
}

fire_and_forget SettingsWindow::transfer(bool save) {
    const auto lifetime = shared_from_this();
    const apartment_context ui;
    auto requested = settings_;
    if (save) {
        if (!loaded_ || profile_.SelectedIndex() < 0) {
            co_return;
        }
        requested.scheme = scheme_.SelectedIndex() == 1
                               ? ipc::PinyinScheme::Double
                               : ipc::PinyinScheme::Full;
        requested.profile = profiles_.at(profile_.SelectedIndex());
    }
    busy(true);
    status_.Text(save ? L"\u6b63\u5728\u4fdd\u5b58\u2026"
                      : L"\u6b63\u5728\u52a0\u8f7d\u2026");
    ipc::SettingsReply reply;
    bool connected = false;
    co_await resume_background();
    try {
        PipeClient client;
        connected =
            client.connect() && (save ? client.setSettings(requested, reply)
                                      : client.getSettings(reply));
        if (save && !connected) {
            client.disconnect();
            // A timed-out write may have completed. Query before allowing
            // another write.
            connected = client.connect() && client.getSettings(reply);
            if (connected && (reply.settings.scheme != requested.scheme ||
                              reply.settings.profile != requested.profile)) {
                reply.error = ipc::SettingsError::Conflict;
            }
        }
    } catch (...) {
        connected = false;
    }
    co_await ui;
    if (closed_) {
        co_return;
    }
    if (!connected) {
        status_.Text(L"\u65e0\u6cd5\u8fde\u63a5 "
                     L"Core\u3002\u8bf7\u786e\u8ba4\u5b83\u5df2\u542f\u52a8"
                     L"\u5e76\u91cd\u8bd5\u3002");
        if (save) {
            loaded_ = false;
        }
        retry_.Visibility(Visibility::Visible);
    } else if (reply.error != ipc::SettingsError::None) {
        status_.Text(errorText(reply.error));
        if (reply.error == ipc::SettingsError::Conflict) {
            settings_.revision = reply.settings.revision;
        }
    } else if (save) {
        window_.Close();
        co_return;
    } else {
        settings_ = reply.settings;
        loaded_ = reply.pinyinAvailable;
        doubleAvailable_ = reply.shuangpinAvailable;
        profiles_.clear();
        profile_.Items().Clear();
        constexpr ipc::ShuangpinProfile order[] = {
            ipc::ShuangpinProfile::Xiaohe,
            ipc::ShuangpinProfile::Ziranma,
            ipc::ShuangpinProfile::MS,
            ipc::ShuangpinProfile::Ziguang,
            ipc::ShuangpinProfile::ABC,
            ipc::ShuangpinProfile::Zhongwenzhixing,
            ipc::ShuangpinProfile::PinyinJiajia,
            ipc::ShuangpinProfile::GB,
            ipc::ShuangpinProfile::Custom};
        int selected = -1;
        for (auto profile : order) {
            if (profile == ipc::ShuangpinProfile::Custom &&
                profile != settings_.profile) {
                continue;
            }
            if (profile == settings_.profile) {
                selected = static_cast<int>(profiles_.size());
            }
            profiles_.push_back(profile);
            profile_.Items().Append(
                box_value(profileNames[static_cast<unsigned>(profile)]));
        }
        profile_.SelectedIndex(selected);
        scheme_.SelectedIndex(
            settings_.scheme == ipc::PinyinScheme::Double ? 1 : 0);
        status_.Text(loaded_ ? L""
                             : errorText(ipc::SettingsError::Unavailable));
        retry_.Visibility(loaded_ ? Visibility::Collapsed
                                  : Visibility::Visible);
    }
    busy(false);
}
} // namespace fcitx
