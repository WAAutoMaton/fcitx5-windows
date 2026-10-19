#include "SettingsWindow.h"
#include "../ipc/diagnostic_log.h"
#include "../ipc/transport.h"
#include "diagnostics.h"
#include "keycapture.h"
#include <commctrl.h>
#undef GetCurrentTime
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
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
HRESULT openUserDirectory(REFKNOWNFOLDERID folder,
                          const std::filesystem::path &relative) {
    const auto initialized = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized)) {
        return initialized;
    }
    struct Lifetime {
        ~Lifetime() { CoUninitialize(); }
    } lifetime;
    PWSTR knownFolder = nullptr;
    const auto result = SHGetKnownFolderPath(folder, 0, nullptr, &knownFolder);
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> directory(
        knownFolder, &CoTaskMemFree);
    if (FAILED(result)) {
        return result;
    }
    const auto path =
        std::filesystem::path(directory.get()) / L"Fcitx5" / relative;
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

const char *dictionaryState(ipc::DictionaryStatus status) {
    switch (status) {
    case ipc::DictionaryStatus::Loaded:
        return win32::i18n::mark("settings", "Loaded");
    case ipc::DictionaryStatus::Failed:
        return win32::i18n::mark("settings", "Failed to load");
    case ipc::DictionaryStatus::Disabled:
        return win32::i18n::mark("settings", "Disabled");
    default:
        return win32::i18n::mark("settings", "Loading");
    }
}

const char *pluginState(ipc::PluginStatus status) {
    switch (status) {
    case ipc::PluginStatus::Loaded:
        return win32::i18n::mark("settings", "Loaded");
    case ipc::PluginStatus::Disabled:
        return win32::i18n::mark("settings", "Disabled");
    default:
        return win32::i18n::mark("settings", "Not loaded");
    }
}

constexpr const char *profileNames[] = {
    win32::i18n::mark("settings", "Ziranma"),
    win32::i18n::mark("settings", "Microsoft"),
    win32::i18n::mark("settings", "Ziguang"),
    win32::i18n::mark("settings", "Smart ABC"),
    win32::i18n::mark("settings", "Chinese Star"),
    win32::i18n::mark("settings", "Pinyin Jiajia"),
    win32::i18n::mark("settings", "Xiaohe"),
    win32::i18n::mark("settings", "GB Standard"),
    win32::i18n::mark("settings", "Existing custom profile")};

const char *settingsErrorText(ipc::SettingsError error) {
    switch (error) {
    case ipc::SettingsError::Conflict:
        return win32::i18n::mark(
            "settings", "Settings have changed. Reload before editing.");
    case ipc::SettingsError::LastInputMethod:
        return win32::i18n::mark("settings",
                                 "Keep at least one available input method. "
                                 "Add another before removing this one.");
    case ipc::SettingsError::SaveFailed:
        return win32::i18n::mark(
            "settings", "Cannot save settings. Check write permissions for the "
                        "user configuration directory.");
    case ipc::SettingsError::Unavailable:
        return win32::i18n::mark(
            "settings", "The input engine or input method is unavailable.");
    default:
        return win32::i18n::mark("settings",
                                 "Cannot apply settings. Please retry.");
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

class RecordingKeyboard {
  public:
    RecordingKeyboard(HWND owner,
                      std::function<void(const std::string &)> changed)
        : owner_(owner), changed_(std::move(changed)) {}
    ~RecordingKeyboard() { stop(); }
    bool start() {
        stop();
        capture_.reset();
        if (!GetKeyboardState(keyboard_))
            return false;
        hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, process,
                                  GetModuleHandleW(nullptr), 0);
        if (hook_)
            active_ = this;
        return hook_ != nullptr;
    }
    void stop() {
        if (hook_)
            UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
        if (active_ == this)
            active_ = nullptr;
    }

  private:
    static LRESULT CALLBACK process(int code, WPARAM message, LPARAM details) {
        auto *self = active_;
        if (code != HC_ACTION || !self || GetForegroundWindow() != self->owner_)
            return CallNextHookEx(nullptr, code, message, details);
        const auto &event = *reinterpret_cast<const KBDLLHOOKSTRUCT *>(details);
        const auto key = event.vkCode;
        auto *keyboard = self->keyboard_;
        const bool release = message == WM_KEYUP || message == WM_SYSKEYUP;
        if (key >= 256)
            return CallNextHookEx(nullptr, code, message, details);
        if (release)
            keyboard[key] &= ~0x80;
        else
            keyboard[key] |= 0x80;
        keyboard[VK_SHIFT] = (keyboard[VK_LSHIFT] | keyboard[VK_RSHIFT]) & 0x80;
        keyboard[VK_CONTROL] =
            (keyboard[VK_LCONTROL] | keyboard[VK_RCONTROL]) & 0x80;
        keyboard[VK_MENU] = (keyboard[VK_LMENU] | keyboard[VK_RMENU]) & 0x80;
        if (release) {
            self->capture_.release(key);
            return 1;
        }
        try {
            const auto down = [&](int value) {
                return (keyboard[value] & 0x80) != 0;
            };
            unsigned modifiers = 0;
            if (down(VK_SHIFT) || down(VK_LSHIFT) || down(VK_RSHIFT))
                modifiers |= settings::Shift;
            if (down(VK_CONTROL) || down(VK_LCONTROL) || down(VK_RCONTROL))
                modifiers |= settings::Control;
            if (down(VK_MENU) || down(VK_LMENU) || down(VK_RMENU))
                modifiers |= settings::Alt;
            if (down(VK_LWIN) || down(VK_RWIN))
                modifiers |= settings::Super;
            modifiers |= settings::modifierForKey(static_cast<UINT>(key));
            if (keyboard[VK_CAPITAL] & 1)
                modifiers |= settings::Caps;
            auto scan = event.scanCode;
            if (event.flags & LLKHF_EXTENDED)
                scan |= 0x100;
            std::string unicode;
            if (!(modifiers & (settings::Control | settings::Alt)) &&
                !settings::modifierForKey(static_cast<UINT>(key))) {
                WCHAR text[4]{};
                const auto count =
                    ToUnicodeEx(static_cast<UINT>(key), scan & 0xff, keyboard,
                                text, 4, 4, GetKeyboardLayout(0));
                if ((count == 1 && text[0] >= 0x20 && text[0] != 0x7f &&
                     !(text[0] >= 0xd800 && text[0] <= 0xdfff)) ||
                    (count == 2 && text[0] >= 0xd800 && text[0] <= 0xdbff &&
                     text[1] >= 0xdc00 && text[1] <= 0xdfff)) {
                    unicode =
                        to_string(hstring(text, static_cast<uint32_t>(count)));
                }
            }
            const auto value = self->capture_.press(static_cast<UINT>(key),
                                                    scan, modifiers, unicode);
            if (!value.empty())
                self->changed_(value);
        } catch (...) {
            // Exceptions must not escape the Windows hook callback.
        }
        return 1;
    }
    inline static thread_local RecordingKeyboard *active_ = nullptr;
    HWND owner_ = nullptr;
    HHOOK hook_ = nullptr;
    BYTE keyboard_[256]{};
    settings::KeyCapture capture_;
    std::function<void(const std::string &)> changed_;
};
} // namespace

std::wstring SettingsWindow::tr(const char *context,
                                const char *english) const {
    return translator_.wide(context, english);
}

