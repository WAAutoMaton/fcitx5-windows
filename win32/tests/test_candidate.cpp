#include "../tsf/candidatewindow.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <dwmapi.h>
#include <filesystem>
#include <wincodec.h>

namespace fcitx {
struct CandidateWindowTestAccess {
    static HWND window(const CandidateWindow &view) { return view.window_; }
    static IDWriteTextLayout *text(const CandidateWindow &view, size_t index) {
        return view.rows_[index].text.layout;
    }
    static void discardTarget(CandidateWindow &view) { view.discardTarget(); }
    static void checkTrimming(const CandidateWindow &view) {
        DWRITE_LINE_METRICS line{};
        UINT32 count = 0;
        const auto &last = view.rows_.back();
        assert(SUCCEEDED(last.text.layout->GetLineMetrics(&line, 1, &count)));
        assert(count == 1 && line.isTrimmed);
        assert(
            SUCCEEDED(last.comment.layout->GetLineMetrics(&line, 1, &count)));
        assert(count == 1 && line.isTrimmed);
    }
    static void prepare(CandidateWindow &view,
                        const PipeClient::KeyReply &reply, UINT dpi) {
        assert(view.initialize());
        assert(view.updateRows(reply));
        view.selected_ = reply.selected;
        view.dpi_ = dpi;
        view.anchor_ = {100, 100, 101, 101};
        assert(view.layout());
    }
    static CComPtr<IWICBitmap> render(CandidateWindow &view,
                                      IWICImagingFactory *factory) {
        const auto width =
            static_cast<UINT>(std::ceil(view.width_ * view.dpi_ / 96));
        const auto height =
            static_cast<UINT>(std::ceil(view.height_ * view.dpi_ / 96));
        CComPtr<IWICBitmap> bitmap;
        assert(SUCCEEDED(factory->CreateBitmap(width, height,
                                               GUID_WICPixelFormat32bppPBGRA,
                                               WICBitmapCacheOnLoad, &bitmap)));
        CComPtr<ID2D1RenderTarget> target;
        const auto properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<float>(view.dpi_), static_cast<float>(view.dpi_));
        assert(SUCCEEDED(view.drawingFactory_->CreateWicBitmapRenderTarget(
            bitmap, properties, &target)));
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        CComPtr<ID2D1SolidColorBrush> brush;
        assert(SUCCEEDED(
            target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush)));
        assert(SUCCEEDED(view.draw(target, brush)));
        return bitmap;
    }
};
} // namespace fcitx

namespace {
using Access = fcitx::CandidateWindowTestAccess;
using Reply = fcitx::PipeClient::KeyReply;

void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void save(IWICImagingFactory *factory, IWICBitmapSource *bitmap,
          const std::filesystem::path &path) {
    CComPtr<IWICStream> stream;
    assert(SUCCEEDED(factory->CreateStream(&stream)));
    assert(
        SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)));
    CComPtr<IWICBitmapEncoder> encoder;
    assert(SUCCEEDED(
        factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)));
    assert(SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)));
    CComPtr<IWICBitmapFrameEncode> frame;
    assert(SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)));
    assert(SUCCEEDED(frame->Initialize(nullptr)));
    UINT width = 0, height = 0;
    assert(SUCCEEDED(bitmap->GetSize(&width, &height)));
    assert(SUCCEEDED(frame->SetSize(width, height)));
    auto format = GUID_WICPixelFormat32bppBGRA;
    assert(SUCCEEDED(frame->SetPixelFormat(&format)));
    assert(SUCCEEDED(frame->WriteSource(bitmap, nullptr)));
    assert(SUCCEEDED(frame->Commit()));
    assert(SUCCEEDED(encoder->Commit()));
}

size_t coloredPixels(IWICBitmapSource *bitmap) {
    UINT width = 0, height = 0;
    assert(SUCCEEDED(bitmap->GetSize(&width, &height)));
    std::vector<BYTE> pixels(width * height * 4);
    assert(SUCCEEDED(bitmap->CopyPixels(
        nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())));
    size_t colored = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        const auto low = std::min({pixels[i], pixels[i + 1], pixels[i + 2]});
        const auto high = std::max({pixels[i], pixels[i + 1], pixels[i + 2]});
        colored += high - low > 40;
    }
    return colored;
}

Reply fixture() {
    Reply reply;
    reply.selected = 1;
    reply.candidates = {
        {"\xe4\xbd\xa0\xe5\xa5\xbd", "1", "ni hao"},
        {"\xf0\x9f\x98\x80 \xf0\x9f\x8e\x89 \xe2\x9d\xa4\xef\xb8\x8f", "2",
         "emoji"},
        {"\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd "
         "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb",
         "3", "ZWJ / skin tone"},
        {"\xe4\xb8\xad\xe6\x96\x87 / Latin / e\xcc\x81", "4", "fallback"},
        {"\xe2\x9d\xa4\xef\xb8\x8e / \xe2\x9d\xa4\xef\xb8\x8f", "5",
         "text / emoji"},
        {"A long candidate with enough text to exercise native ellipsis and "
         "column alignment",
         "6",
         "A long dictionary comment that should be trimmed before it crowds "
         "the candidate"}};
    return reply;
}

