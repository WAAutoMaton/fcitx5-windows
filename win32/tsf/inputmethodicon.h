#pragma once
#include <algorithm>
#include <atlcomcli.h>
#include <cstring>
#include <string>
#include <vector>
#include <wincodec.h>

namespace fcitx {
inline HICON loadInputMethodIcon(const std::wstring &path) {
    if (path.empty()) {
        return nullptr;
    }
    CComPtr<IWICImagingFactory> factory;
    CComPtr<IWICBitmapDecoder> decoder;
    CComPtr<IWICBitmapFrameDecode> frame;
    CComPtr<IWICBitmapScaler> scaler;
    CComPtr<IWICFormatConverter> converter;
    const auto size =
        static_cast<UINT>(std::max(16, GetSystemMetrics(SM_CXSMICON)));
    if (FAILED(factory.CoCreateInstance(CLSID_WICImagingFactory)) ||
        FAILED(factory->CreateDecoderFromFilename(
            path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
            &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) ||
        FAILED(factory->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(frame, size, size,
                                  WICBitmapInterpolationModeFant)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(scaler, GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom))) {
        return nullptr;
    }
    std::vector<BYTE> pixels(size * size * 4);
    if (FAILED(converter->CopyPixels(nullptr, size * 4,
                                     static_cast<UINT>(pixels.size()),
                                     pixels.data()))) {
        return nullptr;
    }
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = size;
    bitmap.bmiHeader.biHeight = -static_cast<LONG>(size);
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    auto color =
        CreateDIBSection(nullptr, &bitmap, DIB_RGB_COLORS, &bits, nullptr, 0);
    auto mask = CreateBitmap(size, size, 1, 1, nullptr);
    HICON icon = nullptr;
    if (color && mask && bits) {
        memcpy(bits, pixels.data(), pixels.size());
        ICONINFO info{};
        info.fIcon = TRUE;
        info.hbmColor = color;
        info.hbmMask = mask;
        icon = CreateIconIndirect(&info);
    }
    if (color) {
        DeleteObject(color);
    }
    if (mask) {
        DeleteObject(mask);
    }
    return icon;
}
} // namespace fcitx
