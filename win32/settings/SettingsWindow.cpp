#include "SettingsWindow.h"
#include "../ipc/transport.h"
#include <commctrl.h>
#undef GetCurrentTime
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.Text.h>

namespace fcitx {
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace {
HRESULT openDictionariesDirectory() {
    const auto initialized = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized)) {
        return initialized;
    }
    struct Lifetime {
        ~Lifetime() { CoUninitialize(); }
    } lifetime;
    PWSTR roaming = nullptr;
    const auto result =
        SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming);
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> directory(
        roaming, &CoTaskMemFree);
    if (FAILED(result)) {
        return result;
    }
    const auto path = std::filesystem::path(directory.get()) / L"Fcitx5" /
                      L"pinyin" / L"dictionaries";
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        return HRESULT_FROM_WIN32(error.value());
    }
    SHELLEXECUTEINFOW execute{sizeof(execute)};
    execute.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    execute.lpVerb = L"open";
    execute.lpFile = path.c_str();
    execute.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&execute) ? S_OK
                                     : HRESULT_FROM_WIN32(GetLastError());
}

const wchar_t *dictionaryState(ipc::DictionaryStatus status) {
    switch (status) {
    case ipc::DictionaryStatus::Loaded:
        return L"\u5df2\u52a0\u8f7d";
    case ipc::DictionaryStatus::Failed:
        return L"\u52a0\u8f7d\u5931\u8d25";
    case ipc::DictionaryStatus::Disabled:
        return L"\u5df2\u505c\u7528";
    default:
        return L"\u52a0\u8f7d\u4e2d";
    }
}

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
        return L"\u8bbe\u7f6e\u5df2\u53d8\u66f4\uff0c\u8bf7\u91cd\u65b0\u52a0"
               L"\u8f7d\u540e\u518d\u4fee\u6539\u3002";
    case ipc::SettingsError::LastInputMethod:
        return L"\u81f3\u5c11\u4fdd\u7559\u4e00\u4e2a\u53ef\u7528\u8f93\u5165"
               L"\u6cd5\uff0c\u8bf7\u5148\u6dfb\u52a0\u65b0\u7684\u8f93\u5165"
               L"\u6cd5\u518d\u5220\u9664\u3002";
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
        bounds->ptMinTrackSize = {MulDiv(720, dpi, 96), MulDiv(480, dpi, 96)};
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
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Padding="20" RowSpacing="12" ColumnSpacing="24">
  <Grid.ColumnDefinitions><ColumnDefinition Width="220"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions>
  <Grid.RowDefinitions><RowDefinition Height="*"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <Grid RowSpacing="12">
    <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
    <TextBlock Text="&#x8f93;&#x5165;&#x6cd5;" FontSize="20" FontWeight="SemiBold"/>
    <ListView x:Name="Methods" Grid.Row="1" HorizontalContentAlignment="Stretch" AutomationProperties.AutomationId="InputMethods"/>
    <StackPanel Grid.Row="2" Orientation="Horizontal" Spacing="6">
      <Button x:Name="Add" ToolTipService.ToolTip="&#x6dfb;&#x52a0;&#x8f93;&#x5165;&#x6cd5;" AutomationProperties.AutomationId="AddInputMethod">
        <FontIcon Glyph="&#xE710;" FontSize="16"/>
        <Button.Flyout><Flyout><StackPanel Spacing="12" Width="260">
          <TextBlock Text="&#x6dfb;&#x52a0;&#x8f93;&#x5165;&#x6cd5;" FontWeight="SemiBold"/>
          <ComboBox x:Name="Available" HorizontalAlignment="Stretch" AutomationProperties.AutomationId="AvailableInputMethods"/>
          <Button x:Name="AddConfirm" Content="&#x6dfb;&#x52a0;" HorizontalAlignment="Right"/>
        </StackPanel></Flyout></Button.Flyout>
      </Button>
      <Button x:Name="Remove" ToolTipService.ToolTip="&#x5220;&#x9664;" AutomationProperties.AutomationId="RemoveInputMethod"><FontIcon Glyph="&#xE74D;" FontSize="16"/></Button>
      <Button x:Name="Up" ToolTipService.ToolTip="&#x4e0a;&#x79fb;" AutomationProperties.AutomationId="MoveInputMethodUp"><FontIcon Glyph="&#xE74A;" FontSize="16"/></Button>
      <Button x:Name="Down" ToolTipService.ToolTip="&#x4e0b;&#x79fb;" AutomationProperties.AutomationId="MoveInputMethodDown"><FontIcon Glyph="&#xE74B;" FontSize="16"/></Button>
    </StackPanel>
  </Grid>
  <Grid Grid.Column="1" RowSpacing="16">
    <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/></Grid.RowDefinitions>
    <TextBlock x:Name="Heading" FontSize="24" FontWeight="SemiBold" TextWrapping="Wrap"/>
  <TabView x:Name="Tabs" Grid.Row="1" IsAddTabButtonVisible="False" CanDragTabs="False" CanReorderTabs="False">
    <TabViewItem Header="&#x62fc;&#x97f3;" IsClosable="False">
      <ScrollViewer Margin="0,16,0,0" VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled">
        <StackPanel x:Name="Options" Spacing="8" Margin="0,0,12,0"/>
      </ScrollViewer>
    </TabViewItem>
    <TabViewItem x:Name="DictionaryTab" Header="&#x8bcd;&#x5e93;" IsClosable="False">
      <Grid Margin="0,16,0,0" RowSpacing="12">
        <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/></Grid.RowDefinitions>
        <Button x:Name="DictionaryFolder" AutomationProperties.AutomationId="DictionaryFolder">
          <StackPanel Orientation="Horizontal" Spacing="8">
            <FontIcon Glyph="&#xE8B7;" FontSize="16"/>
            <TextBlock Text="&#x6253;&#x5f00;&#x7b2c;&#x4e09;&#x65b9;&#x8bcd;&#x5e93;&#x6587;&#x4ef6;&#x5939;"/>
          </StackPanel>
        </Button>
        <TextBlock x:Name="DictionaryDirectory" Grid.Row="1" TextWrapping="Wrap" IsTextSelectionEnabled="True" FontSize="12"/>
        <TextBlock x:Name="DictionaryStatus" Grid.Row="2" TextWrapping="Wrap" AutomationProperties.LiveSetting="Polite"/>
        <ListView x:Name="Dictionaries" Grid.Row="3" SelectionMode="None" HorizontalContentAlignment="Stretch" AutomationProperties.AutomationId="Dictionaries"/>
      </Grid>
    </TabViewItem>
  </TabView>
  </Grid>
  <TextBlock x:Name="Status" Grid.Row="1" Grid.ColumnSpan="2" TextWrapping="Wrap" Visibility="Collapsed" AutomationProperties.LiveSetting="Polite"/>
  <Grid Grid.Row="2" Grid.ColumnSpan="2">
    <Button x:Name="ResetDefaults" Content="&#x91cd;&#x7f6e;&#x4e3a;&#x9ed8;&#x8ba4;&#x503c;"/>
    <StackPanel Orientation="Horizontal" HorizontalAlignment="Right" Spacing="8">
      <Button x:Name="Retry" Content="&#x91cd;&#x65b0;&#x52a0;&#x8f7d;" Visibility="Collapsed"/>
      <Button x:Name="Cancel" Content="&#x5173;&#x95ed;"/>
      <Button x:Name="Confirm" Content="&#x5e94;&#x7528;" Style="{StaticResource AccentButtonStyle}"/>
    </StackPanel>
  </Grid>