std::wstring SettingsWindow::errorText(ipc::SettingsError error) const {
    if (error == ipc::SettingsError::Busy)
        return tr(
            "settings",
            "Settings are being updated by another session. Please retry.");
    return tr("settings", settingsErrorText(error));
}

void SettingsWindow::translateStatic() {
    const auto root = window_.Content().as<FrameworkElement>();
    window_.Title(tr("settings", "Input Method Settings"));
    root.FindName(L"MethodsHeading")
        .as<TextBlock>()
        .Text(tr("settings", "Input Methods"));
    root.FindName(L"AddHeading")
        .as<TextBlock>()
        .Text(tr("settings", "Add Input Method"));
    root.FindName(L"GlobalSettingsText")
        .as<TextBlock>()
        .Text(tr("settings", "Global Settings"));
    root.FindName(L"LanguageLabel")
        .as<TextBlock>()
        .Text(tr("settings", "Language"));
    root.FindName(L"GeneralTab")
        .as<TabViewItem>()
        .Header(box_value(tr("settings", "General")));
    root.FindName(L"PluginTab")
        .as<TabViewItem>()
        .Header(box_value(tr("settings", "Plugins")));
    root.FindName(L"OpenPluginFolderText")
        .as<TextBlock>()
        .Text(tr("settings", "Open Personal Plugin Folder"));
    Automation::AutomationProperties::SetName(
        pluginFolder_, tr("settings", "Open Personal Plugin Folder"));
    root.FindName(L"DeveloperTab")
        .as<TabViewItem>()
        .Header(box_value(tr("settings", "Developer options")));
    root.FindName(L"DetailedLoggingLabel")
        .as<TextBlock>()
        .Text(tr("settings", "Enable detailed logging"));
    root.FindName(L"LoggingPrivacy")
        .as<TextBlock>()
        .Text(tr("settings",
                 "Logs record connection, context, candidate counts "
                 "and layout metadata, never input text. Files are "
                 "kept here for up to 7 days, up to 512 MiB total."));
    root.FindName(L"LogFolderLabel")
        .as<TextBlock>()
        .Text(tr("settings", "Log folder"));
    root.FindName(L"OpenLogFolderText")
        .as<TextBlock>()
        .Text(tr("settings", "Open log folder"));
    Automation::AutomationProperties::SetName(
        detailedLogging_, tr("settings", "Enable detailed logging"));
    detailedLogging_.OnContent(box_value(tr("settings", "On")));
    detailedLogging_.OffContent(box_value(tr("settings", "Off")));
    Automation::AutomationProperties::SetName(
        logFolder_, tr("settings", "Open log folder"));
    Automation::AutomationProperties::SetName(language_,
                                              tr("settings", "Language"));
    Automation::AutomationProperties::SetName(
        globalButton_, tr("settings", "Global Settings"));
    const std::pair<const wchar_t *, const char *> buttons[] = {
        {L"AddConfirm", win32::i18n::mark("settings", "Add")},
        {L"ResetDefaults", win32::i18n::mark("settings", "Reset to Defaults")},
        {L"Retry", win32::i18n::mark("settings", "Reload")},
        {L"Cancel", win32::i18n::mark("settings", "Close")},
        {L"Confirm", win32::i18n::mark("settings", "Apply")}};
    for (const auto &[name, message] : buttons)
        root.FindName(name).as<Button>().Content(
            box_value(tr("settings", message)));
    const std::pair<Button, const char *> tools[] = {
        {add_, win32::i18n::mark("settings", "Add Input Method")},
        {remove_, win32::i18n::mark("settings", "Remove")},
        {up_, win32::i18n::mark("settings", "Move Up")},
        {down_, win32::i18n::mark("settings", "Move Down")}};
    for (const auto &[button, message] : tools) {
        ToolTipService::SetToolTip(button, box_value(tr("settings", message)));
        Automation::AutomationProperties::SetName(button,
                                                  tr("settings", message));
    }
    dictionaryTab_.Header(box_value(tr("settings", "Dictionaries")));
    dictionaryFolder_.Content()
        .as<StackPanel>()
        .Children()
        .GetAt(1)
        .as<TextBlock>()
        .Text(tr("settings", "Open Third-party Dictionary Folder"));
    language_.Items().Clear();
    language_.Items().Append(
        box_value(tr("settings", "Follow System Language")));
    language_.Items().Append(box_value(L"\u7b80\u4f53\u4e2d\u6587"));
    language_.Items().Append(box_value(L"\u7e41\u9ad4\u4e2d\u6587"));
    language_.Items().Append(box_value(L"English"));
    language_.SelectedIndex(static_cast<int>(global_.settings.language));
}

void SettingsWindow::renderGlobal() {
    const settings::diagnostic::Operation operation("render_global");
    operation.stage("selection");
    page_ = PageKind::Global;
    rendering_ = true;
    methods_.SelectedIndex(-1);
    globalButton_.IsChecked(true);
    rendering_ = false;
    heading_.Text(tr("settings", "Global Settings"));
    operation.stage("page_visibility");
    tabs_.Visibility(Visibility::Collapsed);
    globalPanel_.Visibility(Visibility::Visible);
    operation.stage("controls");
    language_.SelectedIndex(static_cast<int>(global_.settings.language));
    detailedLogging_.IsOn(global_.settings.detailedLogging);
    win32::diagnostic::settingsLogger().setEnabled(
        global_.settings.detailedLogging);
    logStatus_.Text(
        tr("settings", global_.settings.detailedLogging ? "Starting" : "Off"));
    operation.stage("log_status");
    refreshLogStatus();
    if (globalPanel_.SelectedIndex() == 2)
        refreshPlugins();
}

Windows::Foundation::IAsyncOperation<bool> SettingsWindow::loadGlobal() {
    const settings::diagnostic::Operation operation("load_global");
    const apartment_context ui;
    ipc::GlobalSettingsReply reply;
    bool connected = false;
    co_await resume_background();
    try {
        PipeClient client({}, {}, &win32::diagnostic::settingsLogger());
        connected = client.connect() && client.getGlobalSettings(reply);
    } catch (...) {
        settings::diagnostic::exception("load_global");
    }
    co_await ui;
    operation.stage(closed_ ? "closed" : "apply_reply");
    if (closed_)
        co_return false;
    if (!connected || reply.error != ipc::SettingsError::None) {
        status_.Text(connected ? errorText(reply.error)
                               : tr("settings", "Cannot connect to Core."));
        status_.Visibility(Visibility::Visible);
        retry_.Visibility(Visibility::Visible);
        co_return false;
    }
    global_ = reply;
    renderGlobal();
    status_.Visibility(Visibility::Collapsed);
    co_return true;
}

