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
    void busy(bool enabled);
    winrt::Microsoft::UI::Xaml::Window window_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::RadioButtons scheme_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox profile_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock status_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button confirm_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button retry_{nullptr};
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer_{nullptr};
    std::vector<ipc::ShuangpinProfile> profiles_;
    ipc::PinyinSettings settings_;
    bool loaded_ = false;
    bool closed_ = false;
    bool doubleAvailable_ = false;
    HANDLE activation_ = nullptr;
    HWND handle_ = nullptr;
    std::wstring activationProperty_;
};
} // namespace fcitx