</Grid>)")
                    .as<FrameworkElement>();
    methods_ = root.FindName(L"Methods").as<ListView>();
    available_ = root.FindName(L"Available").as<ComboBox>();
    options_ = root.FindName(L"Options").as<StackPanel>();
    heading_ = root.FindName(L"Heading").as<TextBlock>();
    add_ = root.FindName(L"Add").as<Button>();
    remove_ = root.FindName(L"Remove").as<Button>();
    up_ = root.FindName(L"Up").as<Button>();
    down_ = root.FindName(L"Down").as<Button>();
    reset_ = root.FindName(L"ResetDefaults").as<Button>();
    dictionaryTab_ = root.FindName(L"DictionaryTab").as<TabViewItem>();
    methods_.SelectionChanged([this](auto &&, auto &&) {
        if (!rendering_ && !working_) {
            chooseMethod();
        }
    });
    root.FindName(L"AddConfirm").as<Button>().Click([this](auto &&, auto &&) {
        changeSelection(0);
    });
    remove_.Click([this](auto &&, auto &&) { changeSelection(1); });
    up_.Click([this](auto &&, auto &&) { changeSelection(2); });
    down_.Click([this](auto &&, auto &&) { changeSelection(3); });
    reset_.Click([this](auto &&, auto &&) {
        const auto previous = config_.options;
        for (auto &option : config_.options) {
            if (option.path == "ShuangpinProfile" &&
                option.defaultValue == "Custom") {
                continue;
            }
            option.value = option.defaultValue;
        }
        renderConfig();
        config_.options = previous;
    });
    status_ = root.FindName(L"Status").as<TextBlock>();
    confirm_ = root.FindName(L"Confirm").as<Button>();
    retry_ = root.FindName(L"Retry").as<Button>();
    tabs_ = root.FindName(L"Tabs").as<TabView>();
    dictionaries_ = root.FindName(L"Dictionaries").as<ListView>();
    dictionaryDirectory_ =
        root.FindName(L"DictionaryDirectory").as<TextBlock>();
    dictionaryStatus_ = root.FindName(L"DictionaryStatus").as<TextBlock>();
    dictionaryFolder_ = root.FindName(L"DictionaryFolder").as<Button>();
    tabs_.SelectionChanged([this](auto &&, auto &&) {
        if (tabs_.SelectedIndex() == 1) {
            refreshDictionaries();
        }
    });
    dictionaryFolder_.Click(
        [this](auto &&, auto &&) { openDictionaryFolder(); });
    root.FindName(L"Cancel").as<Button>().Click(
        [this](auto &&, auto &&) { window_.Close(); });
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
    window_.AppWindow().Resize({MulDiv(980, dpi, 96), MulDiv(720, dpi, 96)});
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
        if (++dictionaryTicks_ >= 10) {
            dictionaryTicks_ = 0;
            if (tabs_.SelectedIndex() == 1) {
                refreshDictionaries();
            }
        }
    });
    timer_.Start();
    window_.Activate();
    transfer(false);
    return true;
}

