#pragma once

#include "../i18n/translator.h"
#include "../ipc/service.h"
#include "../tsf/pipeclient.h"
#undef GetCurrentTime
#include <memory>
#include <vector>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace fcitx {
bool settingsServiceStopping();
class SettingsWindow : public std::enable_shared_from_this<SettingsWindow> {
  public:
    ~SettingsWindow();
    bool open();

  private:
    winrt::fire_and_forget transfer(bool save);
    winrt::fire_and_forget chooseMethod();
    winrt::fire_and_forget chooseGlobal();
    winrt::Windows::Foundation::IAsyncOperation<bool> resolveDraft();
    winrt::Windows::Foundation::IAsyncOperation<bool> loadGlobal();
    winrt::Windows::Foundation::IAsyncOperation<bool> saveGlobal();
    void renderGlobal();
    void renderShortcuts();
    ipc::GlobalSettings globalDraft() const;
    winrt::Microsoft::UI::Xaml::Controls::StackPanel
    shortcutEditor(winrt::Microsoft::UI::Xaml::Controls::TextBox text,
                   const std::wstring &label, const std::string &id);
    std::wstring tr(const char *context, const char *english) const;
    std::wstring errorText(ipc::SettingsError error) const;
    void translateStatic();
    winrt::fire_and_forget changeSelection(int action);
    winrt::Windows::Foundation::IAsyncOperation<bool> saveConfig();
    winrt::Windows::Foundation::IAsyncOperation<bool>
    loadConfig(const std::string &id);
    std::vector<ipc::ConfigValue> configValues() const;
    void renderConfig();
    winrt::fire_and_forget
    recordShortcut(winrt::Microsoft::UI::Xaml::Controls::TextBox text,
                   std::wstring label);
    void renderMethods(const std::string &selected = {});
    winrt::fire_and_forget
    loadIcon(winrt::Microsoft::UI::Xaml::Controls::Image image,
             winrt::Microsoft::UI::Xaml::Controls::TextBlock fallback,
             std::string path);
    const ipc::InputMethodInfo *method(const std::string &id) const;
    winrt::fire_and_forget refreshDictionaries();
    winrt::fire_and_forget openDictionaryFolder();
    winrt::fire_and_forget refreshPlugins();
    winrt::fire_and_forget openPluginFolder();
    winrt::fire_and_forget refreshLogStatus(bool openFolder = false);
    void busy(bool enabled);
    winrt::Microsoft::UI::Xaml::Window window_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ListView methods_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox available_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::StackPanel options_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock heading_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button add_{nullptr},
        remove_{nullptr}, up_{nullptr}, down_{nullptr}, reset_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TabViewItem dictionaryTab_{nullptr};
    std::vector<winrt::Microsoft::UI::Xaml::FrameworkElement> editors_;
    ipc::InputMethodsReply catalog_;
    ipc::MethodConfigReply config_;
    std::vector<std::string> availableIds_;
    bool working_ = false, rendering_ = false;
    enum class PageKind { InputMethod, Global };
    PageKind page_ = PageKind::InputMethod;
    win32::i18n::Translator translator_;
    bool languageInitialized_ = false;
    ipc::GlobalSettingsReply global_;
    std::array<winrt::Microsoft::UI::Xaml::Controls::TextBox, 6>
        shortcutEditors_{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton
        globalButton_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TabView globalPanel_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox language_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch detailedLogging_{
        nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock logStatus_{nullptr},
        logDirectory_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button logFolder_{nullptr};
    bool logRefresh_ = false;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock status_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button confirm_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button retry_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TabView tabs_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ListView dictionaries_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock dictionaryDirectory_{
        nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock dictionaryStatus_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button dictionaryFolder_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ListView plugins_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock pluginDirectory_{nullptr},
        pluginStatus_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button pluginFolder_{nullptr};
    ipc::PluginsReply pluginReply_;
    bool pluginRefresh_ = false;
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer_{nullptr};
    ipc::DictionariesReply dictionaryReply_;
    bool dictionaryRefresh_ = false;
    unsigned dictionaryTicks_ = 0;
    bool loaded_ = false;
    bool closed_ = false;
    HANDLE activation_ = nullptr;
    HWND handle_ = nullptr;
    std::wstring activationProperty_;
};
} // namespace fcitx
