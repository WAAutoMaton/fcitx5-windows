#pragma once

#include "../i18n/monitor.h"
#include <string>
#include <windows.h>

namespace fcitx {

class AboutWindow {
  public:
    AboutWindow() = default;
    AboutWindow(const AboutWindow &) = delete;
    AboutWindow &operator=(const AboutWindow &) = delete;
    ~AboutWindow();
    bool show();
    void setTranslation(win32::i18n::Translation translation) {
        translation_ = std::move(translation);
    }
    void close();
    HWND handle() const { return window_; }
    static std::wstring
    details(const win32::i18n::Translator *translator = nullptr);
    static const wchar_t *homepage();
    static constexpr const wchar_t *license =
        L"https://www.gnu.org/licenses/gpl-3.0.html";

  private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam,
                                       LPARAM lParam);
    static LRESULT CALLBACK controlProc(HWND window, UINT message,
                                        WPARAM wParam, LPARAM lParam,
                                        UINT_PTR id, DWORD_PTR data);
    bool createControls();
    void refreshText();
    void layout();
    HWND window_ = nullptr;
    HFONT font_ = nullptr;
    HFONT headingFont_ = nullptr;
    HINSTANCE module_ = nullptr;
    win32::i18n::Translation translation_;
};

} // namespace fcitx