void SettingsWindow::busy(bool enabled) {
    working_ = enabled;
    methods_.IsEnabled(!enabled && loaded_);
    for (auto &editor : editors_) {
        editor.as<Control>().IsEnabled(!enabled && loaded_);
    }
    add_.IsEnabled(!enabled && loaded_ && !availableIds_.empty());
    remove_.IsEnabled(!enabled && loaded_ && methods_.SelectedIndex() >= 0);
    up_.IsEnabled(!enabled && loaded_ && methods_.SelectedIndex() > 0);
    down_.IsEnabled(!enabled && loaded_ && methods_.SelectedIndex() >= 0 &&
                    methods_.SelectedIndex() + 1 <
                        static_cast<int>(catalog_.selection.enabled.size()));
    reset_.IsEnabled(!enabled && loaded_ && !config_.options.empty());
    confirm_.IsEnabled(!enabled && loaded_);
    retry_.IsEnabled(!enabled);
}

fire_and_forget SettingsWindow::transfer(bool save) {
    const auto lifetime = shared_from_this();
    const apartment_context ui;
    if (working_) {
        co_return;
    }
    if (save) {
        busy(true);
        co_await saveConfig();
        busy(false);
        co_return;
    }
    busy(true);
    status_.Visibility(Visibility::Visible);
    status_.Text(L"\u6b63\u5728\u52a0\u8f7d\u2026");
    ipc::InputMethodsReply reply;
    bool connected = false;
    co_await resume_background();
    try {
        PipeClient client;
        connected = client.connect() && client.getInputMethods(reply);
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
        loaded_ = false;
        retry_.Visibility(Visibility::Visible);
    } else if (reply.error != ipc::SettingsError::None) {
        status_.Text(errorText(reply.error));
    } else {
        catalog_ = std::move(reply);
        loaded_ = true;
        renderMethods(config_.id);
        status_.Text(L"");
        retry_.Visibility(Visibility::Collapsed);
        if (methods_.SelectedIndex() >= 0) {
            co_await loadConfig(
                catalog_.selection.enabled.at(methods_.SelectedIndex()));
        }
    }
    status_.Visibility(status_.Text().empty() ? Visibility::Collapsed
                                              : Visibility::Visible);
    busy(false);
}

const ipc::InputMethodInfo *
SettingsWindow::method(const std::string &id) const {
    auto found = std::find_if(catalog_.methods.begin(), catalog_.methods.end(),
                              [&](const auto &item) { return item.id == id; });
    return found == catalog_.methods.end() ? nullptr : &*found;
}

