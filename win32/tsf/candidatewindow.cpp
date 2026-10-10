#include "candidatewindow.h"
#include "dpiawareness.h"
#include "textutil.h"
#include <algorithm>
#include <cmath>

namespace fcitx {
namespace {
constexpr wchar_t kWindowClass[] = L"Fcitx5WindowsCandidatesV3";
constexpr float kPadding = 8;
constexpr float kGap = 10;
constexpr float kBorder = 1;

D2D1_COLOR_F systemColor(int index) {
    const auto color = GetSysColor(index);
    return D2D1::ColorF(GetRValue(color) / 255.f, GetGValue(color) / 255.f,
                        GetBValue(color) / 255.f);
}

bool workArea(const RECT &anchor, RECT &work) {
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST),
                         &monitor)) {
        return false;
    }
    work = monitor.rcWork;
    return work.right > work.left && work.bottom > work.top;
}
} // namespace

CandidateWindow::~CandidateWindow() {
    if (window_) {
        DestroyWindow(window_);
    }
    if (module_) {
        UnregisterClassW(kWindowClass, module_);
    }
}

void CandidateWindow::hide() {
    if (window_) {
        ShowWindow(window_, SW_HIDE);
    }
}

bool CandidateWindow::initialize() {
    if (textFormat_) {
        return true;
    }
    if (!drawingFactory_ &&
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 &drawingFactory_))) {
        return false;
    }
    if (!textFactory_ &&
        FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown **>(&textFactory_)))) {
        return false;
    }
    CComPtr<IDWriteTextFormat> primary;
    CComPtr<IDWriteTextFormat> secondary;
    if (FAILED(textFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16, L"zh-CN",
            &primary)) ||
        FAILED(textFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13, L"zh-CN",
            &secondary)) ||
        FAILED(primary->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP)) ||
        FAILED(secondary->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP))) {
        return false;
    }
    CComPtr<IDWriteInlineObject> ellipsis;
    if (FAILED(textFactory_->CreateEllipsisTrimmingSign(primary, &ellipsis))) {
        return false;
    }
    textFormat_ = std::move(primary);
    secondaryFormat_ = std::move(secondary);
    ellipsis_ = std::move(ellipsis);
    return true;
}

bool CandidateWindow::createText(Text &text, IDWriteTextFormat *format) {
    if (FAILED(textFactory_->CreateTextLayout(
            text.value.data(), static_cast<UINT32>(text.value.size()), format,
            100000, 1024, &text.layout))) {
        return false;
    }
    // VS15 requests text presentation; prefer monochrome symbol glyphs.
    for (UINT32 i = 1; i < text.value.size(); ++i) {
        if (text.value[i] != L'\ufe0e') {
            continue;
        }
        auto start = i - 1;
        if (start > 0 && text.value[start] >= 0xdc00 &&
            text.value[start] <= 0xdfff && text.value[start - 1] >= 0xd800 &&
            text.value[start - 1] <= 0xdbff) {
            --start;
        }
        if (FAILED(text.layout->SetFontFamilyName(L"Segoe UI Symbol",
                                                  {start, i - start + 1}))) {
            return false;
        }
    }
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(text.layout->GetMetrics(&metrics)) ||
        FAILED(text.layout->SetMaxHeight(metrics.height))) {
        return false;
    }
    DWRITE_OVERHANG_METRICS overhang{};
    if (FAILED(text.layout->GetOverhangMetrics(&overhang))) {
        return false;
    }
    text.width = metrics.widthIncludingTrailingWhitespace +
                 std::max(0.f, overhang.left) + std::max(0.f, overhang.right);
    text.overhangTop = std::max(0.f, overhang.top);
    text.height =
        metrics.height + text.overhangTop + std::max(0.f, overhang.bottom);
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    return SUCCEEDED(text.layout->SetTrimming(&trimming, ellipsis_));
}

bool CandidateWindow::updateRows(const PipeClient::KeyReply &reply) {
    std::vector<Row> rows(reply.candidates.size());
    bool changed = rows.size() != rows_.size();
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto &source = reply.candidates[i];
        auto &row = rows[i];
        if (!utf8ToWide(source.label, row.label.value) ||
            !utf8ToWide(source.text, row.text.value) ||
            !utf8ToWide(source.comment, row.comment.value)) {
            return false;
        }
        auto update = [&](Text &text, const Text *previous,
                          IDWriteTextFormat *format) {
            if (previous && previous->value == text.value) {
                text = *previous;
                return true;
            }
            changed = true;
            return createText(text, format);
        };
        const auto *previous = i < rows_.size() ? &rows_[i] : nullptr;
        if (!update(row.label, previous ? &previous->label : nullptr,
                    secondaryFormat_) ||
            !update(row.text, previous ? &previous->text : nullptr,
                    textFormat_) ||
            !update(row.comment, previous ? &previous->comment : nullptr,
                    secondaryFormat_)) {
            return false;
        }
    }
    rows_ = std::move(rows);
    layoutDirty_ = layoutDirty_ || changed;
    return true;
}

