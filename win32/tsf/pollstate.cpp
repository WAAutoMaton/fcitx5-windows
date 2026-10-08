#include "tsf.h"

namespace fcitx {

bool Tsf::initMessageWindow() {
    HINSTANCE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&messageWindowProc), &module);
    WNDCLASSW windowClass{};
    windowClass.hInstance = module;
    windowClass.lpfnWndProc = messageWindowProc;
    windowClass.lpszClassName = L"Fcitx5WindowsDispatchV2";
    if (!RegisterClassW(&windowClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    messageWindow_ = CreateWindowExW(0, windowClass.lpszClassName, L"", 0, 0, 0,
                                     0, 0, HWND_MESSAGE, nullptr, module, this);
    return messageWindow_ && SetTimer(messageWindow_, 1, 100, nullptr);
}

LRESULT CALLBACK Tsf::messageWindowProc(HWND window, UINT message,
                                        WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto *creation = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    }
    auto *self =
        reinterpret_cast<Tsf *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self && message == WM_TIMER) {
        self->AddRef();
        self->pollState();
        self->Release();
        return 0;
    }
    if (self && message == WM_APP + 1) {
        self->AddRef();
        self->requestNextEdit(false);
        self->Release();
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void Tsf::pollState() {
    if (!textEditSinkContext_ || !foreground_ || editRequested_) {
        return;
    }
    if (!pipe_.connected()) {
        if (GetTickCount64() < nextReconnect_) {
            return;
        }
        nextReconnect_ = GetTickCount64() + 2000;
        if (!pipe_.connect()) {
            return;
        }
    }
    if (!remoteContextId_ && !initRemoteContext()) {
        return;
    }
    PipeClient::KeyReply reply;
    if (!pipe_.poll(remoteContextId_, reply)) {
        clearRemoteContext();
        pipe_.disconnect();
        nextReconnect_ = GetTickCount64() + 2000;
        return;
    }
    if (!reply.commit.empty() || !reply.preedit.empty() || composition_ ||
        !reply.candidates.empty() || candidates_.visible()) {
        submitSnapshot(std::move(reply), false);
    } else {
        state_ = std::move(reply);
    }
}

} // namespace fcitx