Windows::Foundation::IAsyncOperation<bool> SettingsWindow::saveGlobal() {
    const settings::diagnostic::Operation operation("save_global");
    const auto index = language_.SelectedIndex();
    if (index < 0 || index > 3)
        co_return false;
    const ipc::GlobalSettings request{static_cast<ipc::UiLanguage>(index),
                                      global_.settings.revision,
                                      detailedLogging_.IsOn()};
    const apartment_context ui;
    ipc::GlobalSettingsReply reply;
    bool connected = false;
    co_await resume_background();
    try {
        PipeClient client({}, {}, &win32::diagnostic::settingsLogger());
        connected =
            client.connect() && client.setGlobalSettings(request, reply);
        if (!connected) {
            client.disconnect();
            connected = client.connect() && client.getGlobalSettings(reply);
            if (connected && reply.error == ipc::SettingsError::None &&
                (reply.settings.language != request.language ||
                 reply.settings.detailedLogging != request.detailedLogging))
                reply.error = ipc::SettingsError::Conflict;
        }
    } catch (...) {
        settings::diagnostic::exception("save_global");
    }
    co_await ui;
    if (closed_)
        co_return false;
    if (!connected || reply.error != ipc::SettingsError::None) {
        status_.Text(connected ? errorText(reply.error)
                               : tr("settings", "Cannot connect to Core."));
        status_.Visibility(Visibility::Visible);
        retry_.Visibility(Visibility::Visible);
        co_return false;
    }
    global_ = reply;
    win32::diagnostic::settingsLogger().setEnabled(
        global_.settings.detailedLogging);
    win32::diagnostic::settingsLogger().event(
        win32::diagnostic::Level::Info, "global_settings_saved",
        {{"revision", global_.settings.revision}});
    renderGlobal();
    const bool pending = reply.resolvedLanguage != reply.coreActiveLanguage;
    status_.Text(
        pending
            ? tr("settings",
                 "Language saved. Restart services to apply it to Settings.")
            : L"");
    status_.Visibility(pending ? Visibility::Visible : Visibility::Collapsed);
    retry_.Visibility(Visibility::Collapsed);
    co_return true;
}

Windows::Foundation::IAsyncOperation<bool> SettingsWindow::resolveDraft() {
    bool dirty = false;
    if (page_ == PageKind::Global) {
        dirty = language_.SelectedIndex() !=
                    static_cast<int>(global_.settings.language) ||
                detailedLogging_.IsOn() != global_.settings.detailedLogging;
    } else {
        const auto values = configValues();
        for (size_t i = 0; i < values.size(); ++i)
            dirty = dirty || values[i].value != config_.options[i].value;
    }
    if (!dirty)
        co_return true;
    ContentDialog dialog;
    dialog.XamlRoot(window_.Content().as<FrameworkElement>().XamlRoot());
    dialog.Title(box_value(tr("settings", "Save changes?")));
    dialog.PrimaryButtonText(tr("settings", "Save"));
    dialog.SecondaryButtonText(tr("settings", "Discard"));
    dialog.CloseButtonText(tr("settings", "Cancel"));
    const auto result = co_await dialog.ShowAsync();
    if (closed_ || result == ContentDialogResult::None)
        co_return false;
    if (result == ContentDialogResult::Primary) {
        if (page_ == PageKind::Global)
            co_return co_await saveGlobal();
        co_return co_await saveConfig();
    }
    co_return true;
}

fire_and_forget SettingsWindow::chooseGlobal() try {
    const settings::diagnostic::Operation operation("choose_global");
    const auto lifetime = shared_from_this();
    if (working_ || !loaded_ || page_ == PageKind::Global) {
        globalButton_.IsChecked(page_ == PageKind::Global);
        co_return;
    }
    busy(true);
    if (!(co_await resolveDraft()) || !(co_await loadGlobal()))
        globalButton_.IsChecked(false);
    busy(false);
} catch (...) {
    settings::diagnostic::exception("choose_global", true);
    throw;
}

SettingsWindow::~SettingsWindow() {
    if (activation_) {
        CloseHandle(activation_);
    }
}

