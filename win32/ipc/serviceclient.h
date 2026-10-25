#pragma once

#include "isolationpolicy.h"

namespace fcitx::win32::ipc {

inline bool uacHostServiceClient(const ClientEnvironment &environment) {
    // A failed elevation query must never enable local elevated startup.
    return environment.isolation == IsolationKind::Host &&
           (!environment.elevationKnown ||
            environment.elevationType == TokenElevationTypeFull);
}

template <typename Remote, typename Local>
auto routeServiceOperation(const ClientEnvironment &environment, Remote remote,
                           Local local) -> decltype(local()) {
    if (isolationPolicy(environment) != IsolationPolicy::HostServices ||
        uacHostServiceClient(environment))
        return remote();
    return local();
}

// Hello denotes ensure-only: authenticate the existing Core without launching.
template <typename Connection>
DWORD remoteServiceOperation(Connection &connection, MessageType operation) {
    if (operation != MessageType::Hello &&
        operation != MessageType::OpenSettings && !hostCommand(operation))
        return ERROR_INVALID_PARAMETER;
    if (!connection.connect())
        return connection.connectionError();
    if (operation == MessageType::Hello)
        return ERROR_SUCCESS;
    if (operation == MessageType::OpenSettings) {
        SettingsReply reply;
        return connection.openSettings(reply) &&
                       reply.error == SettingsError::None
                   ? ERROR_SUCCESS
                   : ERROR_OPEN_FAILED;
    }
    DWORD error = ERROR_SUCCESS;
    return connection.hostCommand(operation, error) ? error : ERROR_OPEN_FAILED;
}

} // namespace fcitx::win32::ipc
