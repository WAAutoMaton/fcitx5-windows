#pragma once

#include "pipeclient.h"
#include <string>
#include <vector>

namespace fcitx {

class CandidateWindow {
  public:
    ~CandidateWindow();
    void show(const PipeClient::KeyReply &reply, HWND owner, RECT anchor);
    void hide();
    bool visible() const { return window_ && IsWindowVisible(window_); }

  private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam,
                                       LPARAM lParam);
    void paint();
    HWND window_ = nullptr;
    std::vector<std::wstring> lines_;
    uint32_t selected_ = UINT32_MAX;
    int rowHeight_ = 24;
    HFONT font_ = nullptr;
};

} // namespace fcitx
