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
    for (const UINT dpi : {96u, 192u, 288u}) {
        CComPtr<FailingHwndFactory> failing;
        failing.Attach(new FailingHwndFactory);
        fcitx::CandidateWindow view;
        Access::factory(view, failing);
        view.show(fixture(), nullptr, {100, 100, 101, 120});
        assert(view.visible() && Access::usesDc(view));
        assert(failing->hwndAttempts == 1 && failing->dcAttempts == 1);
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
        const auto bitmap =
            CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
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
        reply.candidates.resize(2);
        view.show(reply, nullptr, {200, 200, 201, 220});
        assert(view.visible() && Access::usesDc(view));
        assert(failing->dcAttempts == 1);
        Access::discardTarget(view);
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
        assert(view.visible() && Access::usesDc(view));
        assert(failing->hwndAttempts == 1 && failing->dcAttempts == 2);
        std::printf(
            "DC fallback DPI %u: text pixels = %zu, selected pixels = %zu\n",
            dpi, textPixels, selectedPixels);
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
        fallbackTests(factory);
        inputSiteWindowTests();
        if (!directory.empty()) {
            preview(factory, directory);
        }
    }
    CoUninitialize();
    std::puts("Candidate rendering and window tests passed.");
}
