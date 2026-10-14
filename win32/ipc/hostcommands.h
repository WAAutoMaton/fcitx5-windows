#pragma once

#include "protocol.h"
#include <windows.h>

namespace fcitx::win32::ipc {

inline bool validHostCommandRequest(const Frame &request) {
    return hostCommand(request.type) && !request.contextId &&
           request.payload.empty();
}

// Owned by the Core main thread. Stop/restart stays reserved until Core exits.
class HostCommands {
  public:
    DWORD reserve(MessageType command) {
        if (!hostCommand(command))
            return ERROR_INVALID_PARAMETER;
        if (command != MessageType::OpenUserDataFolder) {
            if (controlPending_)
                return ERROR_BUSY;
            controlPending_ = true;
        }
        return ERROR_SUCCESS;
    }

    template <typename Launcher>
    void afterReply(MessageType command, bool delivered, Launcher launch) {
        const bool control = command != MessageType::OpenUserDataFolder;
        if (!delivered) {
            if (control)
                controlPending_ = false;
            return;
        }
        const wchar_t *arguments =
            command == MessageType::OpenUserDataFolder ? L"--open-user-data"
            : command == MessageType::RestartServices  ? L"--restart-services"
            : command == MessageType::StopServices     ? L"--stop-services"
                                                       : nullptr;
        if (!arguments)
            return;
        try {
            launch(arguments);
        } catch (...) {
            if (control)
                controlPending_ = false;
            throw;
        }
    }

  private:
    bool controlPending_ = false;
};

} // namespace fcitx::win32::ipc
