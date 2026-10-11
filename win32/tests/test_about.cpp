#include "../tsf/aboutwindow.h"
#include "buildinfo.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {
std::wstring text(HWND window) {
    std::wstring result(GetWindowTextLengthW(window) + 1, L'\0');
    result.resize(
        GetWindowTextW(window, result.data(), static_cast<int>(result.size())));
    return result;
}

void checkBounds(HWND window) {
    RECT client{};
    assert(GetClientRect(window, &client));
    RECT previous{};
    for (const auto id : {100, 101, 102, 103, 104, IDOK}) {
        RECT bounds{};
        assert(GetWindowRect(GetDlgItem(window, id), &bounds));
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&bounds), 2);
        assert(bounds.left >= 0 && bounds.top >= 0);
        assert(bounds.right <= client.right && bounds.bottom <= client.bottom);
        if (id != 103) {
            assert(bounds.top >= previous.bottom);
        }
        previous = bounds;
    }
}

void snapshot(HWND window, const char *path) {
    RedrawWindow(window, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    RECT bounds{};
    assert(GetWindowRect(window, &bounds));
    const auto width = bounds.right - bounds.left;
    const auto height = bounds.bottom - bounds.top;
    const auto dc = CreateCompatibleDC(nullptr);
    const auto screen = GetDC(nullptr);
    const auto bitmap = CreateCompatibleBitmap(screen, width, height);
    ReleaseDC(nullptr, screen);
    const auto previous = SelectObject(dc, bitmap);
    assert(PrintWindow(window, dc, 0));
    SelectObject(dc, previous);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    std::vector<DWORD> pixels(width * height);
    assert(
        GetDIBits(dc, bitmap, 0, height, pixels.data(), &info, DIB_RGB_COLORS));
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size() * 4);
    FILE *file = nullptr;
    assert(fopen_s(&file, path, "wb") == 0 && file);
    assert(fwrite(&header, sizeof(header), 1, file) == 1);
    assert(fwrite(&info.bmiHeader, sizeof(info.bmiHeader), 1, file) == 1);
    assert(fwrite(pixels.data(), 4, pixels.size(), file) == pixels.size());
    fclose(file);
    DeleteObject(bitmap);
    DeleteDC(dc);
}
} // namespace

int main(int argc, char **argv) {
    using fcitx::AboutWindow;
    const auto original = GetThreadDpiAwarenessContext();
    const auto previous =
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    assert(previous);
    HWND destroyed = nullptr;
    {
        AboutWindow about;
        assert(about.show());
        assert(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),
                                            DPI_AWARENESS_CONTEXT_UNAWARE));
        const auto window = about.handle();
        destroyed = window;
        assert(IsWindowVisible(window));
        assert(GetWindow(window, GW_OWNER) == nullptr);
        assert(text(GetDlgItem(window, 100)) == L"Fcitx5 for Windows");
        const auto info = text(GetDlgItem(window, 101));
        assert(info == AboutWindow::details());
#if defined(_M_X64) || defined(__x86_64__)
        assert(info.find(L"x64 (AMD64)") != std::wstring::npos);
#endif
        assert(info.find(fcitx::buildinfo::commitTime) != std::wstring::npos);
        assert(info.find(L"http://") == std::wstring::npos);
        assert(info.find(L"https://") == std::wstring::npos);
        if (*fcitx::buildinfo::ci) {
            assert(info.find(fcitx::buildinfo::ci) != std::wstring::npos);
        } else {
            assert(info.find(L"Local build") != std::wstring::npos);
        }
        assert(text(GetDlgItem(window, 104)) == AboutWindow::homepage());
        assert(std::wstring(AboutWindow::license) ==
               L"https://www.gnu.org/licenses/gpl-3.0.html");
        assert(about.show() && about.handle() == window);
        auto traditional = std::make_shared<fcitx::win32::i18n::Translator>(
            fcitx::win32::ipc::UiLanguage::TraditionalChinese);
        about.setTranslation(traditional);
        assert(about.show() && about.handle() == window);
        assert(text(window) == L"\u95dc\u65bc Fcitx5");
        assert(text(GetDlgItem(window, IDOK)) == L"\u95dc\u9589");
        about.setTranslation(
            std::make_shared<fcitx::win32::i18n::Translator>());
        assert(about.show() && text(window) == L"About Fcitx5");
        const auto aware = SetThreadDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        checkBounds(window);
        if (argc > 2 && std::string(argv[1]) == "--snapshot") {
            snapshot(window, argv[2]);
        }
        SetThreadDpiAwarenessContext(aware);
        SetFocus(GetDlgItem(window, 102));
        SendMessageW(GetDlgItem(window, 102), WM_KEYDOWN, VK_TAB, 0);
        assert(GetFocus() == GetDlgItem(window, 103) ||
               GetFocus() == GetDlgItem(window, 104));
        if (argc > 1 && std::string(argv[1]) == "--preview") {
            std::printf("About HWND: %p\n", window);
            std::fflush(stdout);
            const auto end = GetTickCount64() + 30000;
            MSG message{};
            while (IsWindow(window) && GetTickCount64() < end) {
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
            }
        }
    }
    assert(!IsWindow(destroyed));
    {
        AboutWindow about;
        assert(about.show());
        SendMessageW(GetDlgItem(about.handle(), IDOK), WM_KEYDOWN, VK_ESCAPE,
                     0);
        assert(!about.handle());
        assert(about.show());
        SendMessageW(about.handle(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED),
                     0);
        assert(!about.handle());
        assert(about.show());
        SendMessageW(about.handle(), WM_CLOSE, 0, 0);
        assert(!about.handle());
    }
    SetThreadDpiAwarenessContext(original);
}