bool SettingsWindow::open() {
    const settings::diagnostic::Operation operation("window_open");
    const auto name = ipc::settingsActivationBase();
    activation_ =
        CreateEventW(nullptr, FALSE, FALSE, (name + L"-activate").c_str());
    if (!activation_) {
        throw_last_error();
    }
    activationProperty_ = name + L"-window";
    window_ = Window();
    window_.Title(tr("settings", "Input Method Settings"));
    auto root = Markup::XamlReader::Load(LR"(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Padding="20" RowSpacing="12" ColumnSpacing="24">
  <Grid.ColumnDefinitions><ColumnDefinition Width="220"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions>
  <Grid.RowDefinitions><RowDefinition Height="*"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <Grid RowSpacing="12">
    <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
    <TextBlock x:Name="MethodsHeading" Text="Input Methods" FontSize="20" FontWeight="SemiBold"/>
    <ListView x:Name="Methods" Grid.Row="1" HorizontalContentAlignment="Stretch" AutomationProperties.AutomationId="InputMethods"/>
    <StackPanel Grid.Row="2" Orientation="Horizontal" Spacing="6">
      <Button x:Name="Add" ToolTipService.ToolTip="Add Input Method" AutomationProperties.AutomationId="AddInputMethod">
        <FontIcon Glyph="&#xE710;" FontSize="16"/>
        <Button.Flyout><Flyout><StackPanel Spacing="12" Width="260">
          <TextBlock x:Name="AddHeading" Text="Add Input Method" FontWeight="SemiBold"/>
          <ComboBox x:Name="Available" HorizontalAlignment="Stretch" AutomationProperties.AutomationId="AvailableInputMethods"/>
          <Button x:Name="AddConfirm" Content="Add" HorizontalAlignment="Right"/>
        </StackPanel></Flyout></Button.Flyout>
      </Button>
      <Button x:Name="Remove" ToolTipService.ToolTip="Remove" AutomationProperties.AutomationId="RemoveInputMethod"><FontIcon Glyph="&#xE74D;" FontSize="16"/></Button>
      <Button x:Name="Up" ToolTipService.ToolTip="Move Up" AutomationProperties.AutomationId="MoveInputMethodUp"><FontIcon Glyph="&#xE74A;" FontSize="16"/></Button>
      <Button x:Name="Down" ToolTipService.ToolTip="Move Down" AutomationProperties.AutomationId="MoveInputMethodDown"><FontIcon Glyph="&#xE74B;" FontSize="16"/></Button>
    </StackPanel>
    <ToggleButton x:Name="GlobalSettings" Grid.Row="3" HorizontalAlignment="Stretch" HorizontalContentAlignment="Left" AutomationProperties.AutomationId="GlobalSettings">
      <StackPanel Orientation="Horizontal" Spacing="10"><FontIcon Glyph="&#xE713;" FontSize="16"/><TextBlock x:Name="GlobalSettingsText" Text="Global Settings"/></StackPanel>
    </ToggleButton>
  </Grid>
  <Grid Grid.Column="1" RowSpacing="16">
    <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/></Grid.RowDefinitions>
    <TextBlock x:Name="Heading" FontSize="24" FontWeight="SemiBold" TextWrapping="Wrap"/>
  <TabView x:Name="Tabs" Grid.Row="1" IsAddTabButtonVisible="False" CanDragTabs="False" CanReorderTabs="False">
    <TabViewItem Header="Pinyin" IsClosable="False">
      <ScrollViewer Margin="0,16,0,0" VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled">
        <StackPanel x:Name="Options" Spacing="8" Margin="0,0,12,0"/>
      </ScrollViewer>
    </TabViewItem>
    <TabViewItem x:Name="DictionaryTab" Header="Dictionaries" IsClosable="False">
      <Grid Margin="0,16,0,0" RowSpacing="12">
        <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/></Grid.RowDefinitions>
        <Button x:Name="DictionaryFolder" AutomationProperties.AutomationId="DictionaryFolder">
          <StackPanel Orientation="Horizontal" Spacing="8">
            <FontIcon Glyph="&#xE8B7;" FontSize="16"/>
            <TextBlock Text="Open Third-party Dictionary Folder"/>
          </StackPanel>
        </Button>
        <TextBlock x:Name="DictionaryDirectory" Grid.Row="1" TextWrapping="Wrap" IsTextSelectionEnabled="True" FontSize="12"/>
        <TextBlock x:Name="DictionaryStatus" Grid.Row="2" TextWrapping="Wrap" AutomationProperties.LiveSetting="Polite"/>
        <ListView x:Name="Dictionaries" Grid.Row="3" SelectionMode="None" HorizontalContentAlignment="Stretch" AutomationProperties.AutomationId="Dictionaries"/>
      </Grid>
    </TabViewItem>
  </TabView>
  <TabView x:Name="GlobalPanel" Grid.Row="1" Visibility="Collapsed" IsAddTabButtonVisible="False" CanDragTabs="False" CanReorderTabs="False">
    <TabViewItem x:Name="GeneralTab" Header="General" IsClosable="False" AutomationProperties.AutomationId="GeneralOptions">
    <Grid ColumnSpacing="20">
      <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
      <TextBlock x:Name="LanguageLabel" Text="Language" VerticalAlignment="Center" TextWrapping="Wrap"/>
      <ComboBox x:Name="UiLanguage" Grid.Column="1" Width="230" AutomationProperties.AutomationId="UiLanguage"/>
    </Grid>
    </TabViewItem>
    <TabViewItem x:Name="DeveloperTab" Header="Developer options" IsClosable="False" AutomationProperties.AutomationId="DeveloperOptions">
    <ScrollViewer VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled">
    <StackPanel Spacing="12" Margin="0,16,12,0">
    <Grid ColumnSpacing="20">
      <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
      <TextBlock x:Name="DetailedLoggingLabel" Text="Enable detailed logging" VerticalAlignment="Center" TextWrapping="Wrap"/>
      <ToggleSwitch x:Name="DetailedLogging" Grid.Column="1" Width="132" AutomationProperties.AutomationId="DetailedLogging"/>
    </Grid>
    <TextBlock x:Name="LoggingPrivacy" TextWrapping="Wrap" FontSize="12"/>
    <TextBlock x:Name="LogFolderLabel" Text="Log folder" FontWeight="SemiBold"/>
    <TextBlock x:Name="LogDirectory" TextWrapping="Wrap" IsTextSelectionEnabled="True" FontSize="12"/>
    <TextBlock x:Name="LogStatus" TextWrapping="Wrap" AutomationProperties.AutomationId="LogStatus" AutomationProperties.LiveSetting="Polite"/>
    <TextBlock x:Name="LogStatistics" TextWrapping="Wrap" AutomationProperties.AutomationId="LogStatistics"/>
    <Button x:Name="LogFolder" HorizontalAlignment="Left" AutomationProperties.AutomationId="OpenLogFolder">
      <StackPanel Orientation="Horizontal" Spacing="8"><FontIcon Glyph="&#xE8B7;" FontSize="16"/><TextBlock x:Name="OpenLogFolderText"/></StackPanel>
    </Button>
    </StackPanel>
    </ScrollViewer>
    </TabViewItem>
    <TabViewItem x:Name="PluginTab" Header="Plugins" IsClosable="False" AutomationProperties.AutomationId="PluginOptions">
      <Grid Margin="0,16,0,0" RowSpacing="12">
        <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/></Grid.RowDefinitions>
        <Button x:Name="PluginFolder" HorizontalAlignment="Left" AutomationProperties.AutomationId="PluginFolder">
          <StackPanel Orientation="Horizontal" Spacing="8">
            <FontIcon Glyph="&#xE8B7;" FontSize="16"/>
            <TextBlock x:Name="OpenPluginFolderText" TextWrapping="Wrap"/>
          </StackPanel>
        </Button>
        <TextBlock x:Name="PluginDirectory" Grid.Row="1" TextWrapping="Wrap" IsTextSelectionEnabled="True" FontSize="12" AutomationProperties.AutomationId="PluginDirectory"/>
        <TextBlock x:Name="PluginStatus" Grid.Row="2" TextWrapping="Wrap" AutomationProperties.AutomationId="PluginStatus" AutomationProperties.LiveSetting="Polite"/>
        <ListView x:Name="Plugins" Grid.Row="3" SelectionMode="None" HorizontalContentAlignment="Stretch" AutomationProperties.AutomationId="Plugins"/>
      </Grid>
    </TabViewItem>
  </TabView>
  </Grid>
  <TextBlock x:Name="Status" Grid.Row="1" Grid.ColumnSpan="2" TextWrapping="Wrap" Visibility="Collapsed" AutomationProperties.LiveSetting="Polite"/>
  <Grid Grid.Row="2" Grid.ColumnSpan="2">
    <Button x:Name="ResetDefaults" Content="Reset to Defaults"/>
    <StackPanel Orientation="Horizontal" HorizontalAlignment="Right" Spacing="8">
      <Button x:Name="Retry" Content="Reload" Visibility="Collapsed"/>
      <Button x:Name="Cancel" Content="Close"/>
      <Button x:Name="Confirm" Content="Apply" Style="{StaticResource AccentButtonStyle}"/>
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
    globalButton_ =
        root.FindName(L"GlobalSettings").as<Primitives::ToggleButton>();
    globalPanel_ = root.FindName(L"GlobalPanel").as<TabView>();
    language_ = root.FindName(L"UiLanguage").as<ComboBox>();
    detailedLogging_ = root.FindName(L"DetailedLogging").as<ToggleSwitch>();
    logDirectory_ = root.FindName(L"LogDirectory").as<TextBlock>();
    logStatus_ = root.FindName(L"LogStatus").as<TextBlock>();
    logFolder_ = root.FindName(L"LogFolder").as<Button>();
    plugins_ = root.FindName(L"Plugins").as<ListView>();
    pluginDirectory_ = root.FindName(L"PluginDirectory").as<TextBlock>();
    pluginStatus_ = root.FindName(L"PluginStatus").as<TextBlock>();
    pluginFolder_ = root.FindName(L"PluginFolder").as<Button>();
    pluginFolder_.Click([this](auto &&, auto &&) { openPluginFolder(); });
    globalButton_.Click([this](auto &&, auto &&) { chooseGlobal(); });
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
        if (page_ == PageKind::Global) {
            language_.SelectedIndex(0);
            detailedLogging_.IsOn(false);
            return;
        }
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
        win32::diagnostic::settingsLogger().event(
            win32::diagnostic::Level::Debug, "settings_tab_selected",
            {{"page", "input_method"}, {"index", tabs_.SelectedIndex()}});
        if (page_ == PageKind::InputMethod && tabs_.SelectedIndex() == 1) {
            refreshDictionaries();
        }
    });
    dictionaryFolder_.Click(
        [this](auto &&, auto &&) { openDictionaryFolder(); });
    globalPanel_.SelectionChanged([this](auto &&, auto &&) {
        win32::diagnostic::settingsLogger().event(
            win32::diagnostic::Level::Debug, "settings_tab_selected",
            {{"page", "global"}, {"index", globalPanel_.SelectedIndex()}});
        if (page_ == PageKind::Global && globalPanel_.SelectedIndex() == 2)
            refreshPlugins();
    });
    logFolder_.Click([this](auto &&, auto &&) { refreshLogStatus(true); });
    root.FindName(L"Cancel").as<Button>().Click([this](auto &&, auto &&) {
        win32::diagnostic::settingsLogger().event(
            win32::diagnostic::Level::Info, "settings_close_requested",
            {{"reason", "close_button"}});
        window_.Close();
    });
    confirm_.Click([this](auto &&, auto &&) { transfer(true); });
    retry_.Click([this](auto &&, auto &&) { transfer(false); });
    window_.Closed([this](auto &&, auto &&) {
        win32::diagnostic::settingsLogger().event(
            win32::diagnostic::Level::Info, "settings_window_closed",
            {{"page", page_ == PageKind::Global ? "global" : "input_method"},
             {"working", working_},
             {"log_refresh", logRefresh_},
             {"dictionary_refresh", dictionaryRefresh_}});
        closed_ = true;
        timer_.Stop();
        RemovePropW(handle_, activationProperty_.c_str());
    });
    window_.Content(root);
    translateStatic();
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
            win32::diagnostic::settingsLogger().event(
                win32::diagnostic::Level::Info, "settings_close_requested",
                {{"reason", "service_stop"}});
            window_.Close();
            return;
        }
        if (WaitForSingleObject(activation_, 0) == WAIT_OBJECT_0) {
            ShowWindow(handle_, IsIconic(handle_) ? SW_RESTORE : SW_SHOW);
            window_.Activate();
        }
        if (++dictionaryTicks_ >= 10) {
            dictionaryTicks_ = 0;
            if (page_ == PageKind::InputMethod && tabs_.SelectedIndex() == 1) {
                refreshDictionaries();
            }
            if (page_ == PageKind::Global && globalPanel_.SelectedIndex() == 1)
                refreshLogStatus();
            if (page_ == PageKind::Global && globalPanel_.SelectedIndex() == 2)
                refreshPlugins();
        }
    });
    timer_.Start();
    window_.Activate();
    win32::diagnostic::settingsLogger().event(win32::diagnostic::Level::Info,
                                              "settings_window_opened");
    transfer(false);
    return true;
}

