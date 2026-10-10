#pragma once

#include "pipeclient.h"
#include <atlbase.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>

namespace fcitx {

class CandidateWindow {
  public:
    CandidateWindow() = default;
    CandidateWindow(const CandidateWindow &) = delete;
    CandidateWindow &operator=(const CandidateWindow &) = delete;
    ~CandidateWindow();
    // anchor is in the owner's logical screen coordinates (TSF GetTextExt).
    void show(const PipeClient::KeyReply &reply, HWND owner, RECT anchor);
    void hide();
    bool visible() const { return window_ && IsWindowVisible(window_); }

  private:
    friend struct CandidateWindowTestAccess;
    struct Text {
        std::wstring value;
        CComPtr<IDWriteTextLayout> layout;
        float width = 0;
        float height = 0;
        float overhangTop = 0;
    };
    struct Row {
        Text label;
        Text text;
        Text comment;
    };
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam,
                                       LPARAM lParam);
    bool initialize();
    bool createText(Text &text, IDWriteTextFormat *format);
    bool updateRows(const PipeClient::KeyReply &reply);
    bool layout();
    void position();
    bool createTarget();
    void discardTarget();
    HRESULT draw(ID2D1RenderTarget *target, ID2D1SolidColorBrush *brush);
    void paint();
    HWND window_ = nullptr;
    HINSTANCE module_ = nullptr;
    std::vector<Row> rows_;
    uint32_t selected_ = UINT32_MAX;
    RECT anchor_{};
    UINT dpi_ = 96;
    float width_ = 180;
    float height_ = 0;
    float rowHeight_ = 32;
    float textLeft_ = 0;
    float commentLeft_ = 0;
    bool positioning_ = false;
    bool layoutDirty_ = true;
    CComPtr<ID2D1Factory> drawingFactory_;
    CComPtr<IDWriteFactory> textFactory_;
    CComPtr<IDWriteTextFormat> textFormat_;
    CComPtr<IDWriteTextFormat> secondaryFormat_;
    CComPtr<IDWriteInlineObject> ellipsis_;
    CComPtr<ID2D1HwndRenderTarget> target_;
    CComPtr<ID2D1SolidColorBrush> brush_;
};

} // namespace fcitx