void SettingsWindow::renderMethods(const std::string &selected) {
    rendering_ = true;
    methods_.Items().Clear();
    available_.Items().Clear();
    availableIds_.clear();
    int selectedIndex = 0;
    for (size_t i = 0; i < catalog_.selection.enabled.size(); ++i) {
        const auto &id = catalog_.selection.enabled[i];
        const auto *info = method(id);
        StackPanel row;
        row.Orientation(Orientation::Horizontal);
        row.Spacing(12);
        TextBlock icon, name;
        icon.Text(to_hstring(info ? info->iconLabel : "?"));
        icon.Width(24);
        icon.FontSize(20);
        name.Text(to_hstring(info ? info->name : id));
        name.TextTrimming(TextTrimming::CharacterEllipsis);
        name.MaxWidth(154);
        const auto iconPath =
            info ? std::filesystem::path(to_hstring(info->icon).c_str())
                 : std::filesystem::path{};
        if (iconPath.is_absolute() && std::filesystem::exists(iconPath)) {
            Grid iconHost;
            iconHost.Width(24);
            iconHost.Height(24);
            Image image;
            image.Width(24);
            image.Height(24);
            image.Visibility(Visibility::Collapsed);
            iconHost.Children().Append(icon);
            iconHost.Children().Append(image);
            row.Children().Append(iconHost);
            loadIcon(image, icon, info->icon);
        } else {
            row.Children().Append(icon);
        }
        row.Children().Append(name);
        row.Padding({0, 10, 0, 10});
        if (info && !info->available) {
            row.Opacity(0.5);
        }
        ListViewItem item;
        item.Content(row);
        Automation::AutomationProperties::SetName(
            item, to_hstring(info ? info->name : id));
        Automation::AutomationProperties::SetAutomationId(
            item, to_hstring("method:" + id));
        methods_.Items().Append(item);
        if (id == selected) {
            selectedIndex = static_cast<int>(i);
        }
    }
    for (const auto &info : catalog_.methods) {
        if (info.available &&
            std::find(catalog_.selection.enabled.begin(),
                      catalog_.selection.enabled.end(),
                      info.id) == catalog_.selection.enabled.end()) {
            availableIds_.push_back(info.id);
            ComboBoxItem item;
            item.Content(box_value(to_hstring(info.name)));
            Automation::AutomationProperties::SetAutomationId(
                item, to_hstring("available:" + info.id));
            available_.Items().Append(item);
        }
    }
    methods_.SelectedIndex(selectedIndex);
    available_.SelectedIndex(availableIds_.empty() ? -1 : 0);
    rendering_ = false;
}

fire_and_forget SettingsWindow::loadIcon(Image image, TextBlock fallback,
                                         std::string path) {
    const auto lifetime = shared_from_this();
    try {
        const auto file =
            co_await Windows::Storage::StorageFile::GetFileFromPathAsync(
                hstring(std::filesystem::path(to_hstring(path).c_str())
                            .make_preferred()
                            .wstring()));
        const auto stream =
            co_await file.OpenAsync(Windows::Storage::FileAccessMode::Read);
        Media::Imaging::BitmapImage bitmap;
        co_await bitmap.SetSourceAsync(stream);
        if (closed_) {
            co_return;
        }
        image.Source(bitmap);
        image.Visibility(Visibility::Visible);
        fallback.Visibility(Visibility::Collapsed);
    } catch (...) {
    }
}

