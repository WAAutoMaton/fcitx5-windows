#include "../ipc/serviceclient.h"
#include <cassert>

namespace ipc = fcitx::win32::ipc;

struct Connection {
    bool available = true;
    bool transferred = true;
    DWORD error = ERROR_SUCCESS;
    ipc::SettingsError settingsError = ipc::SettingsError::None;
    unsigned connections = 0;
    unsigned requests = 0;
    ipc::MessageType operation{};
    bool connect() {
        ++connections;
        return available;
    }
    DWORD connectionError() const { return error; }
    bool openSettings(ipc::SettingsReply &reply) {
        ++requests;
        operation = ipc::MessageType::OpenSettings;
        reply.error = settingsError;
        return transferred;
    }
    bool hostCommand(ipc::MessageType command, DWORD &result) {
        ++requests;
        operation = command;
        result = error;
        return transferred;
    }
};

int main() {
    ipc::ClientEnvironment environment;
    environment.tokenKnown = environment.elevationKnown = true;
    environment.isolation = ipc::IsolationKind::Host;
    environment.elevationType = TokenElevationTypeFull;
    for (const auto operation :
         {ipc::MessageType::Hello, ipc::MessageType::OpenSettings,
          ipc::MessageType::OpenUserDataFolder,
          ipc::MessageType::RestartServices, ipc::MessageType::StopServices}) {
        for (const auto failure :
             {ERROR_SUCCESS, ERROR_FILE_NOT_FOUND, ERROR_PIPE_NOT_CONNECTED,
              ERROR_SERVICE_NOT_ACTIVE, ERROR_SERVICE_REQUEST_TIMEOUT}) {
            Connection connection;
            connection.available = failure == ERROR_SUCCESS;
            connection.error = failure;
            unsigned localCalls = 0;
            const auto result = ipc::routeServiceOperation(
                environment,
                [&] {
                    return ipc::remoteServiceOperation(connection, operation);
                },
                [&] {
                    ++localCalls;
                    return DWORD(ERROR_GEN_FAILURE);
                });
            assert(result == failure && localCalls == 0);
            assert(connection.connections == 1);
            assert(connection.requests ==
                   (failure == ERROR_SUCCESS &&
                            operation != ipc::MessageType::Hello
                        ? 1U
                        : 0U));
            if (connection.requests)
                assert(connection.operation == operation);
        }
        Connection timeout;
        timeout.transferred = false;
        assert(ipc::remoteServiceOperation(timeout, operation) ==
               (operation == ipc::MessageType::Hello ? ERROR_SUCCESS
                                                     : ERROR_OPEN_FAILED));
        assert(timeout.connections == 1 && timeout.requests <= 1);
    }
    Connection rejected;
    rejected.error = ERROR_BUSY;
    assert(ipc::remoteServiceOperation(
               rejected, ipc::MessageType::StopServices) == ERROR_BUSY);
    rejected.settingsError = ipc::SettingsError::Invalid;
    assert(ipc::remoteServiceOperation(
               rejected, ipc::MessageType::OpenSettings) == ERROR_OPEN_FAILED);
    Connection invalid;
    assert(ipc::remoteServiceOperation(invalid, ipc::MessageType::KeyDown) ==
           ERROR_INVALID_PARAMETER);
    assert(!invalid.connections);

    const auto remote = [] { return true; };
    const auto local = [] { return false; };
    assert(ipc::routeServiceOperation(environment, remote, local));
    environment.elevationKnown = false;
    assert(ipc::routeServiceOperation(environment, remote, local));
    environment.elevationKnown = true;
    environment.elevationType = TokenElevationTypeLimited;
    assert(!ipc::routeServiceOperation(environment, remote, local));
    environment.elevationType = TokenElevationTypeDefault;
    assert(!ipc::routeServiceOperation(environment, remote, local));
    environment.isolation = ipc::IsolationKind::Sandboxie;
    assert(ipc::routeServiceOperation(environment, remote, local));
    environment.isolation = ipc::IsolationKind::Unknown;
    assert(ipc::routeServiceOperation(environment, remote, local));
    environment.isolation = ipc::IsolationKind::AppContainer;
    assert(ipc::routeServiceOperation(environment, remote, local));
}