void SettingsWindow::busy(bool enabled) {
    working_ = enabled;
    methods_.IsEnabled(!enabled && loaded_);
    globalButton_.IsEnabled(!enabled && loaded_);
    language_.IsEnabled(!enabled && loaded_);
    detailedLogging_.IsEnabled(!enabled && loaded_);
    for (auto &editor : editors_) {
        editor.as<Control>().IsEnabled(!enabled && loaded_);
    }
    add_.IsEnabled(!enabled && loaded_ && !availableIds_.empty());
    remove_.IsEnabled(!enabled && loaded_ && methods_.SelectedIndex() >= 0);
    up_.IsEnabled(!enabled && loaded_ && methods_.SelectedIndex() > 0);
    down_.IsEnabled(!enabled && loaded_ && methods_.SelectedIndex() >= 0 &&
                    methods_.SelectedIndex() + 1 <
                        static_cast<int>(catalog_.selection.enabled.size()));
    reset_.IsEnabled(!enabled && loaded_ &&
                     (page_ == PageKind::Global || !config_.options.empty()));
    confirm_.IsEnabled(!enabled && loaded_);
    retry_.IsEnabled(!enabled);
}

fire_and_forget SettingsWindow::transfer(bool save) try {
    const settings::diagnostic::Operation operation(save ? "apply" : "reload");
    const auto lifetime = shared_from_this();
    const apartment_context ui;
    if (working_) {
        co_return;
    }
    if (save) {
        busy(true);
        if (page_ == PageKind::Global)
            co_await saveGlobal();
        else
            co_await saveConfig();
        busy(false);
        co_return;
    }
    busy(true);
    status_.Visibility(Visibility::Visible);
    status_.Text(tr("settings", "Loading..."));
    ipc::InputMethodsReply reply;
    ipc::GlobalSettingsReply globalReply;
    std::unique_ptr<win32::i18n::Translator> translation;
    bool connected = false;
    co_await resume_background();
    try {
        PipeClient client({}, {}, &win32::diagnostic::settingsLogger());
        connected = client.connect() && client.getGlobalSettings(globalReply) &&
                    client.getInputMethods(reply);
        if (connected && !languageInitialized_) {
            translation = std::make_unique<win32::i18n::Translator>(
                globalReply.coreActiveLanguage);
        }
    } catch (...) {
        settings::diagnostic::exception("reload");
        connected = false;
    }
    co_await ui;
    operation.stage(closed_ ? "closed" : "apply_reply");
    if (closed_) {
        co_return;
    }
    if (!connected) {
        status_.Text(
            tr("settings",
               "Cannot connect to Core. Check that it is running and retry."));
        loaded_ = false;
        retry_.Visibility(Visibility::Visible);
    } else if (reply.error != ipc::SettingsError::None ||
               globalReply.error != ipc::SettingsError::None) {
        status_.Text(errorText(globalReply.error != ipc::SettingsError::None
                                   ? globalReply.error
                                   : reply.error));
        loaded_ = false;
        retry_.Visibility(Visibility::Visible);
    } else {
        if (translation) {
            translator_ = std::move(*translation);
            languageInitialized_ = true;
            translateStatic();
        }
        global_ = globalReply;
        catalog_ = std::move(reply);
        loaded_ = true;
        renderMethods(config_.id);
        status_.Text(L"");
        retry_.Visibility(Visibility::Collapsed);
        if (page_ == PageKind::Global) {
            renderGlobal();
        } else if (methods_.SelectedIndex() >= 0) {
            co_await loadConfig(
                catalog_.selection.enabled.at(methods_.SelectedIndex()));
        }
    }
    status_.Visibility(status_.Text().empty() ? Visibility::Collapsed
                                              : Visibility::Visible);
    busy(false);
} catch (...) {
    settings::diagnostic::exception("transfer", true);
    throw;
}

const ipc::InputMethodInfo *
SettingsWindow::method(const std::string &id) const {
    auto found = std::find_if(catalog_.methods.begin(), catalog_.methods.end(),
                              [&](const auto &item) { return item.id == id; });
    return found == catalog_.methods.end() ? nullptr : &*found;
}

void SettingsWindow::renderMethods(const std::string &selected) {
    const settings::diagnostic::Operation operation("render_methods");
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
    methods_.SelectedIndex(page_ == PageKind::Global ? -1 : selectedIndex);
    available_.SelectedIndex(availableIds_.empty() ? -1 : 0);
    rendering_ = false;
}

fire_and_forget SettingsWindow::loadIcon(Image image, TextBlock fallback,
                                         std::string path) try {
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
        settings::diagnostic::exception("load_icon");
    }
} catch (...) {
    settings::diagnostic::exception("load_icon", true);
    throw;
}