void SettingsWindow::renderConfig() {
    options_.Children().Clear();
    editors_.clear();
    const auto *info = method(config_.id);
    heading_.Text(to_hstring(info ? info->name : config_.id));
    auto settingsTab = tabs_.TabItems().GetAt(0).as<TabViewItem>();
    settingsTab.Header(
        box_value(config_.panel == "pinyin"     ? L"\u62fc\u97f3\u8bbe\u7f6e"
                  : config_.panel == "table"    ? L"\u7801\u8868\u8bbe\u7f6e"
                  : config_.panel == "keyboard" ? L"\u952e\u76d8\u8bbe\u7f6e"
                                                : L"\u8bbe\u7f6e"));
    dictionaryTab_.Visibility(config_.panel == "pinyin"
                                  ? Visibility::Visible
                                  : Visibility::Collapsed);
    if (config_.panel != "pinyin") {
        tabs_.SelectedIndex(0);
    }
    if (config_.options.empty()) {
        TextBlock text;
        text.Text(config_.panel == "keyboard"
                      ? L"\u952e\u76d8\u5e03\u5c40\uff1aWindows"
                      : L"\u6682\u65e0\u53ef\u7528\u8bbe\u7f6e");
        text.TextWrapping(TextWrapping::Wrap);
        options_.Children().Append(text);
        return;
    }
    constexpr std::pair<const char *, const wchar_t *> labels[] = {
        {"ShuangpinProfile", L"\u53cc\u62fc\u952e\u4f4d"},
        {"PageSize", L"\u6bcf\u9875\u5019\u9009\u6570"},
        {"SpellEnabled", L"\u82f1\u6587\u5019\u9009"},
        {"SymbolsEnabled", L"\u7b26\u53f7\u5019\u9009"},
        {"ChaiziEnabled", L"\u62c6\u5b57\u5019\u9009"},
        {"ExtBEnabled", L"\u6269\u5c55\u6c49\u5b57"},
        {"StrokeCandidateEnabled", L"\u7b14\u753b\u5019\u9009"},
        {"Prediction", L"\u8054\u60f3"},
        {"PreeditMode", L"\u9884\u7f16\u8f91\u6a21\u5f0f"},
        {"PreeditCursorPositionAtBeginning",
         L"\u9884\u7f16\u8f91\u5149\u6807\u56fa\u5b9a\u5728\u5f00\u5934"},
        {"PinyinInPreedit",
         L"\u9884\u7f16\u8f91\u663e\u793a\u5b8c\u6574\u62fc\u97f3"},
        {"ShowShuangpinMode", L"\u663e\u793a\u53cc\u62fc\u6a21\u5f0f"},
        {"NG_GN", L"\u5e38\u89c1\u9519\u8bef"},
        {"LowerCaseMatchLetter", L"\u5c0f\u5199\u65f6\u5339\u914d\u5b57\u6bcd"},
        {"Inner", L"\u5185\u6a21\u7cca\u97f3\u8282 (xian -> xi'an)"},
        {"InnerShort", L"\u77ed\u62fc\u97f3\u7684\u5185\u6a21\u7cca\u97f3\u8282"
                       L" (qie -> qi'e)"},
        {"PartialFinal", L"\u5339\u914d\u4e0d\u5b8c\u6574\u7684\u5143\u97f3 (e "
                         L"-> en, eng, ei)"},
        {"PartialSp", L"\u8f93\u5165\u957f\u5ea6\u5927\u4e8e 4 "
                      L"\u65f6\u90e8\u5206\u53cc\u62fc\u5339\u914d"},
        {"AutoSelect", L"\u81ea\u52a8\u9009\u62e9\u5019\u9009"},
        {"AutoSelectLength",
         L"\u552f\u4e00\u5019\u9009\u81ea\u52a8\u9009\u62e9\u957f\u5ea6"},
        {"CommitAfterSelect", L"\u81ea\u52a8\u9009\u62e9\u540e\u63d0\u4ea4"},
        {"CommitWhenDeactivate", L"\u505c\u7528\u65f6\u63d0\u4ea4"},
        {"UseFullWidth", L"\u4f7f\u7528\u5168\u89d2"},
        {"Hint", L"\u663e\u793a\u7f16\u7801\u63d0\u793a"},
        {"Learning", L"\u8bcd\u9891\u5b66\u4e60"},
        {"UseContextRelatedOrder",
         L"\u6839\u636e\u4e0a\u4e0b\u6587\u6392\u5e8f"},
        {"UseSystemLanguageModel",
         L"\u4f7f\u7528\u7cfb\u7edf\u8bed\u8a00\u6a21\u578b"},
        {"OrderPolicy", L"\u6392\u5e8f\u65b9\u5f0f"},
        {"PrevPage", L"\u4e0a\u4e00\u9875"},
        {"NextPage", L"\u4e0b\u4e00\u9875"},
        {"PrevCandidate", L"\u4e0a\u4e00\u5019\u9009"},
        {"NextCandidate", L"\u4e0b\u4e00\u5019\u9009"},
        {"DefaultCandidate", L"\u9009\u62e9\u5f53\u524d\u5019\u9009"}};
    std::string group;
    for (const auto &option : config_.options) {
        const auto separator = option.path.find_last_of('/');
        const auto section = separator == std::string::npos
                                 ? std::string{}
                                 : option.path.substr(0, separator);
        const auto key = separator == std::string::npos
                             ? option.path
                             : option.path.substr(separator + 1);
        if (section != group || editors_.empty()) {
            TextBlock title;
            title.Text(section == "Fuzzy" ? L"\u6a21\u7cca\u62fc\u97f3"
                       : section.empty()  ? L"\u5e38\u89c4"
                                          : to_hstring(section));
            title.FontSize(18);
            title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            title.Margin({0, 12, 0, 4});
            options_.Children().Append(title);
            group = section;
        }
        Grid row;
        row.ColumnSpacing(20);
        row.Padding({0, 6, 0, 6});
        ColumnDefinition labelColumn, editorColumn;
        labelColumn.Width({1, GridUnitType::Star});
        editorColumn.Width({1, GridUnitType::Auto});
        row.ColumnDefinitions().Append(labelColumn);
        row.ColumnDefinitions().Append(editorColumn);
        TextBlock label;
        label.Text(to_hstring(option.label));
        for (const auto &item : labels) {
            if (key == item.first) {
                label.Text(item.second);
                break;
            }
        }
        label.TextWrapping(TextWrapping::Wrap);
        label.VerticalAlignment(VerticalAlignment::Center);
        FrameworkElement editor{nullptr};
        if (option.kind == ipc::ConfigKind::Boolean) {
            ToggleSwitch toggle;
            toggle.IsOn(option.value == "True");
            toggle.OnContent(box_value(L""));
            toggle.OffContent(box_value(L""));
            toggle.MinWidth(70);
            editor = toggle;
        } else if (option.kind == ipc::ConfigKind::Integer) {
            NumberBox number;
            number.Minimum(option.minimum);
            number.Maximum(option.maximum);
            number.Value(std::stod(option.value));
            number.SpinButtonPlacementMode(
                NumberBoxSpinButtonPlacementMode::Compact);
            number.Width(140);
            editor = number;
        } else if (option.kind == ipc::ConfigKind::Enum) {
            ComboBox combo;
            combo.Width(210);
            int selected = -1;
            for (size_t i = 0; i < option.choices.size(); ++i) {
                auto name = to_hstring(option.choices[i].label);
                if (key == "ShuangpinProfile") {
                    for (unsigned p = 0;
                         p <=
                         static_cast<unsigned>(ipc::ShuangpinProfile::Custom);
                         ++p) {
                        if (ipc::profileConfigValue(
                                static_cast<ipc::ShuangpinProfile>(p)) ==
                            option.choices[i].value) {
                            name = profileNames[p];
                        }
                    }
                }
                combo.Items().Append(box_value(name));
                if (option.choices[i].value == option.value) {
                    selected = static_cast<int>(i);
                }
            }
            combo.SelectedIndex(selected);
            editor = combo;
        } else {
            TextBox text;
            text.Text(to_hstring(option.value));
            text.Width(210);
            if (option.kind == ipc::ConfigKind::KeyList) {
                text.AcceptsReturn(true);
                text.TextWrapping(TextWrapping::Wrap);
                text.MaxHeight(100);
            }
            editor = text;
        }
        Automation::AutomationProperties::SetName(editor, label.Text());
        Automation::AutomationProperties::SetAutomationId(
            editor, to_hstring(option.path));
        Grid::SetColumn(editor, 1);
        editor.VerticalAlignment(VerticalAlignment::Center);
        row.Children().Append(label);
        row.Children().Append(editor);
        editors_.push_back(editor);
        options_.Children().Append(row);
    }
}

