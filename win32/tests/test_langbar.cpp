#include "../dll/util.h"
#include "../tsf/langbaritem.h"
#include <cassert>
#include <olectl.h>
#include <vector>

namespace {
LONG dllReferences = 0;

class UpdateSink : public ITfLangBarItemSink {
  public:
    STDMETHODIMP QueryInterface(REFIID id, void **result) override {
        if (!result)
            return E_INVALIDARG;
        *result = nullptr;
        if (id != IID_IUnknown && id != IID_ITfLangBarItemSink)
            return E_NOINTERFACE;
        *result = static_cast<ITfLangBarItemSink *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references; }
    STDMETHODIMP_(ULONG) Release() override { return --references; }
    STDMETHODIMP OnUpdate(DWORD flags) override {
        ++updates;
        lastFlags = flags;
        return S_OK;
    }
    ULONG references = 1;
    unsigned int updates = 0;
    DWORD lastFlags = 0;
};

std::vector<DWORD> iconPixels(ITfLangBarItemButton *item) {
    HICON icon = nullptr;
    assert(item->GetIcon(&icon) == S_OK && icon);
    ICONINFO info{};
    assert(GetIconInfo(icon, &info) && !info.hbmColor);
    BITMAP mask{};
    assert(GetObjectW(info.hbmMask, sizeof(mask), &mask));
    const auto width = mask.bmWidth;
    const auto height = mask.bmHeight / 2;
    assert(width >= 16 && height == width);
    DeleteObject(info.hbmMask);

    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = width;
    bitmap.bmiHeader.biHeight = -height;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    void *bits = nullptr;
    auto image =
        CreateDIBSection(dc, &bitmap, DIB_RGB_COLORS, &bits, nullptr, 0);
    assert(dc && image && bits);
    const auto oldImage = SelectObject(dc, image);
    PatBlt(dc, 0, 0, width, height, WHITENESS);
    assert(DrawIconEx(dc, 0, 0, icon, width, height, 0, nullptr, DI_NORMAL));
    GdiFlush();
    const auto pixels = static_cast<const DWORD *>(bits);
    std::vector<DWORD> result(pixels, pixels + width * height);
    size_t black = 0, white = 0;
    for (auto pixel : result) {
        black += (pixel & 0xffffff) == 0;
        white += (pixel & 0xffffff) == 0xffffff;
    }
    assert(black > 0 && white > 0 && black + white == result.size());
    SelectObject(dc, oldImage);
    DeleteObject(image);
    DeleteDC(dc);
    DestroyIcon(icon);
    return result;
}
} // namespace

void DllAddRef() { ++dllReferences; }
void DllRelease() { --dllReferences; }

int main() {
    using fcitx::LangBarItem;
    CComPtr<LangBarItem> item;
    item.Attach(new LangBarItem(nullptr, true));
    assert(dllReferences == 1);
    CComPtr<ITfSource> source;
    assert(item.QueryInterface(&source) == S_OK);
    CComPtr<IUnknown> buttonIdentity, sourceIdentity;
    assert(item.QueryInterface(&buttonIdentity) == S_OK);
    assert(source.QueryInterface(&sourceIdentity) == S_OK);
    assert(buttonIdentity == sourceIdentity);
    buttonIdentity.Release();
    sourceIdentity.Release();
    assert(item->GetInfo(nullptr) == E_INVALIDARG);
    assert(item->GetStatus(nullptr) == E_INVALIDARG);
    assert(item->GetText(nullptr) == E_INVALIDARG);
    assert(item->GetTooltipString(nullptr) == E_INVALIDARG);
    assert(item->GetIcon(nullptr) == E_INVALIDARG);
    TF_LANGBARITEMINFO info{};
    assert(item->GetInfo(&info) == S_OK);
    assert(info.clsidService == fcitx::FCITX_CLSID);
    assert(info.guidItem == GUID_LBI_INPUTMODE);
    assert(info.dwStyle & TF_LBI_STYLE_TEXTCOLORICON);
    UpdateSink sink;
    DWORD cookie = 0, duplicate = 0;
    assert(source->AdviseSink(IID_ITfLangBarItemSink, &sink, &cookie) == S_OK);
    assert(cookie != TF_INVALID_COOKIE && sink.references == 2);
    assert(source->AdviseSink(IID_ITfLangBarItemSink, &sink, &duplicate) ==
           CONNECT_E_ADVISELIMIT);
    assert(duplicate == TF_INVALID_COOKIE);
    const auto chineseIcon = iconPixels(item);
    item.p->setMode(false);
    assert(sink.updates == 1 &&
           sink.lastFlags == (TF_LBI_BTNALL | TF_LBI_STATUS));
    CComBSTR text, tooltip;
    assert(item->GetText(&text) == S_OK && text == L"A");
    assert(item->GetTooltipString(&tooltip) == S_OK &&
           tooltip == L"Fcitx5 Pinyin - English input");
    assert(chineseIcon != iconPixels(item));
    item.p->setMode(false);
    assert(sink.updates == 1);
    assert(item->Show(FALSE) == S_OK);
    DWORD status = 0;
    assert(item->GetStatus(&status) == S_OK && (status & TF_LBI_STATUS_HIDDEN));
    assert(source->UnadviseSink(cookie + 1) == CONNECT_E_NOCONNECTION);
    assert(source->UnadviseSink(cookie) == S_OK && sink.references == 1);
    DWORD newCookie = 0;
    assert(source->AdviseSink(IID_ITfLangBarItemSink, &sink, &newCookie) ==
           S_OK);
    assert(newCookie != cookie);
    assert(source->UnadviseSink(cookie) == CONNECT_E_NOCONNECTION);
    item.p->detach();
    assert(sink.references == 1);
    assert(item->OnClick(TF_LBI_CLK_LEFT, {}, nullptr) == S_OK);
    source.Release();
    item.Release();
    assert(dllReferences == 0);
}
