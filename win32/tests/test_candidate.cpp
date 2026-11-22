#include "../tsf/candidatewindow.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <dwmapi.h>
#include <dxgi.h>
#include <filesystem>
#include <wincodec.h>

namespace fcitx {
struct CandidateWindowTestAccess {
    static HWND window(const CandidateWindow &view) { return view.window_; }
    static IDWriteTextLayout *text(const CandidateWindow &view, size_t index) {
        return view.rows_[index].text.layout;
    }
    static D2D1_RECT_F bounds(const CandidateWindow &view, size_t index) {
        return view.placements_[index].bounds;
    }
    static float width(const CandidateWindow &view) { return view.width_; }
    static float height(const CandidateWindow &view) { return view.height_; }
    static float widthLimit(const CandidateWindow &view) {
        return view.layoutWidthLimit_;
    }
    static float textLeft(const CandidateWindow &view, size_t index) {
        return view.placements_[index].textLeft;
    }
    static float commentLeft(const CandidateWindow &view, size_t index) {
        return view.placements_[index].commentLeft;
    }
    static bool textTrimmed(const CandidateWindow &view, size_t index) {
        DWRITE_LINE_METRICS line{};
        UINT32 count = 0;
        assert(SUCCEEDED(
            view.rows_[index].text.layout->GetLineMetrics(&line, 1, &count)));
        assert(count == 1);
        return line.isTrimmed != FALSE;
    }
    static bool commentTrimmed(const CandidateWindow &view, size_t index) {
        DWRITE_LINE_METRICS line{};
        UINT32 count = 0;
        assert(SUCCEEDED(view.rows_[index].comment.layout->GetLineMetrics(
            &line, 1, &count)));
        assert(count == 1);
        return line.isTrimmed != FALSE;
    }
    static void discardTarget(CandidateWindow &view) { view.discardTarget(); }
    static void factory(CandidateWindow &view, ID2D1Factory *factory) {
        view.drawingFactory_ = factory;
    }
    static bool usesDc(const CandidateWindow &view) {
        return view.useDcTarget_ && view.dcTarget_ && !view.target_;
    }
    static HRESULT drawDc(CandidateWindow &view, HDC dc, UINT dpi) {
        view.dpi_ = dpi;
        auto result = view.createTarget(dc);
        if (SUCCEEDED(result))
            result = view.draw(view.dcTarget_, view.brush_);
        return result;
    }
    static void loseTarget(CandidateWindow &view) {
        view.finishPaint(D2DERR_RECREATE_TARGET);
    }
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

class FailingHwndFactory : public ID2D1Factory {
  public:
    FailingHwndFactory() {
        assert(SUCCEEDED(
            D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_)));
    }
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (id != IID_IUnknown && id != __uuidof(ID2D1Factory))
            return E_NOINTERFACE;
        *result = static_cast<ID2D1Factory *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override {
        const auto remaining = --references_;
        if (!remaining)
            delete this;
        return remaining;
    }
    STDMETHODIMP ReloadSystemMetrics() noexcept override { return S_OK; }
    STDMETHODIMP_(void) GetDesktopDpi(FLOAT *x, FLOAT *y) noexcept override {
        *x = *y = 96;
    }
    STDMETHODIMP
    CreateRectangleGeometry(const D2D1_RECT_F *rect,
                            ID2D1RectangleGeometry **out) noexcept override {
        return factory_->CreateRectangleGeometry(rect, out);
    }
    STDMETHODIMP CreateRoundedRectangleGeometry(
        const D2D1_ROUNDED_RECT *rect,
        ID2D1RoundedRectangleGeometry **out) noexcept override {
        return factory_->CreateRoundedRectangleGeometry(rect, out);
    }
    STDMETHODIMP
    CreateEllipseGeometry(const D2D1_ELLIPSE *ellipse,
                          ID2D1EllipseGeometry **out) noexcept override {
        return factory_->CreateEllipseGeometry(ellipse, out);
    }
    STDMETHODIMP
    CreateGeometryGroup(D2D1_FILL_MODE mode, ID2D1Geometry **items,
                        UINT32 count,
                        ID2D1GeometryGroup **out) noexcept override {
        return factory_->CreateGeometryGroup(mode, items, count, out);
    }
    STDMETHODIMP
    CreateTransformedGeometry(
        ID2D1Geometry *geometry, const D2D1_MATRIX_3X2_F *transform,
        ID2D1TransformedGeometry **out) noexcept override {
        return factory_->CreateTransformedGeometry(geometry, transform, out);
    }
    STDMETHODIMP CreatePathGeometry(ID2D1PathGeometry **out) noexcept override {
        return factory_->CreatePathGeometry(out);
    }
    STDMETHODIMP
    CreateStrokeStyle(const D2D1_STROKE_STYLE_PROPERTIES *properties,
                      const FLOAT *dashes, UINT32 count,
                      ID2D1StrokeStyle **out) noexcept override {
        return factory_->CreateStrokeStyle(properties, dashes, count, out);
    }
    STDMETHODIMP
    CreateDrawingStateBlock(const D2D1_DRAWING_STATE_DESCRIPTION *description,
                            IDWriteRenderingParams *params,
                            ID2D1DrawingStateBlock **out) noexcept override {
        return factory_->CreateDrawingStateBlock(description, params, out);
    }
    STDMETHODIMP
    CreateWicBitmapRenderTarget(IWICBitmap *bitmap,
                                const D2D1_RENDER_TARGET_PROPERTIES *properties,
                                ID2D1RenderTarget **out) noexcept override {
        return factory_->CreateWicBitmapRenderTarget(bitmap, properties, out);
    }
    STDMETHODIMP
    CreateHwndRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES *,
                           const D2D1_HWND_RENDER_TARGET_PROPERTIES *,
                           ID2D1HwndRenderTarget **out) noexcept override {
        ++hwndAttempts;
        *out = nullptr;
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }
    STDMETHODIMP CreateDxgiSurfaceRenderTarget(
        IDXGISurface *surface, const D2D1_RENDER_TARGET_PROPERTIES *properties,
        ID2D1RenderTarget **out) noexcept override {
        return factory_->CreateDxgiSurfaceRenderTarget(surface, properties,
                                                       out);
    }
    STDMETHODIMP
    CreateDCRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES *properties,
                         ID2D1DCRenderTarget **out) noexcept override {
        ++dcAttempts;
        *out = nullptr;
        return FAILED(dcResult)
                   ? dcResult
                   : factory_->CreateDCRenderTarget(properties, out);
    }
    unsigned hwndAttempts = 0;
    unsigned dcAttempts = 0;
    HRESULT dcResult = S_OK;

  private:
    ULONG references_ = 1;
    CComPtr<ID2D1Factory> factory_;
};

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

