#include "aboutwindow.h"
#include "../dll/resource.h"
#include "buildinfo.h"

#include <commctrl.h>
#include <shellapi.h>

namespace fcitx {
namespace {
constexpr wchar_t kClassName[] = L"Fcitx5WindowsAbout";
constexpr wchar_t kTitle[] = L"About Fcitx5";
constexpr int kHeading = 100;
constexpr int kDetails = 101;
constexpr int kLicense = 102;
constexpr int kHomepage = 103;
constexpr int kUrl = 104;

bool webLink(const wchar_t *url) {
    return wcsncmp(url, L"https://", 8) == 0 ||
           wcsncmp(url, L"http://", 7) == 0;
}

const wchar_t *architecture() {
#if defined(_M_ARM64) || defined(__aarch64__)
    return L"ARM64";
#elif defined(_M_X64) || defined(__x86_64__)
    return L"x64 (AMD64)";
#elif defined(_M_IX86) || defined(__i386__)
    return L"x86";
#else
    return L"Unknown";
#endif
}

class DpiScope {
  public:
    DpiScope()
        : previous_(SetThreadDpiAwarenessContext(
              DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}
    ~DpiScope() {
        if (previous_) {
            SetThreadDpiAwarenessContext(previous_);
        }
    }

  private:
    DPI_AWARENESS_CONTEXT previous_;
};
} // namespace

std::wstring AboutWindow::details(const win32::i18n::Translator *translator) {
    const win32::i18n::Translator english;
    const auto &translation = translator ? *translator : english;
    std::wstring result = translation.wide("about", "Architecture: ");
    result += std::wstring(architecture()) == L"Unknown"
                  ? translation.wide("about", "Unknown")
                  : architecture();
    result += L"\r\n" + translation.wide("about", "CI build: ");
    result +=
        *buildinfo::ci
            ? buildinfo::ci
            : translation.wide("about", "Local build (no CI information)");
    result += L"\r\nCommit\uff1a";
    result += std::wstring(buildinfo::commit) == L"Unavailable"
                  ? translation.wide("about", "Unavailable")
                  : buildinfo::commit;
    result += L"\r\n" + translation.wide("about", "Commit time: ");
    result += std::wstring(buildinfo::commitTime) == L"Unavailable"
                  ? translation.wide("about", "Unavailable")
                  : buildinfo::commitTime;
    return result;
}

const wchar_t *AboutWindow::homepage() { return buildinfo::homepage; }

AboutWindow::~AboutWindow() { close(); }

bool AboutWindow::show() {
    if (window_) {
        refreshText();
        ShowWindow(window_, SW_RESTORE);
        SetForegroundWindow(window_);
        return true;
    }
    DpiScope dpi;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&windowProc), &module_);
    WNDCLASSW windowClass{};
    windowClass.hInstance = module_;
    windowClass.lpfnWndProc = windowProc;
    windowClass.lpszClassName = kClassName;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hIcon = LoadIconW(module_, MAKEINTRESOURCEW(IDI_FCITX5));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&windowClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    window_ = CreateWindowExW(
        WS_EX_APPWINDOW, kClassName, kTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
        CW_USEDEFAULT, 600, 400, nullptr, nullptr, module_, this);
    if (!window_ || !createControls()) {
        close();
        return false;
    }
    const auto scale = GetDpiForWindow(window_);
    RECT bounds{0, 0, MulDiv(560, scale, 96), MulDiv(330, scale, 96)};
    AdjustWindowRectExForDpi(&bounds, GetWindowLongW(window_, GWL_STYLE), FALSE,
                             WS_EX_APPWINDOW, scale);
    SetWindowPos(window_, nullptr, 0, 0, bounds.right - bounds.left,
                 bounds.bottom - bounds.top, SWP_NOMOVE | SWP_NOZORDER);
    layout();
    ShowWindow(window_, SW_SHOWNORMAL);
    SetForegroundWindow(window_);
    SetFocus(GetDlgItem(window_, IDOK));
    return true;
}

bool AboutWindow::createControls() {
    const auto info = details(translation_.get());
    struct Control {
        int id;
        const wchar_t *type;
        const wchar_t *text;
        DWORD style;
    };
    const Control controls[] = {
        {kHeading, L"STATIC", L"Fcitx5 for Windows", SS_LEFT},
        {kDetails, L"EDIT", info.c_str(),
         ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_TABSTOP},
        {kLicense, L"BUTTON", L"GPL-3.0 License", WS_TABSTOP},
        {kHomepage, L"BUTTON", L"Repository Homepage", WS_TABSTOP},
        {kUrl, L"EDIT", homepage(),
         ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_TABSTOP},
        {IDOK, L"BUTTON", L"Close", WS_TABSTOP}};
    for (const auto &control : controls) {
        const auto child = CreateWindowExW(
            0, control.type, control.text,
            WS_CHILD | WS_VISIBLE | control.style, 0, 0, 0, 0, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(control.id)), module_,
            nullptr);
        if (!child || !SetWindowSubclass(child, controlProc, 1,
                                         reinterpret_cast<DWORD_PTR>(this))) {
            return false;
        }
    }
    EnableWindow(GetDlgItem(window_, kHomepage), webLink(homepage()));
    refreshText();
    return true;
}

