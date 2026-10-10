#pragma once

#include "../ipc/service.h"
#include "../tsf/pipeclient.h"
#undef GetCurrentTime
#include <memory>
#include <vector>
#include <winrt/Microsoft.UI.Dispatching.h>
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
    winrt::fire_and_forget refreshDictionaries();
    winrt::fire_and_forget openDictionaryFolder();
    void busy(bool enabled);
    winrt::Microsoft::UI::Xaml::Window window_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::RadioButtons scheme_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox profile_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock status_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button confirm_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button retry_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TabView tabs_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ListView dictionaries_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock dictionaryDirectory_{
        nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock dictionaryStatus_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button dictionaryFolder_{nullptr};
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer_{nullptr};
    std::vector<ipc::ShuangpinProfile> profiles_;
    ipc::PinyinSettings settings_;
    ipc::DictionariesReply dictionaryReply_;
    bool dictionaryRefresh_ = false;
    unsigned dictionaryTicks_ = 0;
    bool loaded_ = false;
    bool closed_ = false;
    bool doubleAvailable_ = false;
    HANDLE activation_ = nullptr;
    HWND handle_ = nullptr;
    std::wstring activationProperty_;
};
} // namespace fcitx