RECT dipRectToPixels(const D2D1_RECT_F &bounds, UINT dpi, LONG inset = 1) {
    return {static_cast<LONG>(std::ceil(bounds.left * dpi / 96.f)) + inset,
            static_cast<LONG>(std::ceil(bounds.top * dpi / 96.f)) + inset,
            static_cast<LONG>(std::floor(bounds.right * dpi / 96.f)) - inset,
            static_cast<LONG>(std::floor(bounds.bottom * dpi / 96.f)) - inset};
}

size_t matchingPixels(IWICBitmapSource *bitmap, const RECT &region,
                      COLORREF expected, bool invert = false) {
    UINT width = 0, height = 0;
    assert(SUCCEEDED(bitmap->GetSize(&width, &height)));
    std::vector<BYTE> pixels(width * height * 4);
    assert(SUCCEEDED(bitmap->CopyPixels(
        nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())));
    const auto left = std::clamp(region.left, 0L, static_cast<LONG>(width));
    const auto top = std::clamp(region.top, 0L, static_cast<LONG>(height));
    const auto right = std::clamp(region.right, left, static_cast<LONG>(width));
    const auto bottom =
        std::clamp(region.bottom, top, static_cast<LONG>(height));
    size_t count = 0;
    for (LONG y = top; y < bottom; ++y) {
        for (LONG x = left; x < right; ++x) {
            const auto offset =
                (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 4;
            const auto color =
                RGB(pixels[offset + 2], pixels[offset + 1], pixels[offset]);
            count += invert ? (color != expected) : (color == expected);
        }
    }
    return count;
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
        // Production show paints before returning; no message pump is needed.
        assert(!GetUpdateRect(window, nullptr, FALSE));
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
        assert(view.update(reply));
        assert(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),
                                            context));
        Access::loseTarget(view);
        assert(view.visible());
        SendMessageW(window, WM_TIMER, 1, 0);
        assert(view.visible());
        Access::loseTarget(view);
        Access::loseTarget(view);
        Access::loseTarget(view);
        assert(!view.visible());
        assert(!view.update(reply));
        view.show(reply, owner, anchor);
        assert(view.visible());
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