bool CandidateWindow::layout() {
    RECT work{};
    if (!workArea(anchor_, work)) {
        return false;
    }
    const auto available = (work.right - work.left) * 96.f / dpi_;
    float labelWidth = 0;
    float bodyWidth = 0;
    float commentWidth = 0;
    rowHeight_ = 32;
    for (const auto &row : rows_) {
        labelWidth = std::max(labelWidth, row.label.width);
        bodyWidth = std::max(bodyWidth, row.text.width);
        commentWidth = std::max(commentWidth, row.comment.width);
        rowHeight_ = std::max(
            rowHeight_,
            std::max({row.label.height, row.text.height, row.comment.height}) +
                12);
    }
    labelWidth = std::min(labelWidth, 80.f);
    commentWidth = std::min(commentWidth, 240.f);
    const auto textLeft = kPadding + labelWidth + (labelWidth > 0 ? kGap : 0);
    const auto preferred = textLeft + bodyWidth + kPadding +
                           (commentWidth > 0 ? kGap + commentWidth : 0);
    const auto width = std::min(available, std::clamp(preferred, 180.f, 640.f));
    if (!layoutDirty_ && width_ == width) {
        return true;
    }
    textLeft_ = textLeft;
    width_ = width;
    const auto remaining = std::max(1.f, width_ - textLeft_ - kPadding);
    if (bodyWidth + kGap + commentWidth > remaining) {
        commentWidth =
            std::min(commentWidth, std::max(0.f, remaining * .35f - kGap));
    }
    bodyWidth =
        std::max(1.f, remaining - (commentWidth > 0 ? kGap + commentWidth : 0));
    commentLeft_ = textLeft_ + bodyWidth + kGap;
    for (auto &row : rows_) {
        if (FAILED(row.label.layout->SetMaxWidth(std::max(1.f, labelWidth))) ||
            FAILED(row.text.layout->SetMaxWidth(bodyWidth)) ||
            FAILED(
                row.comment.layout->SetMaxWidth(std::max(1.f, commentWidth)))) {
            return false;
        }
    }
    height_ = rows_.size() * rowHeight_ + 2 * kPadding;
    layoutDirty_ = false;
    return true;
}

void CandidateWindow::position() {
    if (positioning_ || !window_) {
        return;
    }
    RECT work{};
    if (!workArea(anchor_, work)) {
        hide();
        return;
    }
    positioning_ = true;
    // Select the anchor's monitor before measuring, even for DPI-unaware
    // owners.
    if (MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST) !=
        MonitorFromRect(&anchor_, MONITOR_DEFAULTTONEAREST)) {
        SetWindowPos(window_, nullptr,
                     std::clamp(anchor_.left, work.left, work.right - 1),
                     std::clamp(anchor_.top, work.top, work.bottom - 1), 0, 0,
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
    }
    dpi_ = GetDpiForWindow(window_);
    if (!dpi_ || !layout()) {
        positioning_ = false;
        hide();
        return;
    }
    const auto width =
        std::min(static_cast<LONG>(std::ceil(width_ * dpi_ / 96)),
                 work.right - work.left);
    const auto height =
        std::min(static_cast<LONG>(std::ceil(height_ * dpi_ / 96)),
                 work.bottom - work.top);
    const auto left = std::clamp(anchor_.left, work.left, work.right - width);
    auto top = anchor_.bottom;
    if (top + height > work.bottom) {
        top = anchor_.top - height;
    }
    top = std::clamp(top, work.top, work.bottom - height);
    SetWindowPos(window_, HWND_TOPMOST, left, top, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    positioning_ = false;
    InvalidateRect(window_, nullptr, FALSE);
}

void CandidateWindow::show(const PipeClient::KeyReply &reply, HWND owner,
                           RECT anchor) {
    if (reply.candidates.empty() || (owner && !IsWindowVisible(owner))) {
        hide();
        return;
    }
    if (owner) {
        POINT start{anchor.left, anchor.top};
        POINT end{anchor.right, anchor.bottom};
        if (!LogicalToPhysicalPointForPerMonitorDPI(owner, &start) ||
            !LogicalToPhysicalPointForPerMonitorDPI(owner, &end)) {
            hide();
            return;
        }
        anchor = {start.x, start.y, end.x, end.y};
    }
    ScopedDpiAwareness scope(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (!scope.valid() || !initialize() || !updateRows(reply)) {
        hide();
        return;
    }
    anchor_ = anchor;
    selected_ = reply.selected;
    HINSTANCE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&windowProc), &module);
    if (!window_) {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = windowProc;
        windowClass.hInstance = module;
        windowClass.lpszClassName = kWindowClass;
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        if (!RegisterClassW(&windowClass) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return;
        }
        module_ = module;
        window_ =
            CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kWindowClass,
                            L"Fcitx5", WS_POPUP, anchor.left, anchor.top, 1, 1,
                            owner, nullptr, module, this);
        if (!window_) {
            return;
        }
    }
    SetWindowLongPtrW(window_, GWLP_HWNDPARENT,
                      reinterpret_cast<LONG_PTR>(owner));
    position();
}