void AboutWindow::refreshText() {
    const auto snapshot = translation_;
    const win32::i18n::Translator english;
    const auto &translation = snapshot ? *snapshot : english;
    SetWindowTextW(window_, translation.wide("about", "About Fcitx5").c_str());
    SetWindowTextW(GetDlgItem(window_, kDetails),
                   details(&translation).c_str());
    const std::pair<int, const char *> labels[] = {
        {kLicense, win32::i18n::mark("about", "GPL-3.0 License")},
        {kHomepage, win32::i18n::mark("about", "Repository Homepage")},
        {IDOK, win32::i18n::mark("about", "Close")}};
    for (const auto &[id, message] : labels)
        SetWindowTextW(GetDlgItem(window_, id),
                       translation.wide("about", message).c_str());
}

void AboutWindow::layout() {
    const auto dpi = GetDpiForWindow(window_);
    const auto size = [dpi](int value) { return MulDiv(value, dpi, 96); };
    auto font =
        CreateFontW(-size(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    auto heading =
        CreateFontW(-size(24), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    const auto place = [&](int id, int x, int y, int width, int height) {
        const auto control = GetDlgItem(window_, id);
        SendMessageW(control, WM_SETFONT,
                     reinterpret_cast<WPARAM>(id == kHeading ? heading : font),
                     TRUE);
        MoveWindow(control, size(x), size(y), size(width), size(height), TRUE);
    };
    place(kHeading, 24, 22, 512, 40);
    place(kDetails, 24, 78, 512, 110);
    place(kLicense, 24, 204, 172, 34);
    place(kHomepage, 208, 204, 180, 34);
    place(kUrl, 24, 248, 512, 42);
    place(IDOK, 436, 292, 100, 30);
    if (font_) {
        DeleteObject(font_);
    }
    if (headingFont_) {
        DeleteObject(headingFont_);
    }
    font_ = font;
    headingFont_ = heading;
}

void AboutWindow::close() {
    if (window_) {
        DestroyWindow(window_);
    }
    if (module_) {
        UnregisterClassW(kClassName, module_);
    }
}

LRESULT CALLBACK AboutWindow::windowProc(HWND window, UINT message,
                                         WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto *creation = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    }
    auto *self = reinterpret_cast<AboutWindow *>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self && message == WM_DPICHANGED) {
        const auto *bounds = reinterpret_cast<RECT *>(lParam);
        SetWindowPos(window, nullptr, bounds->left, bounds->top,
                     bounds->right - bounds->left, bounds->bottom - bounds->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        self->layout();
        return 0;
    }
    if (self && message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED) {
        const auto id = LOWORD(wParam);
        if (id == IDOK || id == IDCANCEL) {
            self->close();
        } else if (id == kLicense || id == kHomepage) {
            const auto *url = id == kLicense ? license : homepage();
            if (webLink(url) && reinterpret_cast<INT_PTR>(ShellExecuteW(
                                    window, L"open", url, nullptr, nullptr,
                                    SW_SHOWNORMAL)) <= 32) {
                MessageBoxW(window,
                            L"\u65e0\u6cd5\u6253\u5f00\u94fe\u63a5\u3002",
                            kTitle, MB_OK | MB_ICONERROR);
            }
        }
        return 0;
    }
    if (self && message == WM_CLOSE) {
        self->close();
        return 0;
    }
    if (self && message == WM_NCDESTROY) {
        self->window_ = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        DeleteObject(self->font_);
        DeleteObject(self->headingFont_);
        self->font_ = nullptr;
        self->headingFont_ = nullptr;
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT) {
        const auto dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK AboutWindow::controlProc(HWND window, UINT message,
                                          WPARAM wParam, LPARAM lParam,
                                          UINT_PTR id, DWORD_PTR data) {
    auto *self = reinterpret_cast<AboutWindow *>(data);
    if (message == WM_KEYDOWN) {
        if (wParam == VK_TAB) {
            SetFocus(GetNextDlgTabItem(self->window_, window,
                                       GetKeyState(VK_SHIFT) < 0));
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            self->close();
            return 0;
        }
        const auto controlId = GetDlgCtrlID(window);
        if (wParam == VK_RETURN &&
            (controlId == kLicense || controlId == kHomepage ||
             controlId == IDOK)) {
            SendMessageW(self->window_, WM_COMMAND,
                         MAKEWPARAM(controlId, BN_CLICKED),
                         reinterpret_cast<LPARAM>(window));
            return 0;
        }
    }
    if (message == WM_CHAR && (wParam == VK_TAB || wParam == VK_ESCAPE)) {
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, controlProc, id);
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

} // namespace fcitx
