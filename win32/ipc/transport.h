#pragma once

#include "protocol.h"
#include <windows.h>

#include <sddl.h>

namespace fcitx::win32::ipc {

inline std::wstring userSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return {};
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<uint8_t> data(size);
    std::wstring result;
    if (size &&
        GetTokenInformation(token, TokenUser, data.data(), size, &size)) {
        LPWSTR sid = nullptr;
        if (ConvertSidToStringSidW(
                reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid, &sid)) {
            result = sid;
            LocalFree(sid);
        }
    }
    CloseHandle(token);
    return result;
}

inline std::wstring pipeName() {
    const auto sid = userSid();
    DWORD session = 0;
    if (sid.empty() || !ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
        return {};
    }
    return LR"(\\.\pipe\fcitx5-windows-v2-)" + sid + L"-" +
           std::to_wstring(session);
}

inline bool transfer(HANDLE pipe, bool write, void *data, DWORD size,
                     DWORD timeout, HANDLE stopEvent = nullptr) {
    OVERLAPPED operation{};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!operation.hEvent) {
        return false;
    }
    DWORD transferred = 0;
    BOOL success = write ? WriteFile(pipe, data, size, &transferred, &operation)
                         : ReadFile(pipe, data, size, &transferred, &operation);
    if (!success && GetLastError() == ERROR_IO_PENDING) {
        HANDLE events[] = {operation.hEvent, stopEvent};
        const auto wait =
            WaitForMultipleObjects(stopEvent ? 2 : 1, events, FALSE, timeout);
        if (wait != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &operation);
            GetOverlappedResult(pipe, &operation, &transferred, TRUE);
            CloseHandle(operation.hEvent);
            return false;
        }
        success = GetOverlappedResult(pipe, &operation, &transferred, FALSE);
    }
    CloseHandle(operation.hEvent);
    return success && (!write || transferred == size);
}

inline bool readFrame(HANDLE pipe, Frame &frame, DWORD timeout,
                      HANDLE stopEvent = nullptr) {
    std::vector<uint8_t> data(kHeaderSize + kMaxPayloadSize);
    OVERLAPPED operation{};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!operation.hEvent) {
        return false;
    }
    DWORD size = 0;
    BOOL success = ReadFile(pipe, data.data(), static_cast<DWORD>(data.size()),
                            &size, &operation);
    if (!success && GetLastError() == ERROR_IO_PENDING) {
        HANDLE events[] = {operation.hEvent, stopEvent};
        if (WaitForMultipleObjects(stopEvent ? 2 : 1, events, FALSE, timeout) ==
            WAIT_OBJECT_0) {
            success = GetOverlappedResult(pipe, &operation, &size, FALSE);
        } else {
            CancelIoEx(pipe, &operation);
            GetOverlappedResult(pipe, &operation, &size, TRUE);
            success = FALSE;
        }
    }
    CloseHandle(operation.hEvent);
    if (!success) {
        return false;
    }
    data.resize(size);
    return decodeFrame(data, frame);
}

inline bool writeFrame(HANDLE pipe, const Frame &frame, DWORD timeout,
                       HANDLE stopEvent = nullptr) {
    if (frame.payload.size() > kMaxPayloadSize) {
        return false;
    }
    auto data = encodeFrame(frame);
    return transfer(pipe, true, data.data(), static_cast<DWORD>(data.size()),
                    timeout, stopEvent);
}

} // namespace fcitx::win32::ipc