void fallbackTests(IWICImagingFactory *factory) {
    const auto previous = SetThreadDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    for (const auto layout : {fcitx::win32::ipc::CandidateLayout::Vertical,
                              fcitx::win32::ipc::CandidateLayout::Horizontal}) {
        const bool horizontal =
            layout == fcitx::win32::ipc::CandidateLayout::Horizontal;
        for (const UINT dpi : {96u, 192u, 288u}) {
            CComPtr<FailingHwndFactory> failing;
            failing.Attach(new FailingHwndFactory);
            fcitx::CandidateWindow view;
            Access::factory(view, failing);
            auto initialReply = fixture();
            initialReply.candidateLayout = layout;
            view.show(initialReply, nullptr, {100, 100, 101, 120});
            assert(view.visible() && Access::usesDc(view));
            assert(failing->hwndAttempts == 1 && failing->dcAttempts == 1);
            if (horizontal) {
                assert(Access::bounds(view, 1).top ==
                       Access::bounds(view, 0).top);
            }
            const auto window = Access::window(view);
            RECT client{};
            assert(GetClientRect(window, &client));
            const auto dc = CreateCompatibleDC(nullptr);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = client.right;
            info.bmiHeader.biHeight = -client.bottom;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            void *pixels = nullptr;
            const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS,
                                                 &pixels, nullptr, 0);
            assert(dc && bitmap && pixels);
            const auto old = SelectObject(dc, bitmap);
            assert(SUCCEEDED(Access::drawDc(view, dc, dpi)));
            GdiFlush();
            CComPtr<IWICBitmap> rendered;
            assert(SUCCEEDED(factory->CreateBitmapFromMemory(
                client.right, client.bottom, GUID_WICPixelFormat32bppBGR,
                client.right * 4, client.right * client.bottom * 4,
                static_cast<BYTE *>(pixels), &rendered)));
            assert(coloredPixels(rendered) > 30);
            const auto *bytes = static_cast<const BYTE *>(pixels);
            size_t textPixels = 0, selectedPixels = 0;
            for (LONG i = 0; i < client.right * client.bottom; ++i) {
                const auto color =
                    RGB(bytes[i * 4 + 2], bytes[i * 4 + 1], bytes[i * 4]);
                textPixels += color == GetSysColor(COLOR_WINDOWTEXT);
                selectedPixels += color == GetSysColor(COLOR_HIGHLIGHT);
            }
            assert(textPixels > 30 && selectedPixels > 100);
            // Bind back to the window before releasing the temporary DC.
            InvalidateRect(window, nullptr, FALSE);
            UpdateWindow(window);
            SelectObject(dc, old);
            DeleteObject(bitmap);
            DeleteDC(dc);
            assert(view.visible());
            auto reply = fixture();
            reply.candidateLayout = layout;
            reply.candidates.resize(2);
            view.show(reply, nullptr, {200, 200, 201, 220});
            assert(view.visible() && Access::usesDc(view));
            assert(failing->dcAttempts == 1);
            if (horizontal) {
                assert(Access::bounds(view, 1).top ==
                       Access::bounds(view, 0).top);
            }
            Access::discardTarget(view);
            InvalidateRect(window, nullptr, FALSE);
            UpdateWindow(window);
            assert(view.visible() && Access::usesDc(view));
            assert(failing->hwndAttempts == 1 && failing->dcAttempts == 2);
            std::printf("DC fallback %s DPI %u: text pixels = %zu, selected "
                        "pixels = %zu\n",
                        horizontal ? "horizontal" : "vertical", dpi, textPixels,
                        selectedPixels);
        }
    }
    CComPtr<FailingHwndFactory> failing;
    failing.Attach(new FailingHwndFactory);
    failing->dcResult = E_OUTOFMEMORY;
    fcitx::CandidateWindow view;
    Access::factory(view, failing);
    view.show(fixture(), nullptr, {100, 100, 101, 120});
    assert(!view.visible());
    failing->dcResult = S_OK;
    view.show(fixture(), nullptr, {100, 100, 101, 120});
    assert(view.visible() && Access::usesDc(view));
    assert(failing->hwndAttempts == 1 && failing->dcAttempts == 2);
    assert(SetThreadDpiAwarenessContext(previous));
}