void CandidateWindow::discardTarget() {
    brush_.Release();
    target_.Release();
}

bool CandidateWindow::createTarget() {
    RECT client{};
    GetClientRect(window_, &client);
    const auto size = D2D1::SizeU(client.right, client.bottom);
    if (target_) {
        target_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        const auto previous = target_->GetPixelSize();
        if ((previous.width == size.width && previous.height == size.height) ||
            SUCCEEDED(target_->Resize(size))) {
            return true;
        }
        discardTarget();
    }
    const auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(),
        static_cast<float>(dpi_), static_cast<float>(dpi_));
    // Keep display refresh waits off the application's TSF thread.
    if (FAILED(drawingFactory_->CreateHwndRenderTarget(
            properties,
            D2D1::HwndRenderTargetProperties(window_, size,
                                             D2D1_PRESENT_OPTIONS_IMMEDIATELY),
            &target_))) {
        return false;
    }
    if (FAILED(target_->CreateSolidColorBrush(systemColor(COLOR_WINDOWTEXT),
                                              &brush_))) {
        discardTarget();
        return false;
    }
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    return true;
}

HRESULT CandidateWindow::draw(ID2D1RenderTarget *target,
                              ID2D1SolidColorBrush *brush) {
    target->BeginDraw();
    target->Clear(systemColor(COLOR_WINDOW));
    const auto size = target->GetSize();
    for (size_t index = 0; index < rows_.size(); ++index) {
        const auto top = kPadding + index * rowHeight_;
        const bool selected = index == selected_;
        if (selected) {
            brush->SetColor(systemColor(COLOR_HIGHLIGHT));
            target->FillRectangle(D2D1::RectF(kBorder, top,
                                              size.width - kBorder,
                                              top + rowHeight_),
                                  brush);
        }
        const auto &row = rows_[index];
        auto draw = [&](const Text &text, float left, int color) {
            if (text.value.empty() || left >= size.width - kPadding) {
                return;
            }
            brush->SetColor(systemColor(color));
            target->DrawTextLayout(
                D2D1::Point2F(left, top + (rowHeight_ - text.height) / 2 +
                                        text.overhangTop),
                text.layout, brush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        };
        const auto color = selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT;
        draw(row.label, kPadding, color);
        draw(row.text, textLeft_, color);
        draw(row.comment, commentLeft_,
             selected ? COLOR_HIGHLIGHTTEXT : COLOR_GRAYTEXT);
    }
    brush->SetColor(systemColor(COLOR_3DSHADOW));
    target->DrawRectangle(
        D2D1::RectF(.5f, .5f, size.width - .5f, size.height - .5f), brush,
        kBorder);
    return target->EndDraw();
}

void CandidateWindow::paint() {
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);
    if (createTarget()) {
        if (FAILED(draw(target_, brush_))) {
            discardTarget();
            hide();
        }
    } else {
        hide();
    }
    EndPaint(window_, &paint);
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
    if (self) {
        switch (message) {
        case WM_PAINT:
            self->paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DPICHANGED:
            self->dpi_ = HIWORD(wParam);
            if (!self->positioning_ && IsWindowVisible(window)) {
                self->position();
            }
            return 0;
        case WM_SIZE:
        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED:
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_DISPLAYCHANGE:
            if (IsWindowVisible(window)) {
                self->position();
            }
            return 0;
        case WM_NCDESTROY:
            self->window_ = nullptr;
            self->discardTarget();
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace fcitx
