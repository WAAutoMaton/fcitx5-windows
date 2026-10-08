#include "candidatewindow.h"
#include "textutil.h"
#include <algorithm>

namespace fcitx {

CandidateWindow::~CandidateWindow() {
    if (window_) {
        const auto module = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(window_, GWLP_HINSTANCE));
        DestroyWindow(window_);
        UnregisterClassW(L"Fcitx5WindowsCandidatesV2", module);
    }
    if (font_) {
        DeleteObject(font_);
    }
}

void CandidateWindow::hide() {
    if (window_) {
        ShowWindow(window_, SW_HIDE);
    }
}

void CandidateWindow::show(const PipeClient::KeyReply &reply, HWND owner,
                           RECT anchor) {
    if (reply.candidates.empty() || (owner && !IsWindowVisible(owner))) {
        hide();
        return;
    }
    HINSTANCE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&windowProc), &module);
    if (!window_) {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = windowProc;
        windowClass.hInstance = module;
        windowClass.lpszClassName = L"Fcitx5WindowsCandidatesV2";
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        if (!RegisterClassW(&windowClass) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return;
        }
        window_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                  windowClass.lpszClassName, L"Fcitx5",
                                  WS_POPUP | WS_BORDER, 0, 0, 0, 0, owner,
                                  nullptr, module, this);
        if (!window_) {
            return;
        }
    }
    SetWindowLongPtrW(window_, GWLP_HWNDPARENT,
                      reinterpret_cast<LONG_PTR>(owner));
    const auto dpi = owner ? GetDpiForWindow(owner) : 96;
    rowHeight_ = MulDiv(24, dpi ? dpi : 96, 96);
    if (font_) {
        DeleteObject(font_);
    }
    font_ = CreateFontW(-MulDiv(14, dpi ? dpi : 96, 96), 0, 0, 0, FW_NORMAL,
                        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    lines_.clear();
    selected_ = reply.selected;
    LONG width = MulDiv(160, dpi ? dpi : 96, 96);
    HDC device = GetDC(window_);
    const auto previousFont =
        SelectObject(device, font_ ? font_ : GetStockObject(DEFAULT_GUI_FONT));
    for (const auto &candidate : reply.candidates) {
        std::wstring line;
        const auto text =
            candidate.label + " " + candidate.text +
            (candidate.comment.empty() ? "" : "  " + candidate.comment);
        if (!utf8ToWide(text, line)) {
            hide();
            SelectObject(device, previousFont);
            ReleaseDC(window_, device);
            return;
        }
        SIZE extent{};
        GetTextExtentPoint32W(device, line.c_str(),
                              static_cast<int>(line.size()), &extent);
        width = std::max(width, extent.cx + rowHeight_);
        lines_.push_back(std::move(line));
    }
    SelectObject(device, previousFont);
    ReleaseDC(window_, device);
    const auto height = static_cast<int>(lines_.size()) * rowHeight_ + 2;
    MONITORINFO monitor{sizeof(MONITORINFO)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST),
                    &monitor);
    width = std::min(width, monitor.rcWork.right - monitor.rcWork.left);
    const auto left = std::clamp(anchor.left, monitor.rcWork.left,
                                 monitor.rcWork.right - width);
    auto top = anchor.bottom;
    if (top + height > monitor.rcWork.bottom) {
        top = std::max(monitor.rcWork.top, anchor.top - height);
    }
    SetWindowPos(window_, HWND_TOPMOST, left, top, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(window_, nullptr, TRUE);
}

LRESULT CALLBACK CandidateWindow::windowProc(HWND window, UINT message,
                                             WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto *creation = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    }
    auto *self = reinterpret_cast<CandidateWindow *>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_MOUSEACTIVATE) {
        return MA_NOACTIVATE;
    }
    if (message == WM_PAINT && self) {
        self->paint();
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void CandidateWindow::paint() {
    PAINTSTRUCT paint{};
    HDC device = BeginPaint(window_, &paint);
    RECT client{};
    GetClientRect(window_, &client);
    FillRect(device, &client, GetSysColorBrush(COLOR_WINDOW));
    const auto previousFont =
        SelectObject(device, font_ ? font_ : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(device, TRANSPARENT);
    for (size_t index = 0; index < lines_.size(); ++index) {
        RECT row{0, static_cast<LONG>(index * rowHeight_), client.right,
                 static_cast<LONG>((index + 1) * rowHeight_)};
        const bool selected = index == selected_;
        if (selected) {
            FillRect(device, &row, GetSysColorBrush(COLOR_HIGHLIGHT));
        }
        SetTextColor(device, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT
                                                  : COLOR_WINDOWTEXT));
        row.left += rowHeight_ / 3;
        DrawTextW(device, lines_[index].c_str(),
                  static_cast<int>(lines_[index].size()), &row,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX |
                      DT_END_ELLIPSIS);
    }
    SelectObject(device, previousFont);
    EndPaint(window_, &paint);
}

} // namespace fcitx