void renderTests(IWICImagingFactory *factory,
                 const std::filesystem::path &directory) {
    for (const UINT dpi : {96u, 120u, 144u, 192u, 288u}) {
        fcitx::CandidateWindow view;
        Reply mono;
        mono.candidates = {{"ABC 123", "", ""}};
        Access::prepare(view, mono, dpi);
        const auto monoPixels = coloredPixels(Access::render(view, factory));
        Reply emoji;
        emoji.candidates = {{"\xf0\x9f\x98\x80", "", ""}};
        Access::prepare(view, emoji, dpi);
        const auto pixels = coloredPixels(Access::render(view, factory));
        assert(pixels > monoPixels + 30);
        Reply textPresentation;
        textPresentation.candidates = {{"\xe2\x9d\xa4\xef\xb8\x8e", "", ""}};
        Access::prepare(view, textPresentation, dpi);
        assert(coloredPixels(Access::render(view, factory)) <= monoPixels);
        Access::prepare(view, fixture(), dpi);
        Access::checkTrimming(view);
        auto bitmap = Access::render(view, factory);
        if (!directory.empty()) {
            save(factory, bitmap,
                 directory /
                     (L"candidates-" + std::to_wstring(dpi) + L"dpi.png"));
        }
        std::printf("DPI %u: emoji colored pixels = %zu (control = %zu)\n", dpi,
                    pixels, monoPixels);
        CComPtr<IDWriteTextLayout> previous = Access::text(view, 0);
        auto selected = fixture();
        selected.selected = 3;
        Access::prepare(view, selected, dpi);
        assert(previous == Access::text(view, 0));
        auto after = Access::render(view, factory);
        assert(coloredPixels(after) > 0);
    }
}