void SettingsWindow::renderConfig() {
    const settings::diagnostic::Operation operation("render_config");
    operation.stage("page_visibility");
    page_ = PageKind::InputMethod;
    globalButton_.IsChecked(false);
    globalPanel_.Visibility(Visibility::Collapsed);
    tabs_.Visibility(Visibility::Visible);
    options_.Children().Clear();
    operation.stage("controls");
    editors_.clear();
    const auto *info = method(config_.id);
    heading_.Text(to_hstring(info ? info->name : config_.id));
    auto settingsTab = tabs_.TabItems().GetAt(0).as<TabViewItem>();
    settingsTab.Header(box_value(
        config_.panel == "pinyin"     ? tr("settings", "Pinyin Settings")
        : config_.panel == "table"    ? tr("settings", "Table Settings")
        : config_.panel == "keyboard" ? tr("settings", "Keyboard Settings")
                                      : tr("settings", "Settings")));
    dictionaryTab_.Visibility(config_.panel == "pinyin"
                                  ? Visibility::Visible
                                  : Visibility::Collapsed);
    if (config_.panel != "pinyin") {
        tabs_.SelectedIndex(0);
    }
    if (config_.options.empty()) {
        TextBlock text;
        text.Text(config_.panel == "keyboard"
                      ? tr("settings", "Keyboard layout: Windows")
                      : tr("settings", "No settings available"));
        text.TextWrapping(TextWrapping::Wrap);
        options_.Children().Append(text);
        return;
    }
    const std::pair<const char *, std::wstring> labels[] = {
        {"ShuangpinProfile", tr("settings", "Shuangpin Profile")},
        {"PageSize", tr("settings", "Candidates Per Page")},
        {"SpellEnabled", tr("settings", "English Candidates")},
        {"SymbolsEnabled", tr("settings", "Symbol Candidates")},
        {"ChaiziEnabled", tr("settings", "Character Decomposition Candidates")},
        {"ExtBEnabled", tr("settings", "Extended Chinese Characters")},
        {"StrokeCandidateEnabled", tr("settings", "Stroke Candidates")},
        {"Prediction", tr("settings", "Prediction")},
        {"PreeditMode", tr("settings", "Preedit Mode")},
        {"PreeditCursorPositionAtBeginning",
         tr("settings", "Keep Preedit Cursor at Beginning")},
        {"PinyinInPreedit", tr("settings", "Show Full Pinyin in Preedit")},
        {"ShowShuangpinMode", tr("settings", "Show Shuangpin Mode")},
        {"NG_GN", tr("settings", "Common Mistakes")},
        {"LowerCaseMatchLetter", tr("settings", "Match Letters in Lowercase")},
        {"Inner", tr("settings", "Inner Fuzzy Syllables (xian -> xi'an)")},
        {"InnerShort",
         tr("settings",
            "Inner Fuzzy Syllables for Short Pinyin (qie -> qi'e)")},
        {"PartialFinal",
         tr("settings", "Match Incomplete Finals (e -> en, eng, ei)")},
        {"PartialSp",
         tr("settings", "Partial Shuangpin Matching for Input Longer Than 4")},
        {"AutoSelect", tr("settings", "Automatically Select Candidates")},
        {"AutoSelectLength",
         tr("settings", "Unique Candidate Auto-selection Length")},
        {"CommitAfterSelect", tr("settings", "Commit After Auto-selection")},
        {"CommitWhenDeactivate", tr("settings", "Commit When Deactivated")},
        {"UseFullWidth", tr("settings", "Use Full Width")},
        {"Hint", tr("settings", "Show Code Hints")},
        {"Learning", tr("settings", "Learn Word Frequency")},
        {"UseContextRelatedOrder", tr("settings", "Order by Context")},
        {"UseSystemLanguageModel", tr("settings", "Use System Language Model")},
        {"OrderPolicy", tr("settings", "Ordering Policy")},
        {"PrevPage", tr("settings", "Previous Page")},
        {"NextPage", tr("settings", "Next Page")},
        {"PrevCandidate", tr("settings", "Previous Candidate")},
        {"NextCandidate", tr("settings", "Next Candidate")},
        {"CurrentCandidate", tr("settings", "Select Current Candidate")},
        {"ChooseLastCharFromPhrase",
         tr("settings", "Choose Last Character from Phrase")},
        {"DefaultCandidate", tr("settings", "Select Current Candidate")}};
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
            title.Text(section == "Fuzzy" ? tr("settings", "Fuzzy Pinyin")
                       : section.empty()  ? tr("settings", "General")
                                          : std::wstring(to_hstring(section)));
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
        FrameworkElement layout{nullptr};
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
                            name = tr("settings", profileNames[p]);
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
            text.AcceptsReturn(option.kind == ipc::ConfigKind::KeyList);
            text.Text(to_hstring(option.value));
            text.Width(210);
            if (option.kind == ipc::ConfigKind::KeyList) {
                text.TextWrapping(TextWrapping::Wrap);
                text.MaxHeight(100);

                Button record;
                record.Content(SymbolIcon(Symbol::Keyboard));
                record.Width(32);
                record.Height(32);
                record.Padding({0, 0, 0, 0});
                record.Click(
                    [weak = weak_from_this(), target = make_weak(text),
                     caption = std::wstring(label.Text())](auto &&, auto &&) {
                        if (auto self = weak.lock()) {
                            if (auto editor = target.get())
                                self->recordShortcut(editor, caption);
                        }
                    });
                ToolTipService::SetToolTip(
                    record, box_value(tr("settings", "Record shortcut")));
                Automation::AutomationProperties::SetName(
                    record, tr("settings", "Record shortcut"));
                Automation::AutomationProperties::SetAutomationId(
                    record, to_hstring(option.path + "/record"));
                StackPanel panel;
                panel.Orientation(Orientation::Horizontal);
                panel.Spacing(8);
                panel.Children().Append(text);
                panel.Children().Append(record);
                layout = panel;
            }
            editor = text;
        }
        Automation::AutomationProperties::SetName(editor, label.Text());
        Automation::AutomationProperties::SetAutomationId(
            editor, to_hstring(option.path));
        if (!layout)
            layout = editor;
        Grid::SetColumn(layout, 1);
        editor.VerticalAlignment(VerticalAlignment::Center);
        layout.VerticalAlignment(VerticalAlignment::Center);
        row.Children().Append(label);
        row.Children().Append(layout);
        editors_.push_back(editor);
        options_.Children().Append(row);
    }
}