std::vector<ipc::ConfigValue> SettingsWindow::configValues() const {
    std::vector<ipc::ConfigValue> result;
    for (size_t i = 0; i < editors_.size(); ++i) {
        const auto &option = config_.options[i];
        std::string value;
        if (option.kind == ipc::ConfigKind::Boolean) {
            value = editors_[i].as<ToggleSwitch>().IsOn() ? "True" : "False";
        } else if (option.kind == ipc::ConfigKind::Integer) {
            const auto number = editors_[i].as<NumberBox>().Value();
            value = std::isfinite(number) && std::floor(number) == number &&
                            number >= INT32_MIN && number <= INT32_MAX
                        ? std::to_string(static_cast<int32_t>(number))
                        : "";
        } else if (option.kind == ipc::ConfigKind::Enum) {
            const auto index = editors_[i].as<ComboBox>().SelectedIndex();
            value = index >= 0 ? option.choices.at(index).value : "";
        } else {
            value = to_string(editors_[i].as<TextBox>().Text());
            if (option.kind == ipc::ConfigKind::KeyList) {
                std::string normalized;
                std::replace(value.begin(), value.end(), '\r', '\n');
                std::istringstream lines(value);
                std::string line;
                while (std::getline(lines, line)) {
                    if (line.empty()) {
                        continue;
                    }
                    if (!normalized.empty()) {
                        normalized += '\n';
                    }
                    normalized += line;
                }
                value = std::move(normalized);
            }
        }
        result.push_back({option.path, std::move(value)});
    }
    return result;
}

Windows::Foundation::IAsyncOperation<bool>
SettingsWindow::loadConfig(const std::string &entry) {
    // Copy before the first suspension; callers may pass a vector element.
    const auto id = entry;
    const apartment_context ui;
    ipc::MethodConfigReply reply;
    bool connected = false;
    co_await resume_background();
    try {
        PipeClient client;
        connected = client.connect() && client.getMethodConfig(id, reply);
    } catch (...) {
    }
    co_await ui;
    if (closed_) {
        co_return false;
    }
    if (!connected) {
        status_.Text(L"\u65e0\u6cd5\u8fde\u63a5 Core\u3002");
        status_.Visibility(Visibility::Visible);
        retry_.Visibility(Visibility::Visible);
        co_return false;
    }
    config_ = std::move(reply);
    renderConfig();
    if (config_.error != ipc::SettingsError::None) {
        status_.Text(errorText(config_.error));
        status_.Visibility(Visibility::Visible);
    }
    co_return config_.error == ipc::SettingsError::None;
}

