#include "../ipc/service.h"
#include "../ipc/userdata.h"
#include "tsf.h"
#include <chrono>
#include <filesystem>
#include <memory>
#include <shellapi.h>
#include <shlobj.h>

namespace fcitx {
void Tsf::requestService(UINT command) {
    if (!inputAllowed_ ||
        (restrictedInput_ && command != kEnsureServiceMessage &&
         command != LangBarItem::kSettingsMessage))
        return;
    if (restrictedInput_ && command == kEnsureServiceMessage)
        return;
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
                    if (ipc::hostOnlyClient()) {
                        PipeClient connection;
                        if (!connection.connect()) {
                            OutputDebugStringW(
                                L"Fcitx5: host Core unavailable; check "
                                L"Sandboxie OpenPipePath and start Core "
                                L"outside the sandbox.\n");
                            return {connection.connectionError()};
                        }
                        if (command == kEnsureServiceMessage)
                            return {ERROR_SUCCESS};
                        if (command == LangBarItem::kSettingsMessage) {
                            ipc::SettingsReply reply;
                            if (!connection.openSettings(reply) ||
                                reply.error != ipc::SettingsError::None)
                                return {ERROR_OPEN_FAILED};
                            return {ERROR_SUCCESS};
                        }
                        const auto operation =
                            command == LangBarItem::kUserDataMessage
                                ? ipc::MessageType::OpenUserDataFolder
                            : command == LangBarItem::kRestartMessage
                                ? ipc::MessageType::RestartServices
                                : ipc::MessageType::StopServices;
                        DWORD error = ERROR_SUCCESS;
                        return {connection.hostCommand(operation, error)
                                    ? error
                                    : ERROR_OPEN_FAILED};
                    }
                    if (command == LangBarItem::kUserDataMessage) {
                        return {ipc::openUserDataFolder()};
                    }
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
    if (error && command != kEnsureServiceMessage && modernUi_) {
        OutputDebugStringW(L"Fcitx5: host service operation failed.\n");
        return;
    }
    if (error && command != kEnsureServiceMessage) {
        const auto translation = translationMonitor_.snapshot();
        const bool pipeMissing =
            ipc::hostOnlyClient() &&
            (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
        auto text =
            pipeMissing
                ? translation->wide(
                      "settings",
                      "Cannot reach the host Core named pipe. Start Core "
                      "outside Sandboxie, add the Fcitx5 OpenPipePath rule "
                      "to this sandbox, and reopen the application.")
                : translation->wide(
                      "settings",
                      command == LangBarItem::kUserDataMessage
                          ? win32::i18n::mark(
                                "settings", "Cannot open the user data folder.")
                          : win32::i18n::mark("settings",
                                              "Service operation failed."));
        if (command != LangBarItem::kUserDataMessage && !pipeMissing)
            text += L"\n" + translation->wide(
                                "settings",
                                "If services are stopped, restart services. "
                                "Check that the DLL, Core and Settings are in "
                                "the same installation directory.");
        if (ipc::hostOnlyClient() && !pipeMissing)
            text += L"\n" +
                    translation->wide("settings",
                                      "Start Core outside Sandboxie and allow "
                                      "its named pipe in the sandbox.");
        if (ipc::hostOnlyClient()) {
            const auto name = ipc::pipeName();
            constexpr std::wstring_view prefix = LR"(\\.\pipe\)";
            if (name.starts_with(prefix))
                text += L"\nOpenPipePath=\\Device\\NamedPipe\\" +
                        name.substr(prefix.size());
        }
        text += L"\n" +
                translation->format("settings", "Windows error: {0}", error);
        OutputDebugStringW(text.c_str());
    }
}

} // namespace fcitx