void windowTests() {
    const auto initial = GetThreadDpiAwarenessContext();
    for (const auto context :
         {DPI_AWARENESS_CONTEXT_UNAWARE, DPI_AWARENESS_CONTEXT_SYSTEM_AWARE,
          DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2}) {
        assert(SetThreadDpiAwarenessContext(context));
        const auto owner =
            CreateWindowExW(0, L"STATIC", L"Candidate test owner",
                            WS_OVERLAPPEDWINDOW, 100, 100, 600, 400, nullptr,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
        assert(owner);
        ShowWindow(owner, SW_SHOWNOACTIVATE);
        const auto foreground = GetForegroundWindow();
        POINT point{40, 60};
        assert(ClientToScreen(owner, &point));
        RECT anchor{point.x, point.y, point.x + 1, point.y + 20};
        POINT physical{anchor.left, anchor.bottom};
        assert(LogicalToPhysicalPointForPerMonitorDPI(owner, &physical));
        fcitx::CandidateWindow view;
        auto reply = fixture();
        view.show(reply, owner, anchor);
        assert(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),
                                            context));
        assert(view.visible());
        const auto window = Access::window(view);
        assert(AreDpiAwarenessContextsEqual(
            GetWindowDpiAwarenessContext(window),
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
        assert(GetForegroundWindow() == foreground);
        assert(SendMessageW(window, WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATE);
        assert(GetWindowLongPtrW(window, GWLP_HWNDPARENT) ==
               reinterpret_cast<LONG_PTR>(owner));
        assert(SetThreadDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
        UpdateWindow(window);
        assert(view.visible());
        RECT bounds{};
        assert(GetWindowRect(window, &bounds));
        assert(bounds.left == physical.x && bounds.top == physical.y);
        CComPtr<IDWriteTextLayout> previous = Access::text(view, 0);
        assert(SetThreadDpiAwarenessContext(context));
        reply.selected = 2;
        view.show(reply, owner, anchor);
        assert(previous == Access::text(view, 0));
        Access::discardTarget(view);
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
        assert(view.visible());
        assert(GetForegroundWindow() == foreground);
        ShowWindow(owner, SW_HIDE);
        view.show(reply, owner, anchor);
        assert(!view.visible());
        ShowWindow(owner, SW_SHOWNOACTIVATE);
        view.show(reply, owner, anchor);
        reply.candidates.front().text = "\xff";
        view.show(reply, owner, anchor);
        assert(!view.visible());
        view.show(fixture(), owner, anchor);
        assert(view.visible());
        view.show({}, owner, anchor);
        assert(!view.visible());
        view.show(fixture(), owner, anchor);
        DestroyWindow(owner);
        assert(!Access::window(view));
        view.show(fixture(), nullptr, {100, 100, 101, 120});
        assert(view.visible());
    }
    assert(SetThreadDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    fcitx::CandidateWindow view;
    MONITORINFO monitor{sizeof(MONITORINFO)};
    RECT anchor{0, 0, 1, 1};
    assert(GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST),
                           &monitor));
    anchor = {monitor.rcWork.right - 2, monitor.rcWork.bottom - 2,
              monitor.rcWork.right - 1, monitor.rcWork.bottom - 1};
    view.show(fixture(), nullptr, anchor);
    RECT bounds{};
    assert(GetWindowRect(Access::window(view), &bounds));
    assert(bounds.left >= monitor.rcWork.left &&
           bounds.right <= monitor.rcWork.right);
    assert(bounds.top >= monitor.rcWork.top && bounds.bottom <= anchor.top);
    auto many = fixture();
    many.candidates.resize(32, many.candidates.front());
    view.show(many, nullptr, anchor);
    assert(GetWindowRect(Access::window(view), &bounds));
    assert(bounds.top >= monitor.rcWork.top &&
           bounds.bottom <= monitor.rcWork.bottom);
    view.hide();
    assert(SetThreadDpiAwarenessContext(initial));
}

void preview(IWICImagingFactory *factory,
             const std::filesystem::path &directory) {
    const auto previous = SetThreadDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    fcitx::CandidateWindow view;
    Reply reply;
    reply.selected = 1;
    reply.candidates = {
        {"\xe4\xbd\xa0\xe5\xa5\xbd", "1", "ni hao"},
        {"\xf0\x9f\x98\x80", "2", "emoji"},
        {"\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd", "3", "skin tone"},
        {"\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb", "4", "ZWJ"},
        {"\xe2\x9d\xa4\xef\xb8\x8f", "5", "heart"},
        {"\xe6\x82\xa8\xe5\xa5\xbd", "6", "nin hao"}};
    view.show(reply, nullptr, {240, 240, 241, 280});
    const auto window = Access::window(view);
    assert(window);
    UpdateWindow(window);
    assert(view.visible());
    const auto presentation = GetTickCount64() + 300;
    while (GetTickCount64() < presentation) {
        pump();
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
    assert(SUCCEEDED(DwmFlush()));
    RECT bounds{};
    assert(GetWindowRect(window, &bounds));
    const auto width = bounds.right - bounds.left;
    const auto height = bounds.bottom - bounds.top;
    const auto screen = GetDC(nullptr);
    const auto memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *pixels = nullptr;
    const auto bitmap =
        CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    assert(bitmap && pixels);
    const auto old = SelectObject(memory, bitmap);
    // Capture the HWND surface, independently of other windows covering it.
    assert(PrintWindow(window, memory, 2));
    GdiFlush();
    CComPtr<IWICBitmap> captured;
    assert(SUCCEEDED(factory->CreateBitmapFromMemory(
        width, height, GUID_WICPixelFormat32bppBGR, width * 4,
        width * height * 4, static_cast<BYTE *>(pixels), &captured)));
    if (!directory.empty()) {
        save(factory, captured, directory / L"candidates-native.png");
    }
    std::printf("Native colored pixels = %zu\n", coloredPixels(captured));
    std::fflush(stdout);
    assert(coloredPixels(captured) > 30);
    std::vector<BYTE> actual(width * height * 4);
    assert(SUCCEEDED(captured->CopyPixels(
        nullptr, width * 4, static_cast<UINT>(actual.size()), actual.data())));
    const auto scale = GetDpiForWindow(window) / 96.f;
    const auto at = (static_cast<size_t>(50 * scale) * width + width - 8) * 4;
    const auto highlight = GetSysColor(COLOR_HIGHLIGHT);
    assert(actual[at] == GetBValue(highlight) &&
           actual[at + 1] == GetGValue(highlight) &&
           actual[at + 2] == GetRValue(highlight));
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    std::printf("Native candidate: %ld x %ld pixels, DPI %u\n", width, height,
                GetDpiForWindow(window));
    std::vector<double> elapsed;
    for (size_t i = 0; i < 100; ++i) {
        reply.selected = i % reply.candidates.size();
        const auto start = std::chrono::steady_clock::now();
        view.show(reply, nullptr, {240, 240, 241, 280});
        UpdateWindow(window);
        elapsed.push_back(std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - start)
                              .count());
    }
    std::sort(elapsed.begin(), elapsed.end());
    std::printf(
        "Warm show + paint (100 selection updates): P50 %.3f ms, P95 %.3f ms\n",
        elapsed[50], elapsed[95]);
    const auto deadline = GetTickCount64() + 5000;
    while (GetTickCount64() < deadline) {
        pump();
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
    }
    view.hide();
    assert(SetThreadDpiAwarenessContext(previous));
}
} // namespace

int main(int argc, char **argv) {
    assert(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    {
        CComPtr<IWICImagingFactory> factory;
        assert(SUCCEEDED(factory.CoCreateInstance(CLSID_WICImagingFactory)));
        const auto directory =
            argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{};
        if (!directory.empty()) {
            std::filesystem::create_directories(directory);
        }
        renderTests(factory, directory);
        windowTests();
        if (!directory.empty()) {
            preview(factory, directory);
        }
    }
    CoUninitialize();
    std::puts("Candidate rendering and window tests passed.");
}