Windows::Foundation::IAsyncOperation<bool> SettingsWindow::saveConfig() {
    const apartment_context ui;
    const auto values = configValues();
    if (config_.id.empty() || config_.error != ipc::SettingsError::None) {
        co_return false;
    }
    for (size_t i = 0; i < values.size(); ++i) {
        if (!ipc::validConfigValue(config_.options[i], values[i].value)) {
            status_.Text(L"\u8bf7\u68c0\u67e5\u8bbe\u7f6e\u503c\u3002");
            status_.Visibility(Visibility::Visible);
            co_return false;
        }
    }
    ipc::MethodConfigRequest request{config_.id, config_.revision, values};
    ipc::MethodConfigReply reply;
    bool connected = false;
    co_await resume_background();
    try {
        PipeClient client;
        connected = client.connect() && client.setMethodConfig(request, reply);
        if (!connected) {
            client.disconnect();
            connected =
                client.connect() && client.getMethodConfig(request.id, reply);
            if (connected) {
                for (const auto &value : values) {
                    auto found =
                        std::find_if(reply.options.begin(), reply.options.end(),
                                     [&](const auto &option) {
                                         return option.path == value.path &&
                                                option.value == value.value;
                                     });
                    if (found == reply.options.end()) {
                        reply.error = ipc::SettingsError::Conflict;
                        break;
                    }
                }
            }
        }
    } catch (...) {
        connected = false;
    }
    co_await ui;
    if (closed_) {
        co_return false;
    }
    if (!connected || reply.error != ipc::SettingsError::None) {
        status_.Text(connected ? errorText(reply.error)
                               : L"\u65e0\u6cd5\u8fde\u63a5 Core\u3002");
        status_.Visibility(Visibility::Visible);
        retry_.Visibility(Visibility::Visible);
        co_return false;
    }
    config_ = std::move(reply);
    catalog_.selection.revision = config_.revision;
    renderConfig();
    status_.Visibility(Visibility::Collapsed);
    co_return true;
}

fire_and_forget SettingsWindow::chooseMethod() {
    const auto lifetime = shared_from_this();
    const auto index = methods_.SelectedIndex();
    if (index < 0) {
        co_return;
    }
    const auto id = catalog_.selection.enabled.at(index);
    if (id == config_.id) {
        busy(false);
        co_return;
    }
    busy(true);
    const auto values = configValues();
    bool dirty = false;
    for (size_t i = 0; i < values.size(); ++i) {
        dirty = dirty || values[i].value != config_.options[i].value;
    }
    if (dirty) {
        ContentDialog dialog;
        dialog.XamlRoot(window_.Content().as<FrameworkElement>().XamlRoot());
        dialog.Title(box_value(L"\u4fdd\u5b58\u66f4\u6539\uff1f"));
        dialog.PrimaryButtonText(L"\u4fdd\u5b58");
        dialog.SecondaryButtonText(L"\u653e\u5f03");
        dialog.CloseButtonText(L"\u53d6\u6d88");
        const auto result = co_await dialog.ShowAsync();
        if (closed_) {
            co_return;
        }
        if (result == ContentDialogResult::None ||
            (result == ContentDialogResult::Primary &&
             !(co_await saveConfig()))) {
            renderMethods(config_.id);
            busy(false);
            co_return;
        }
    }
    if (!(co_await loadConfig(id))) {
        if (config_.id != id) {
            renderMethods(config_.id);
        }
    }
    busy(false);
}