fire_and_forget SettingsWindow::recordShortcut(TextBox text,
                                               std::wstring label) try {
    const settings::diagnostic::Operation operation("record_shortcut");
    const auto lifetime = shared_from_this();
    if (working_ || closed_)
        co_return;
    const auto selectionStart = text.SelectionStart();
    const auto selectionLength = text.SelectionLength();
    ContentDialog dialog;
    dialog.XamlRoot(window_.Content().as<FrameworkElement>().XamlRoot());
    dialog.Title(box_value(label));
    dialog.PrimaryButtonText(tr("settings", "Use shortcut"));
    dialog.CloseButtonText(tr("settings", "Cancel"));
    dialog.IsPrimaryButtonEnabled(false);
    TextBox preview;
    preview.Width(280);
    preview.IsReadOnly(true);
    preview.PlaceholderText(tr("settings", "Recording..."));
    Automation::AutomationProperties::SetName(
        preview, tr("settings", "Recorded shortcut"));
    Automation::AutomationProperties::SetAutomationId(preview,
                                                      L"ShortcutPreview");
    dialog.Content(preview);
    auto recorder = std::make_shared<RecordingKeyboard>(
        handle_, [target = make_weak(preview),
                  owner = make_weak(dialog)](const auto &value) {
            if (auto box = target.get())
                box.Text(to_hstring(value));
            if (auto popup = owner.get())
                popup.IsPrimaryButtonEnabled(true);
        });
    preview.GotFocus(
        [weak = std::weak_ptr(recorder), target = make_weak(preview),
         error = tr("settings", "Cannot record shortcut.")](auto &&, auto &&) {
            if (auto capture = weak.lock(); capture && !capture->start()) {
                if (auto box = target.get())
                    box.PlaceholderText(error);
            }
        });
    preview.LostFocus([weak = std::weak_ptr(recorder)](auto &&, auto &&) {
        if (auto capture = weak.lock())
            capture->stop();
    });
    dialog.Opened([target = make_weak(preview)](auto &&, auto &&) {
        if (auto box = target.get())
            box.Focus(FocusState::Programmatic);
    });
    const auto activation = window_.Activated(
        auto_revoke,
        [weak = std::weak_ptr(recorder), target = make_weak(preview)](
            auto &&, const WindowActivatedEventArgs &args) {
            if (auto capture = weak.lock()) {
                capture->stop();
                if (args.WindowActivationState() !=
                    WindowActivationState::Deactivated) {
                    if (auto box = target.get();
                        box && box.FocusState() != FocusState::Unfocused)
                        capture->start();
                }
            }
        });
    try {
        const auto result = co_await dialog.ShowAsync();
        recorder->stop();
        if (closed_ || result != ContentDialogResult::Primary)
            co_return;
        const std::wstring key(preview.Text());
        if (key.empty())
            co_return;
        if (selectionLength) {
            text.Select(selectionStart, selectionLength);
            text.SelectedText(key);
        } else {
            std::wstring value(text.Text());
            if (!value.empty() && value.back() != L'\n' &&
                value.back() != L'\r')
                value += L'\n';
            text.Text(value + key);
        }
    } catch (const hresult_error &) {
        settings::diagnostic::exception("record_shortcut");
        recorder->stop();
        if (!closed_) {
            status_.Text(tr("settings", "Cannot record shortcut."));
            status_.Visibility(Visibility::Visible);
        }
    }
} catch (...) {
    settings::diagnostic::exception("record_shortcut", true);
    throw;
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
    const settings::diagnostic::Operation operation("load_config");
    // Copy before the first suspension; callers may pass a vector element.
    const auto id = entry;
    const apartment_context ui;
    ipc::MethodConfigReply reply;
    bool connected = false;
    co_await resume_background();
    try {
        PipeClient client({}, {}, &win32::diagnostic::settingsLogger());
        connected = client.connect() && client.getMethodConfig(id, reply);
    } catch (...) {
        settings::diagnostic::exception("load_config");
    }
    co_await ui;
    operation.stage(closed_ ? "closed" : "apply_reply");
    if (closed_) {
        co_return false;
    }
    if (!connected) {
        status_.Text(tr("settings", "Cannot connect to Core."));
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
    const settings::diagnostic::Operation operation("save_config");
    const apartment_context ui;
    const auto values = configValues();
    if (config_.id.empty() || config_.error != ipc::SettingsError::None) {
        co_return false;
    }
    for (size_t i = 0; i < values.size(); ++i) {
        if (!ipc::validConfigValue(config_.options[i], values[i].value)) {
            status_.Text(tr("settings", "Check the setting values."));
            status_.Visibility(Visibility::Visible);
            co_return false;
        }
    }
    ipc::MethodConfigRequest request{config_.id, config_.revision, values};
    ipc::MethodConfigReply reply;
    bool connected = false;
    co_await resume_background();
    try {
        PipeClient client({}, {}, &win32::diagnostic::settingsLogger());
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
        settings::diagnostic::exception("save_config");
        connected = false;
    }
    co_await ui;
    if (closed_) {
        co_return false;
    }
    if (!connected || reply.error != ipc::SettingsError::None) {
        status_.Text(connected ? errorText(reply.error)
                               : tr("settings", "Cannot connect to Core."));
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

fire_and_forget SettingsWindow::chooseMethod() try {
    const settings::diagnostic::Operation operation("choose_method");
    const auto lifetime = shared_from_this();
    const auto index = methods_.SelectedIndex();
    if (index < 0) {
        co_return;
    }
    const auto id = catalog_.selection.enabled.at(index);
    if (page_ == PageKind::InputMethod && id == config_.id) {
        busy(false);
        co_return;
    }
    busy(true);
    if (!(co_await resolveDraft())) {
        renderMethods(config_.id);
        busy(false);
        co_return;
    }
    if (!(co_await loadConfig(id))) {
        if (config_.id != id) {
            renderMethods(config_.id);
        }
    }
    busy(false);
} catch (...) {
    settings::diagnostic::exception("choose_method", true);
    throw;
}

fire_and_forget SettingsWindow::changeSelection(int action) try {
    const settings::diagnostic::Operation operation("change_selection");
    win32::diagnostic::settingsLogger().event(win32::diagnostic::Level::Debug,
                                              "settings_selection_action",
                                              {{"action", action}});
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
        selected = next.enabled.back();
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
    if ((page_ == PageKind::Global && !(co_await resolveDraft())) ||
        (page_ == PageKind::InputMethod && !config_.options.empty() &&
         !(co_await saveConfig()))) {
        busy(false);
        co_return;
    }
    next.revision = catalog_.selection.revision;
    const apartment_context ui;
    ipc::InputMethodsReply reply;
    bool connected = false;
    co_await resume_background();
    try {
        PipeClient client({}, {}, &win32::diagnostic::settingsLogger());
        connected = client.connect() && client.setInputMethods(next, reply);
        if (!connected) {
            client.disconnect();
            connected = client.connect() && client.getInputMethods(reply);
            if (connected && reply.selection.enabled != next.enabled) {
                reply.error = ipc::SettingsError::Conflict;
            }
        }
    } catch (...) {
        settings::diagnostic::exception("change_selection");
        connected = false;
    }
    co_await ui;
    if (closed_) {
        co_return;
    }
    if (connected && reply.error == ipc::SettingsError::None) {
        catalog_ = std::move(reply);
        page_ = PageKind::InputMethod;
        renderMethods(selected);
        co_await loadConfig(
            catalog_.selection.enabled.at(methods_.SelectedIndex()));
        status_.Visibility(Visibility::Collapsed);
    } else {
        status_.Text(connected ? errorText(reply.error)
                               : tr("settings", "Cannot connect to Core."));
        status_.Visibility(Visibility::Visible);
        retry_.Visibility(Visibility::Visible);
    }
    busy(false);
} catch (...) {
    settings::diagnostic::exception("change_selection", true);
    throw;
}

fire_and_forget SettingsWindow::refreshDictionaries() try {
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
        PipeClient client({}, {}, &win32::diagnostic::settingsLogger());
        connected = client.connect() && client.getDictionaries(reply);
    } catch (...) {
        settings::diagnostic::exception("refresh_dictionaries");
    }
    co_await ui;
    dictionaryRefresh_ = false;
    if (closed_) {
        co_return;
    }
    if (!connected) {
        dictionaries_.Items().Clear();
        dictionaryReply_ = {};
        dictionaryStatus_.Text(tr("settings", "Cannot connect to Core."));
        dictionaryStatus_.Visibility(Visibility::Visible);
        co_return;
    }
    dictionaryDirectory_.Text(to_hstring(reply.directory));
    dictionaryStatus_.Text(
        !reply.available
            ? tr("settings", "Pinyin dictionaries are unavailable.")
        : !reply.error.empty()
            ? tr("settings", "Cannot read the dictionary directory.")
        : reply.dictionaries.empty()
            ? tr("settings", "No third-party dictionaries")
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
            state.Text(tr("settings", dictionaryState(dictionary.status)));
            Grid::SetColumn(state, 1);
            row.Children().Append(name);
            row.Children().Append(state);
            ToolTipService::SetToolTip(row,
                                       box_value(to_hstring(dictionary.path)));
            dictionaries_.Items().Append(row);
        }
        dictionaryReply_ = std::move(reply);
    }
} catch (...) {
    settings::diagnostic::exception("refresh_dictionaries", true);
    throw;
}

fire_and_forget SettingsWindow::openDictionaryFolder() try {
    const settings::diagnostic::Operation operation("open_dictionary_folder");
    const auto lifetime = shared_from_this();
    const apartment_context ui;
    dictionaryFolder_.IsEnabled(false);
    HRESULT result = E_FAIL;
    co_await resume_background();
    try {
        result =
            openUserDirectory(FOLDERID_RoamingAppData, L"pinyin/dictionaries");
    } catch (...) {
        settings::diagnostic::exception("open_dictionary_folder");
    }
    co_await ui;
    if (!closed_) {
        dictionaryFolder_.IsEnabled(true);
        if (FAILED(result)) {
            dictionaryStatus_.Visibility(Visibility::Visible);
            dictionaryStatus_.Text(
                tr("settings", "Cannot open the dictionary folder."));
        }
    }
} catch (...) {
    settings::diagnostic::exception("open_dictionary_folder", true);
    throw;
}

fire_and_forget SettingsWindow::refreshPlugins() try {
    if (pluginRefresh_ || closed_)
        co_return;
    const auto lifetime = shared_from_this();
    const apartment_context ui;
    pluginRefresh_ = true;
    ipc::PluginsReply reply;
    bool connected = false;
    co_await resume_background();
    try {
        PipeClient client({}, {}, &win32::diagnostic::settingsLogger());
        connected = client.connect() && client.getPlugins(reply);
    } catch (...) {
        settings::diagnostic::exception("refresh_plugins");
    }
    co_await ui;
    pluginRefresh_ = false;
    if (closed_)
        co_return;
    if (!connected) {
        plugins_.Items().Clear();
        pluginReply_ = {};
        pluginDirectory_.Text(L"");
        pluginStatus_.Text(tr("settings", "Cannot connect to Core."));
        pluginStatus_.Visibility(Visibility::Visible);
        co_return;
    }
    pluginDirectory_.Text(to_hstring(reply.directory));
    pluginStatus_.Text(reply.plugins.empty() ? tr("settings", "No plugins")
                                             : L"");
    pluginStatus_.Visibility(pluginStatus_.Text().empty()
                                 ? Visibility::Collapsed
                                 : Visibility::Visible);
    if (reply == pluginReply_)
        co_return;
    plugins_.Items().Clear();
    for (const auto &plugin : reply.plugins) {
        Grid row;
        ColumnDefinition nameColumn, stateColumn;
        nameColumn.Width({1, GridUnitType::Star});
        stateColumn.Width({1, GridUnitType::Auto});
        row.ColumnDefinitions().Append(nameColumn);
        row.ColumnDefinitions().Append(stateColumn);
        row.ColumnSpacing(16);
        row.Padding({0, 8, 0, 8});
        StackPanel details;
        details.Spacing(4);
        TextBlock name, metadata, comment, state;
        name.Text(to_hstring(plugin.name.empty() ? plugin.id : plugin.name));
        name.TextWrapping(TextWrapping::Wrap);
        metadata.Text(to_hstring(plugin.id + " | " + plugin.version + " | " +
                                 plugin.library));
        metadata.FontSize(12);
        metadata.TextWrapping(TextWrapping::Wrap);
        metadata.IsTextSelectionEnabled(true);
        details.Children().Append(name);
        details.Children().Append(metadata);
        if (!plugin.comment.empty()) {
            comment.Text(to_hstring(plugin.comment));
            comment.FontSize(12);
            comment.TextWrapping(TextWrapping::Wrap);
            details.Children().Append(comment);
        }
        state.Text(tr("settings", pluginState(plugin.status)));
        Grid::SetColumn(state, 1);
        row.Children().Append(details);
        row.Children().Append(state);
        Automation::AutomationProperties::SetAutomationId(
            row, to_hstring("plugin:" + plugin.id));
        plugins_.Items().Append(row);
    }
    pluginReply_ = std::move(reply);
} catch (...) {
    settings::diagnostic::exception("refresh_plugins", true);
    throw;
}

fire_and_forget SettingsWindow::openPluginFolder() try {
    const auto lifetime = shared_from_this();
    const apartment_context ui;
    pluginFolder_.IsEnabled(false);
    HRESULT result = E_FAIL;
    co_await resume_background();
    try {
        result = openUserDirectory(FOLDERID_LocalAppData, L"plugins");
    } catch (...) {
        settings::diagnostic::exception("open_plugin_folder");
    }
    co_await ui;
    if (!closed_) {
        pluginFolder_.IsEnabled(true);
        if (FAILED(result)) {
            pluginStatus_.Text(
                tr("settings", "Cannot open the plugin folder."));
            pluginStatus_.Visibility(Visibility::Visible);
        }
    }
} catch (...) {
    settings::diagnostic::exception("open_plugin_folder", true);
    throw;
}

fire_and_forget SettingsWindow::refreshLogStatus(bool openFolder) try {
    if (logRefresh_ || closed_)
        co_return;
    if (openFolder)
        win32::diagnostic::settingsLogger().event(
            win32::diagnostic::Level::Info, "settings_log_folder_requested");
    const auto lifetime = shared_from_this();
    const apartment_context ui;
    logRefresh_ = true;
    logFolder_.IsEnabled(false);
    const bool enabled = win32::diagnostic::settingsLogger().enabled();
    if (enabled && win32::diagnostic::settingsLogger().status().state ==
                       win32::diagnostic::State::Starting)
        logStatus_.Text(tr("settings", "Starting"));
    std::filesystem::path path;
    win32::diagnostic::Status state;
    bool unavailable = false;
    co_await resume_background();
    try {
        auto &logger = win32::diagnostic::settingsLogger();
        logger.maintain(openFolder);
        logger.flush(std::chrono::milliseconds(1500));
        path = logger.directory();
        state = logger.status();
        unavailable = state.error != 0;
        if (openFolder && !unavailable) {
            const auto initialized = CoInitializeEx(
                nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (SUCCEEDED(initialized)) {
                SHELLEXECUTEINFOW execute{sizeof(execute)};
                execute.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
                execute.lpVerb = L"open";
                execute.lpFile = path.c_str();
                execute.nShow = SW_SHOWNORMAL;
                unavailable = !ShellExecuteExW(&execute);
                CoUninitialize();
            } else {
                unavailable = true;
            }
        }
    } catch (...) {
        settings::diagnostic::exception("refresh_log_status");
        unavailable = true;
    }
    co_await ui;
    logRefresh_ = false;
    if (closed_)
        co_return;
    logFolder_.IsEnabled(true);
    logDirectory_.Text(path.wstring());
    logStatus_.Text(unavailable ? tr("settings", "Unavailable")
                    : !win32::diagnostic::settingsLogger().enabled()
                        ? tr("settings", "Off")
                    : state.state == win32::diagnostic::State::Starting
                        ? tr("settings", "Starting")
                        : tr("settings", "Recording"));
    window_.Content()
        .as<FrameworkElement>()
        .FindName(L"LogStatistics")
        .as<TextBlock>()
        .Text(translator_.format("settings", "{0} log files, {1} bytes",
                                 state.fileCount, state.totalBytes));
} catch (...) {
    settings::diagnostic::exception("refresh_log_status", true);
    throw;
}
} // namespace fcitx
