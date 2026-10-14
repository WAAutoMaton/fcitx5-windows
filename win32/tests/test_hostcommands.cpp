#include "../ipc/hostcommands.h"
#include <cassert>
#include <stdexcept>
#include <string_view>

int main() {
    using namespace fcitx::win32::ipc;
    for (const auto type :
         {MessageType::OpenUserDataFolder, MessageType::RestartServices,
          MessageType::StopServices}) {
        Frame request;
        request.type = type;
        assert(validHostCommandRequest(request));
        request.contextId = 1;
        assert(!validHostCommandRequest(request));
        request.contextId = 0;
        request.payload = {0};
        assert(!validHostCommandRequest(request));
    }
    Frame request;
    request.type = MessageType::OpenSettings;
    assert(!validHostCommandRequest(request));
    HostCommands queue;
    assert(queue.reserve(MessageType::OpenSettings) == ERROR_INVALID_PARAMETER);
    assert(queue.reserve(MessageType::RestartServices) == ERROR_SUCCESS);
    assert(queue.reserve(MessageType::StopServices) == ERROR_BUSY);
    bool launched = false;
    queue.afterReply(MessageType::RestartServices, false,
                     [&](const wchar_t *) { launched = true; });
    assert(!launched);
    assert(queue.reserve(MessageType::StopServices) == ERROR_SUCCESS);
    try {
        queue.afterReply(MessageType::StopServices, true, [](const wchar_t *) {
            throw std::runtime_error("fixture");
        });
        assert(false);
    } catch (const std::runtime_error &) {
    }
    assert(queue.reserve(MessageType::RestartServices) == ERROR_SUCCESS);
    queue.afterReply(
        MessageType::RestartServices, true, [&](const wchar_t *arguments) {
            assert(std::wstring_view(arguments) == L"--restart-services");
            launched = true;
        });
    assert(launched && queue.reserve(MessageType::StopServices) == ERROR_BUSY);
    assert(queue.reserve(MessageType::OpenUserDataFolder) == ERROR_SUCCESS);
    queue.afterReply(
        MessageType::OpenUserDataFolder, true, [](const wchar_t *arguments) {
            assert(std::wstring_view(arguments) == L"--open-user-data");
        });
}