fire_and_forget SettingsWindow::changeSelection(int action) {
    const auto lifetime = shared_from_this();
    if (working_ || !loaded_) {
        co_return;
    }
    auto next = catalog_.selection;
    const auto index = methods_.SelectedIndex();
    std::string selected = config_.id;
    if (action == 0) {
        if (available_.SelectedIndex() < 0) {
            co_return;
        }
        next.enabled.push_back(availableIds_.at(available_.SelectedIndex()));
        add_.Flyout().Hide();
    } else {
        if (index < 0) {
            co_return;
        }
        if (action == 1) {
            size_t remaining = 0;
            for (size_t i = 0; i < next.enabled.size(); ++i) {
                const auto *info = method(next.enabled[i]);
                if (static_cast<int>(i) != index && info && info->available) {
                    ++remaining;
                }
            }
            if (!remaining) {
                status_.Text(errorText(ipc::SettingsError::LastInputMethod));
                status_.Visibility(Visibility::Visible);
                co_return;
            }
            next.enabled.erase(next.enabled.begin() + index);
        } else {
            const auto target = index + (action == 2 ? -1 : 1);
            if (target < 0 || target >= static_cast<int>(next.enabled.size())) {
                co_return;
            }
            std::swap(next.enabled[index], next.enabled[target]);
        }
    }
    busy(true);
    // Save the current page before a list mutation changes the shared revision.
    if (!config_.options.empty() && !(co_await saveConfig())) {
        busy(false);
        co_return;
    }
    next.revision = catalog_.selection.revision;
    const apartment_context ui;
    ipc::InputMethodsReply reply;
    bool connected = false;
    co_await resume_background();
    try {
        PipeClient client;
        connected = client.connect() && client.setInputMethods(next, reply);
        if (!connected) {
            client.disconnect();
            connected = client.connect() && client.getInputMethods(reply);
            if (connected && reply.selection.enabled != next.enabled) {
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
    if (connected && reply.error == ipc::SettingsError::None) {
        catalog_ = std::move(reply);
        renderMethods(selected);
        co_await loadConfig(
            catalog_.selection.enabled.at(methods_.SelectedIndex()));
        status_.Visibility(Visibility::Collapsed);
    } else {
        status_.Text(connected ? errorText(reply.error)
                               : L"\u65e0\u6cd5\u8fde\u63a5 Core\u3002");
        status_.Visibility(Visibility::Visible);
        retry_.Visibility(Visibility::Visible);
    }
    busy(false);
}

fire_and_forget SettingsWindow::refreshDictionaries() {
    if (dictionaryRefresh_ || closed_) {
        co_return;
    }
    const auto lifetime = shared_from_this();
    const apartment_context ui;
    dictionaryRefresh_ = true;
    ipc::DictionariesReply reply;
    bool connected = false;
    co_await resume_background();
    try {
        PipeClient client;
        connected = client.connect() && client.getDictionaries(reply);
    } catch (...) {
    }
    co_await ui;
    dictionaryRefresh_ = false;
    if (closed_) {
        co_return;
    }
    if (!connected) {
        dictionaries_.Items().Clear();
        dictionaryReply_ = {};
        dictionaryStatus_.Text(L"\u65e0\u6cd5\u8fde\u63a5 Core\u3002");
        dictionaryStatus_.Visibility(Visibility::Visible);
        co_return;
    }
    dictionaryDirectory_.Text(to_hstring(reply.directory));
    dictionaryStatus_.Text(
        !reply.available ? L"\u62fc\u97f3\u8bcd\u5e93\u4e0d\u53ef\u7528\u3002"
        : !reply.error.empty()
            ? L"\u65e0\u6cd5\u8bfb\u53d6\u8bcd\u5e93\u76ee\u5f55\u3002"
        : reply.dictionaries.empty()
            ? L"\u6682\u65e0\u7b2c\u4e09\u65b9\u8bcd\u5e93"
            : L"");
    dictionaryStatus_.Visibility(dictionaryStatus_.Text().empty()
                                     ? Visibility::Collapsed
                                     : Visibility::Visible);
    if (reply != dictionaryReply_) {
        dictionaries_.Items().Clear();
        for (const auto &dictionary : reply.dictionaries) {
            Grid row;
            ColumnDefinition nameColumn, stateColumn;
            nameColumn.Width({1, GridUnitType::Star});
            stateColumn.Width({1, GridUnitType::Auto});
            row.ColumnDefinitions().Append(nameColumn);
            row.ColumnDefinitions().Append(stateColumn);
            row.ColumnSpacing(16);
            row.Padding({0, 8, 0, 8});
            TextBlock name, state;
            name.Text(to_hstring(dictionary.name));
            name.TextTrimming(TextTrimming::CharacterEllipsis);
            state.Text(dictionaryState(dictionary.status));
            Grid::SetColumn(state, 1);
            row.Children().Append(name);
            row.Children().Append(state);
            ToolTipService::SetToolTip(row,
                                       box_value(to_hstring(dictionary.path)));
            dictionaries_.Items().Append(row);
        }
        dictionaryReply_ = std::move(reply);
    }
}

fire_and_forget SettingsWindow::openDictionaryFolder() {
    const auto lifetime = shared_from_this();
    const apartment_context ui;
    dictionaryFolder_.IsEnabled(false);
    HRESULT result = E_FAIL;
    co_await resume_background();
    try {
        result = openDictionariesDirectory();
    } catch (...) {
    }
    co_await ui;
    if (!closed_) {
        dictionaryFolder_.IsEnabled(true);
        if (FAILED(result)) {
            dictionaryStatus_.Visibility(Visibility::Visible);
            dictionaryStatus_.Text(L"\u65e0\u6cd5\u6253\u5f00\u8bcd\u5e93\u6587"
                                   L"\u4ef6\u5939\u3002");
        }
    }
}
} // namespace fcitx