void inputSiteWindowTests() {
    const auto initial = GetThreadDpiAwarenessContext();
    for (const auto ownerContext :
         {DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE,
          DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2}) {
        assert(SetThreadDpiAwarenessContext(ownerContext));
        const auto parent =
            CreateWindowExW(0, L"STATIC", L"Input site test parent",
                            WS_OVERLAPPEDWINDOW, 100, 100, 600, 400, nullptr,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
        assert(parent);
        ShowWindow(parent, SW_SHOWNOACTIVATE);
        // Terminal's input site is visible but has no client area.
        const auto owner = CreateWindowExW(
            0, L"STATIC", L"Input site test owner", WS_CHILD | WS_VISIBLE, 0, 0,
            0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        assert(owner && IsWindowVisible(owner));
        RECT client{};
        assert(GetClientRect(owner, &client));
        assert(client.right == 0 && client.bottom == 0);
        POINT point{40, 60};
        assert(ClientToScreen(parent, &point));
        const RECT anchor{point.x, point.y, point.x + 1, point.y + 20};
        POINT converted{anchor.left, anchor.top};
        assert(!LogicalToPhysicalPointForPerMonitorDPI(owner, &converted));
        for (const auto callerContext :
             {DPI_AWARENESS_CONTEXT_UNAWARE, DPI_AWARENESS_CONTEXT_SYSTEM_AWARE,
              DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2}) {
            assert(SetThreadDpiAwarenessContext(callerContext));
            const auto foreground = GetForegroundWindow();
            fcitx::CandidateWindow view;
            auto reply = fixture();
            view.show(reply, owner, anchor);
            assert(view.visible());
            assert(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),
                                                callerContext));
            assert(GetForegroundWindow() == foreground);
            const auto window = Access::window(view);
            assert(!GetUpdateRect(window, nullptr, FALSE));
            assert(SetThreadDpiAwarenessContext(
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
            RECT bounds{};
            assert(GetWindowRect(window, &bounds));
            assert(bounds.left == anchor.left && bounds.top == anchor.bottom);
            assert(SetThreadDpiAwarenessContext(callerContext));
            reply.selected = 2;
            view.show(reply, owner, anchor);
            assert(view.visible() && view.update(reply));
            ShowWindow(parent, SW_HIDE);
            view.show(reply, owner, anchor);
            assert(!view.visible());
            ShowWindow(parent, SW_SHOWNOACTIVATE);
            view.show(reply, owner, anchor);
            assert(view.visible());
        }
        assert(DestroyWindow(parent));
    }
    assert(SetThreadDpiAwarenessContext(initial));
}

void childExtentWindowTest() {
    const auto context = DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;
    const auto previous = SetThreadDpiAwarenessContext(context);
    assert(previous);
    const auto parent = CreateWindowExW(
        0, L"STATIC", L"Candidate test parent", WS_OVERLAPPEDWINDOW, 100, 100,
        600, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(parent);
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    const auto owner = CreateWindowExW(
        0, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE, 20, 20, 300, 25, parent,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(owner && IsWindowVisible(owner));

    POINT start{2, 0};
    assert(ClientToScreen(owner, &start));
    RECT anchor{start.x, start.y, start.x + 12, start.y + 25};
    fcitx::CandidateWindow view;
    const auto reply = fixture();
    view.show(reply, owner, anchor);
    assert(view.visible());

    // Everything can report unclipped text two pixels below the edit window.
    anchor.bottom += 2;
    POINT childEnd{anchor.right, anchor.bottom};
    assert(!LogicalToPhysicalPointForPerMonitorDPI(owner, &childEnd));
    POINT physical{anchor.left, anchor.bottom};
    assert(LogicalToPhysicalPointForPerMonitorDPI(parent, &physical));
    view.show(reply, owner, anchor);
    assert(view.visible());
    assert(
        AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), context));
    const auto popup = Access::window(view);
    assert(GetWindowLongPtrW(popup, GWLP_HWNDPARENT) ==
           reinterpret_cast<LONG_PTR>(owner));
    assert(SetThreadDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    RECT bounds{};
    assert(GetWindowRect(popup, &bounds));
    assert(bounds.left == physical.x && bounds.top == physical.y);
    assert(SetThreadDpiAwarenessContext(context));

    RECT parentBounds{};
    assert(GetWindowRect(parent, &parentBounds));
    anchor.bottom = parentBounds.bottom + 2;
    view.show(reply, owner, anchor);
    assert(!view.visible());
    assert(DestroyWindow(parent));
    assert(SetThreadDpiAwarenessContext(previous));
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

    // Also capture native HWND output for horizontal layout.
    reply.candidateLayout = fcitx::win32::ipc::CandidateLayout::Horizontal;
    reply.selected = 1;
    view.show(reply, nullptr, {240, 240, 241, 280});
    UpdateWindow(window);
    assert(view.visible());
    const auto horizontalPresentation = GetTickCount64() + 300;
    while (GetTickCount64() < horizontalPresentation) {
        pump();
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
    assert(SUCCEEDED(DwmFlush()));
    RECT hBounds{};
    assert(GetWindowRect(window, &hBounds));
    const auto hWidth = hBounds.right - hBounds.left;
    const auto hHeight = hBounds.bottom - hBounds.top;
    const auto hScreen = GetDC(nullptr);
    const auto hMemory = CreateCompatibleDC(hScreen);
    BITMAPINFO hInfo{};
    hInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    hInfo.bmiHeader.biWidth = hWidth;
    hInfo.bmiHeader.biHeight = -hHeight;
    hInfo.bmiHeader.biPlanes = 1;
    hInfo.bmiHeader.biBitCount = 32;
    void *hPixels = nullptr;
    const auto hBitmap =
        CreateDIBSection(hScreen, &hInfo, DIB_RGB_COLORS, &hPixels, nullptr, 0);
    assert(hBitmap && hPixels);
    const auto hOld = SelectObject(hMemory, hBitmap);
    assert(PrintWindow(window, hMemory, 2));
    GdiFlush();
    CComPtr<IWICBitmap> hCaptured;
    assert(SUCCEEDED(factory->CreateBitmapFromMemory(
        hWidth, hHeight, GUID_WICPixelFormat32bppBGR, hWidth * 4,
        hWidth * hHeight * 4, static_cast<BYTE *>(hPixels), &hCaptured)));
    if (!directory.empty()) {
        save(factory, hCaptured,
             directory / L"candidates-horizontal-native.png");
    }
    std::printf("Native horizontal colored pixels = %zu\n",
                coloredPixels(hCaptured));
    assert(coloredPixels(hCaptured) > 30);
    const auto hDpi = GetDpiForWindow(window);
    auto selectedTopStrip = dipRectToPixels(Access::bounds(view, 1), hDpi, 2);
    selectedTopStrip.bottom =
        std::min(selectedTopStrip.bottom, selectedTopStrip.top + 3);
    auto unselectedTopStrip = dipRectToPixels(Access::bounds(view, 0), hDpi, 2);
    unselectedTopStrip.bottom =
        std::min(unselectedTopStrip.bottom, unselectedTopStrip.top + 3);
    assert(matchingPixels(hCaptured, selectedTopStrip, highlight) > 0);
    if (highlight != GetSysColor(COLOR_WINDOW)) {
        assert(matchingPixels(hCaptured, unselectedTopStrip, highlight) == 0);
    }
    SelectObject(hMemory, hOld);
    DeleteObject(hBitmap);
    DeleteDC(hMemory);
    ReleaseDC(nullptr, hScreen);
    std::printf("Native horizontal candidate: %ld x %ld pixels, DPI %u\n",
                hWidth, hHeight, hDpi);
    reply.candidateLayout = fcitx::win32::ipc::CandidateLayout::Vertical;
    view.show(reply, nullptr, {240, 240, 241, 280});
    UpdateWindow(window);

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
void horizontalLayoutTests(IWICImagingFactory *factory,
                           const std::filesystem::path &directory) {
    fcitx::CandidateWindow view;
    // 1. Short candidates stay compact without forcing vertical minimum width.
    Reply compact;
    compact.candidateLayout = fcitx::win32::ipc::CandidateLayout::Horizontal;
    compact.selected = 0;
    compact.candidates = {{"\xe4\xbd\xa0\xe5\xa5\xbd", "1", ""}};
    Access::prepare(view, compact, 96);
    assert(Access::width(view) < 120.f);
    assert(Access::bounds(view, 0).left > 0.f &&
           Access::bounds(view, 0).right < Access::width(view));

    // 2. Single-row overflow uses water-filling: short candidates stay intact
    // while oversized candidates are ellipsized on the same row (no wrapping).
    Reply reply;
    reply.candidates = {{"a", "1", "a"},
                        {"b", "2", ""},
                        {std::string(200, 'W'), "3", "long comment"},
                        {"c", "4", ""}};
    reply.selected = 1;
    Access::prepare(view, reply, 96);
    const auto first = Access::bounds(view, 0);
    assert(Access::bounds(view, 1).top == first.bottom);
    CComPtr<IDWriteTextLayout> text = Access::text(view, 0);
    reply.candidateLayout = fcitx::win32::ipc::CandidateLayout::Horizontal;
    Access::prepare(view, reply, 96);
    const auto b0 = Access::bounds(view, 0);
    const auto b1 = Access::bounds(view, 1);
    const auto b2 = Access::bounds(view, 2);
    const auto b3 = Access::bounds(view, 3);
    assert(b1.top == b0.top && b1.bottom == b0.bottom && b1.left == b0.right);
    assert(b2.top == b0.top && b2.bottom == b0.bottom && b2.left == b1.right);
    assert(b3.top == b0.top && b3.bottom == b0.bottom && b3.left == b2.right);
    assert(b3.right < Access::width(view));
    assert(Access::width(view) <= Access::widthLimit(view) + .01f);
    assert(!Access::textTrimmed(view, 0) && !Access::commentTrimmed(view, 0));
    assert(!Access::textTrimmed(view, 1));
    assert(Access::textTrimmed(view, 2));
    assert(!Access::textTrimmed(view, 3));
    assert(Access::text(view, 0) == text.p);
    auto overflowBitmap = Access::render(view, factory);
    assert(overflowBitmap);
    if (!directory.empty()) {
        save(factory, overflowBitmap,
             directory / L"candidates-horizontal-overflow.png");
    }

    // 3. Multiple wide items share the cap equally on one row.
    Reply multiWide;
    multiWide.candidateLayout = fcitx::win32::ipc::CandidateLayout::Horizontal;
    multiWide.selected = 2;
    multiWide.candidates = {{"\xe7\x9f\xad", "1", ""},
                            {std::string(80, 'A'), "2", ""},
                            {std::string(80, 'B'), "3", "comment"},
                            {std::string(80, 'C'), "4", ""},
                            {"\xe5\xae\x8c", "5", ""}};
    for (const UINT dpi : {96u, 192u, 288u}) {
        Access::prepare(view, multiWide, dpi);
        const auto rowTop = Access::bounds(view, 0).top;
        const auto rowBottom = Access::bounds(view, 0).bottom;
        for (size_t i = 0; i < multiWide.candidates.size(); ++i) {
            const auto current = Access::bounds(view, i);
            assert(current.top == rowTop && current.bottom == rowBottom);
            if (i > 0) {
                assert(std::fabs(current.left -
                                 Access::bounds(view, i - 1).right) < .01f);
            }
        }
        assert(Access::width(view) <= Access::widthLimit(view) + .01f);
        assert(!Access::textTrimmed(view, 0) && !Access::textTrimmed(view, 4));
        const auto w1 =
            Access::bounds(view, 1).right - Access::bounds(view, 1).left;
        const auto w2 =
            Access::bounds(view, 2).right - Access::bounds(view, 2).left;
        const auto w3 =
            Access::bounds(view, 3).right - Access::bounds(view, 3).left;
        assert(std::fabs(w1 - w2) < .5f && std::fabs(w2 - w3) < .5f);
        assert(Access::render(view, factory));
    }

    // 4. Short body with long comment in a squeezed horizontal cell keeps
    // comment tight against the body rather than reserving empty space.
    Reply shortBodyLongComment;
    shortBodyLongComment.candidateLayout =
        fcitx::win32::ipc::CandidateLayout::Horizontal;
    shortBodyLongComment.selected = 0;
    for (int i = 0; i < 6; ++i) {
        shortBodyLongComment.candidates.push_back(
            {"\xe5\xa5\xbd", std::to_string(i + 1),
             "a very long dictionary comment for horizontal squeezing test"});
    }
    Access::prepare(view, shortBodyLongComment, 288);
    assert(Access::bounds(view, 5).top == Access::bounds(view, 0).top);
    assert(Access::commentLeft(view, 0) - Access::textLeft(view, 0) < 40.f);
    assert(Access::render(view, factory));

    // 5. Realistic wide horizontal page stays on one row without wrapping,
    // renders color emoji, and isolates selection highlight to the selected
    // cell across DPIs.
    auto horizontalFixture = fixture();
    horizontalFixture.candidateLayout =
        fcitx::win32::ipc::CandidateLayout::Horizontal;
    const auto highlight = GetSysColor(COLOR_HIGHLIGHT);
    const auto background = GetSysColor(COLOR_WINDOW);
    for (const UINT dpi : {96u, 192u}) {
        Access::prepare(view, horizontalFixture, dpi);
        for (size_t i = 1; i < horizontalFixture.candidates.size(); ++i) {
            assert(Access::bounds(view, i).top == Access::bounds(view, 0).top);
            assert(Access::bounds(view, i).bottom ==
                   Access::bounds(view, 0).bottom);
        }
        auto bitmap = Access::render(view, factory);
        assert(coloredPixels(bitmap) > 30);
        // Check the top padding strip inside selected and unselected cells so
        // text/emoji strokes do not interfere with highlight color checks.
        auto selectedStrip = dipRectToPixels(Access::bounds(view, 1), dpi, 2);
        selectedStrip.bottom =
            std::min(selectedStrip.bottom, selectedStrip.top + 3);
        assert(matchingPixels(bitmap, selectedStrip, highlight) > 0);
        if (highlight != background) {
            for (const size_t unselected : {size_t{0}, size_t{2}, size_t{3}}) {
                auto strip =
                    dipRectToPixels(Access::bounds(view, unselected), dpi, 2);
                strip.bottom = std::min(strip.bottom, strip.top + 3);
                assert(matchingPixels(bitmap, strip, highlight) == 0);
            }
        }
        if (dpi == 96 && !directory.empty()) {
            save(factory, bitmap,
                 directory / L"candidates-horizontal-fixture.png");
        }
    }

    // 6. Per-item axis-aligned clipping keeps glyphs from bleeding into an
    // adjacent empty horizontal cell even when neighbors are squeezed.
    Reply clipped;
    clipped.candidateLayout = fcitx::win32::ipc::CandidateLayout::Horizontal;
    clipped.selected = UINT32_MAX;
    clipped.candidates = {{std::string(200, 'W'), "1", "long comment"},
                          {"", "", ""},
                          {std::string(200, 'M'), "3", "long comment"}};
    for (const UINT dpi : {96u, 192u}) {
        Access::prepare(view, clipped, dpi);
        assert(Access::bounds(view, 1).top == Access::bounds(view, 0).top);
        assert(Access::bounds(view, 2).top == Access::bounds(view, 0).top);
        auto bitmap = Access::render(view, factory);
        const auto leftCell = dipRectToPixels(Access::bounds(view, 0), dpi, 1);
        const auto middleCell =
            dipRectToPixels(Access::bounds(view, 1), dpi, 1);
        const auto rightCell = dipRectToPixels(Access::bounds(view, 2), dpi, 1);
        assert(matchingPixels(bitmap, leftCell, background, true) > 30);
        assert(matchingPixels(bitmap, rightCell, background, true) > 30);
        assert(matchingPixels(bitmap, middleCell, background, true) == 0);
    }

    // 7. Label-less horizontal items omit the label gap while labeled items
    // reserve it.
    Reply labelCheck;
    labelCheck.candidateLayout = fcitx::win32::ipc::CandidateLayout::Horizontal;
    labelCheck.selected = 0;
    labelCheck.candidates = {{"alpha", "", ""}, {"beta", "2", ""}};
    Access::prepare(view, labelCheck, 96);
    assert(std::fabs(Access::textLeft(view, 0) -
                     (Access::bounds(view, 0).left + 8.f)) < .01f);
    assert(Access::textLeft(view, 1) > Access::bounds(view, 1).left + 12.f);

    // 8. Switching back to vertical restores stacked rows.
    reply.candidateLayout = fcitx::win32::ipc::CandidateLayout::Vertical;
    Access::prepare(view, reply, 96);
    assert(Access::bounds(view, 1).top == Access::bounds(view, 0).bottom);
}

void horizontalWindowTests() {
    const auto initial = GetThreadDpiAwarenessContext();
    Reply compactHorizontal;
    compactHorizontal.candidateLayout =
        fcitx::win32::ipc::CandidateLayout::Horizontal;
    compactHorizontal.selected = 1;
    compactHorizontal.candidates = {{"\xe4\xbd\xa0\xe5\xa5\xbd", "1", "ni hao"},
                                    {"\xe4\xb8\x96\xe7\x95\x8c", "2", ""},
                                    {"\xe6\xb5\x8b\xe8\xaf\x95", "3", ""}};
    for (const auto context :
         {DPI_AWARENESS_CONTEXT_UNAWARE, DPI_AWARENESS_CONTEXT_SYSTEM_AWARE,
          DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2}) {
        assert(SetThreadDpiAwarenessContext(context));
        const auto owner =
            CreateWindowExW(0, L"STATIC", L"Horizontal candidate test owner",
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
        auto reply = compactHorizontal;
        view.show(reply, owner, anchor);
        assert(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),
                                            context));
        assert(view.visible());
        const auto window = Access::window(view);
        assert(!GetUpdateRect(window, nullptr, FALSE));
        assert(AreDpiAwarenessContextsEqual(
            GetWindowDpiAwarenessContext(window),
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
        assert(GetForegroundWindow() == foreground);
        assert(SendMessageW(window, WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATE);
        assert(GetWindowLongPtrW(window, GWLP_HWNDPARENT) ==
               reinterpret_cast<LONG_PTR>(owner));
        assert(Access::bounds(view, 1).top == Access::bounds(view, 0).top &&
               Access::bounds(view, 2).top == Access::bounds(view, 0).top);
        assert(SetThreadDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
        RECT bounds{};
        assert(GetWindowRect(window, &bounds));
        assert(bounds.left == physical.x && bounds.top == physical.y);
        CComPtr<IDWriteTextLayout> previous = Access::text(view, 0);
        assert(SetThreadDpiAwarenessContext(context));
        reply.selected = 2;
        view.show(reply, owner, anchor);
        assert(previous == Access::text(view, 0));
        assert(view.update(reply));
        assert(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),
                                            context));

        // Dynamic switching between vertical and horizontal via update().
        auto full = fixture();
        view.show(full, owner, anchor);
        assert(SetThreadDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
        RECT vertBounds{};
        assert(GetWindowRect(window, &vertBounds));
        assert(SetThreadDpiAwarenessContext(context));
        full.candidateLayout = fcitx::win32::ipc::CandidateLayout::Horizontal;
        assert(view.update(full));
        assert(Access::bounds(view, 5).top == Access::bounds(view, 0).top);
        assert(SetThreadDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
        RECT horizBounds{};
        assert(GetWindowRect(window, &horizBounds));
        assert(horizBounds.bottom - horizBounds.top <
               vertBounds.bottom - vertBounds.top);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        assert(GetMonitorInfoW(
            MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor));
        if (monitor.rcWork.right - monitor.rcWork.left >
            (vertBounds.right - vertBounds.left) + 20) {
            assert(horizBounds.right - horizBounds.left >
                   vertBounds.right - vertBounds.left);
        }
        assert(SetThreadDpiAwarenessContext(context));
        full.candidateLayout = fcitx::win32::ipc::CandidateLayout::Vertical;
        assert(view.update(full));
        assert(SetThreadDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
        RECT restoredBounds{};
        assert(GetWindowRect(window, &restoredBounds));
        assert(restoredBounds.bottom - restoredBounds.top ==
               vertBounds.bottom - vertBounds.top);
        assert(restoredBounds.right - restoredBounds.left ==
               vertBounds.right - vertBounds.left);
        assert(SetThreadDpiAwarenessContext(context));
        DestroyWindow(owner);
        assert(!Access::window(view));
    }

    // Work-area clamping, right/bottom edge flip, and large single-row page.
    assert(SetThreadDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    fcitx::CandidateWindow view;
    MONITORINFO monitor{sizeof(MONITORINFO)};
    RECT anchor{0, 0, 1, 1};
    assert(GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST),
                           &monitor));
    anchor = {monitor.rcWork.right - 2, monitor.rcWork.bottom - 2,
              monitor.rcWork.right - 1, monitor.rcWork.bottom - 1};
    auto wide = fixture();
    wide.candidateLayout = fcitx::win32::ipc::CandidateLayout::Horizontal;
    view.show(wide, nullptr, anchor);
    assert(view.visible());
    RECT bounds{};
    assert(GetWindowRect(Access::window(view), &bounds));
    assert(bounds.left >= monitor.rcWork.left &&
           bounds.right <= monitor.rcWork.right);
    assert(bounds.top >= monitor.rcWork.top && bounds.bottom <= anchor.top);
    assert(Access::bounds(view, 5).top == Access::bounds(view, 0).top);

    auto many = fixture();
    many.candidateLayout = fcitx::win32::ipc::CandidateLayout::Horizontal;
    many.candidates.resize(32, many.candidates.front());
    view.show(many, nullptr, anchor);
    assert(view.visible());
    assert(GetWindowRect(Access::window(view), &bounds));
    assert(bounds.left >= monitor.rcWork.left &&
           bounds.right <= monitor.rcWork.right);
    assert(bounds.top >= monitor.rcWork.top &&
           bounds.bottom <= monitor.rcWork.bottom);
    assert(Access::bounds(view, 31).top == Access::bounds(view, 0).top);
    view.hide();
    assert(SetThreadDpiAwarenessContext(initial));
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
        horizontalLayoutTests(factory, directory);
        windowTests();
        horizontalWindowTests();
        fallbackTests(factory);
        inputSiteWindowTests();
        childExtentWindowTest();
        if (!directory.empty()) {
            preview(factory, directory);
        }
    }
    CoUninitialize();
    std::puts("Candidate rendering and window tests passed.");
}
