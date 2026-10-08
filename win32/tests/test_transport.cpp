#include "../ipc/transport.h"
#include <cassert>

int main() {
    using namespace fcitx::win32::ipc;
    const auto name =
        pipeName() + L"-test-" + std::to_wstring(GetCurrentProcessId());
    assert(!userSid().empty());
    assert(!name.empty());
    HANDLE server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
                                     PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
                                         PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                     1, kHeaderSize + kMaxPayloadSize,
                                     kHeaderSize + kMaxPayloadSize, 0, nullptr);
    assert(server != INVALID_HANDLE_VALUE);
    HANDLE client =
        CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                    OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    assert(client != INVALID_HANDLE_VALUE);
    assert(ConnectNamedPipe(server, nullptr) ||
           GetLastError() == ERROR_PIPE_CONNECTED);
    DWORD mode = PIPE_READMODE_MESSAGE;
    assert(SetNamedPipeHandleState(client, &mode, nullptr, nullptr));
    Frame reply;
    const auto start = GetTickCount64();
    assert(!readFrame(client, reply, 25));
    assert(GetTickCount64() - start < 1000);

    Frame source;
    source.type = MessageType::Ack;
    source.requestId = 3;
    const auto bytes = encodeFrame(source);
    DWORD written = 0;
    assert(WriteFile(server, bytes.data(), static_cast<DWORD>(bytes.size()),
                     &written, nullptr));
    assert(readFrame(client, reply, 1000));
    assert(reply.requestId == 3 && reply.type == MessageType::Ack);

    HANDLE stop = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    assert(stop);
    assert(!readFrame(client, reply, INFINITE, stop));
    CloseHandle(stop);
    source.payload.resize(kMaxPayloadSize + 1);
    assert(!writeFrame(client, source, 1000));
    CloseHandle(client);
    DisconnectNamedPipe(server);
    CloseHandle(server);
}
