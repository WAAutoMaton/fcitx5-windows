#include "../ipc/service.h"
#include "tsf.h"
#include <chrono>
#include <filesystem>

namespace fcitx {

void Tsf::requestService(UINT command) {
    if (serviceTask_.valid()) {
        if (command != kEnsureServiceMessage) {
            pendingServiceCommand_ = command;
        }
        return;
    }
    HMODULE module = nullptr;
    std::wstring path(32768, L'\0');
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&messageWindowProc),
                            &module)) {
        return;
    }
    const auto size = GetModuleFileNameW(module, path.data(),
                                         static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) {
        return;
    }
    path.resize(size);
    const auto core = (std::filesystem::path(path).parent_path().parent_path() /
                       "bin" / "Fcitx5.exe")
                          .lexically_normal();
    if (command == LangBarItem::kRestartMessage ||
        command == LangBarItem::kStopMessage) {
        pipe_.disconnect();
        clearRemoteContext();
    }
    serviceCommand_ = command;
    try {
        serviceTask_ =
            std::async(std::launch::async, [core, command]() -> ServiceResult {
                try {
                    if ((command == LangBarItem::kRestartMessage ||
                         command == LangBarItem::kStopMessage) &&
                        ipc::isServiceProcess(L"Settings")) {
                        // Return to the Settings UI loop so it can exit before
                        // the separate controller waits for/reopens Settings.
                        ipc::launchServiceExecutable(
                            core, true,
                            command == LangBarItem::kRestartMessage
                                ? L"--restart-services"
                                : L"--stop-services");
                        return {ERROR_SUCCESS, true};
                    }
                    ipc::ServiceController controller(core);
                    bool openSettings =
                        command == LangBarItem::kSettingsMessage;
                    if (command == LangBarItem::kStopMessage) {
                        controller.stop();
                        return {ERROR_SUCCESS};
                    }
                    if (command == LangBarItem::kRestartMessage) {
                        openSettings = controller.restart();
                    } else if (!controller.ensureCore()) {
                        return {ERROR_SERVICE_NOT_ACTIVE};
                    }
                    if (openSettings) {
                        PipeClient connection;
                        for (unsigned attempt = 0; attempt < 50; ++attempt) {
                            if (WaitNamedPipeW(ipc::pipeName().c_str(), 50)) {
                                if (!connection.connect()) {
                                    return {ERROR_REVISION_MISMATCH};
                                }
                                ipc::SettingsReply reply;
                                if (!connection.openSettings(reply) ||
                                    reply.error != ipc::SettingsError::None) {
                                    return {ERROR_OPEN_FAILED};
                                }
                                return {ERROR_SUCCESS};
                            }
                            Sleep(100);
                        }
                        return {ERROR_SERVICE_REQUEST_TIMEOUT};
                    }
                    return {ERROR_SUCCESS};
                } catch (const std::system_error &error) {
                    return {static_cast<DWORD>(error.code().value())};
                } catch (...) {
                    return {ERROR_GEN_FAILURE};
                }
            });
    } catch (...) {
        serviceCommand_ = 0;
    }
}

void Tsf::pollServiceTask() {
    if (!serviceTask_.valid() ||
        serviceTask_.wait_for(std::chrono::milliseconds(0)) !=
            std::future_status::ready) {
        return;
    }
    const auto result = serviceTask_.get();
    const auto error = result.error;
    serviceHostClosing_ = result.closingHost;
    const auto command = serviceCommand_;
    serviceCommand_ = 0;
    nextReconnect_ = error ? GetTickCount64() + 2000 : 0;
    if (pendingServiceCommand_ && !serviceHostClosing_) {
        const auto pending = pendingServiceCommand_;
        pendingServiceCommand_ = 0;
        requestService(pending);
    }
    if (error && command != kEnsureServiceMessage) {
        const auto text =
            L"\u670d\u52a1\u64cd\u4f5c\u5931\u8d25\u3002\n"
            L"\u670d\u52a1\u5df2\u5173\u95ed\u65f6\uff0c\u8bf7\u5148"
            L"\u9009\u62e9\u201c\u91cd\u542f\u670d\u52a1\u201d\u3002\n"
            L"\u8bf7\u786e\u8ba4 DLL\u3001Core \u548c\u8bbe\u7f6e"
            L"\u7a0b\u5e8f\u5df2\u90e8\u7f72\u5230\u540c\u4e00"
            L"\u5b89\u88c5\u76ee\u5f55\u3002\nWindows error: " +
            std::to_wstring(error);
        MessageBoxW(nullptr, text.c_str(), L"Fcitx5", MB_OK | MB_ICONERROR);
    }
}

} // namespace fcitx
