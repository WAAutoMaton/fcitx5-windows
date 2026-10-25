#include "../ipc/pipeidentity.h"
#include "../ipc/service.h"
#include "../ipc/transport.h"
#include <future>
#include <iostream>
#include <stdexcept>

namespace ipc = fcitx::win32::ipc;

namespace {
void require(bool success, const char *message) {
    if (!success)
        throw std::runtime_error(std::string(message) +
                                 " error=" + std::to_string(GetLastError()));
}

void connectServer(HANDLE pipe) {
    ipc::ServiceHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    require(event.get() != nullptr, "CreateEvent");
    OVERLAPPED pending{};
    pending.hEvent = event.get();
    if (ConnectNamedPipe(pipe, &pending))
        return;
    const auto error = GetLastError();
    if (error == ERROR_PIPE_CONNECTED)
        return;
    require(error == ERROR_IO_PENDING, "ConnectNamedPipe");
    if (WaitForSingleObject(event.get(), 30000) != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &pending);
        DWORD bytes = 0;
        GetOverlappedResult(pipe, &pending, &bytes, TRUE);
        SetLastError(ERROR_TIMEOUT);
        require(false, "waiting for an independently launched client");
    }
    DWORD bytes = 0;
    require(GetOverlappedResult(pipe, &pending, &bytes, FALSE) != FALSE,
            "ConnectNamedPipe result");
}

ipc::PipeLoginMatch serve(const std::wstring &name,
                          std::promise<void> *ready = nullptr) {
    const auto identity = ipc::uacLoginIdentity();
    require(identity.local.tokenKnown, "server token");
    const auto sddl = L"D:P(A;;GA;;;" + identity.local.userSid + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    require(ConvertStringSecurityDescriptorToSecurityDescriptorW(
                sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr) != FALSE,
            "server DACL");
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    ipc::ServiceHandle pipe(
        CreateNamedPipeW(name.c_str(),
                         PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                             FILE_FLAG_FIRST_PIPE_INSTANCE,
                         PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
                             PIPE_REJECT_REMOTE_CLIENTS,
                         1, 4096, 4096, 0, &security));
    const auto createError = GetLastError();
    LocalFree(descriptor);
    SetLastError(createError);
    require(pipe.get() != INVALID_HANDLE_VALUE, "CreateNamedPipe");
    std::wcout << L"pipe=" << name << std::endl;
    if (ready)
        ready->set_value();
    connectServer(pipe.get());
    ipc::Frame request;
    require(ipc::readFrame(pipe.get(), request, 3000, nullptr, 4096),
            "Hello read");
    require(request.type == ipc::MessageType::Hello &&
                request.payload.empty() && !request.contextId &&
                request.requestId == 1,
            "Hello validation");
    ipc::ClientEnvironment caller;
    const auto decision =
        ipc::authorizedPipeClient(pipe.get(), identity, caller, false);
    std::cout << "serverElevation=" << identity.local.elevationType
              << " callerElevation=" << caller.elevationType
              << " match=" << static_cast<unsigned>(decision.match)
              << " reason=" << static_cast<unsigned>(decision.reason)
              << " error=" << decision.error << std::endl;
    ipc::Writer writer;
    writer.u8(static_cast<uint8_t>(decision.match));
    request.type = ipc::MessageType::HelloReply;
    request.payload = writer.take();
    require(ipc::writeFrame(pipe.get(), request, 3000), "Hello write");
    // The acknowledgement keeps the response alive until the client reads it.
    require(ipc::readFrame(pipe.get(), request, 3000, nullptr, 4096) &&
                request.type == ipc::MessageType::Ack &&
                request.payload.empty() && request.requestId == 2 &&
                !request.contextId,
            "client acknowledgement");
    return decision.match;
}

ipc::PipeLoginMatch connect(const std::wstring &name) {
    require(WaitNamedPipeW(name.c_str(), 3000) != FALSE, "WaitNamedPipe");
    ipc::ServiceHandle pipe(
        CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                    OPEN_EXISTING, ipc::kPipeClientFlags, nullptr));
    require(pipe.get() != INVALID_HANDLE_VALUE, "CreateFile");
    DWORD mode = PIPE_READMODE_MESSAGE;
    require(SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr) !=
                FALSE,
            "SetNamedPipeHandleState");
    ipc::Frame request;
    request.type = ipc::MessageType::Hello;
    request.requestId = 1;
    require(ipc::writeFrame(pipe.get(), request, 3000), "Hello write");
    ipc::Frame response;
    require(ipc::readFrame(pipe.get(), response, 3000, nullptr, 4096),
            "Hello read");
    ipc::Reader reader(response.payload.data(), response.payload.size());
    uint8_t match = 0;
    require(response.type == ipc::MessageType::HelloReply &&
                response.requestId == 1 && !response.contextId &&
                reader.u8(match) && !reader.remaining() &&
                match <=
                    static_cast<uint8_t>(ipc::PipeLoginMatch::SandboxAnonymous),
            "Hello validation");
    request.type = ipc::MessageType::Ack;
    request.requestId = 2;
    require(ipc::writeFrame(pipe.get(), request, 3000),
            "client acknowledgement");
    std::cout << "match=" << static_cast<unsigned>(match) << std::endl;
    return static_cast<ipc::PipeLoginMatch>(match);
}
} // namespace

int wmain(int count, wchar_t **arguments) {
    try {
        const std::wstring prefix = LR"(\\.\pipe\fcitx5-uac-test-)";
        if (count == 1) {
            const auto name = prefix + std::to_wstring(GetCurrentProcessId());
            std::promise<void> ready;
            auto started = ready.get_future();
            auto server = std::async(std::launch::async, [&] {
                try {
                    return serve(name, &ready);
                } catch (...) {
                    try {
                        ready.set_exception(std::current_exception());
                    } catch (const std::future_error &) {
                    }
                    throw;
                }
            });
            started.get();
            require(connect(name) == ipc::PipeLoginMatch::ExactLogin,
                    "self client identity");
            require(server.get() == ipc::PipeLoginMatch::ExactLogin,
                    "self server identity");
            return 0;
        }
        require(count == 3, "use --server/--client <unique suffix>");
        const std::wstring suffix = arguments[2];
        require(!suffix.empty() && suffix.size() <= 100 &&
                    suffix.find_first_not_of(
                        L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0"
                        L"123456789-") == std::wstring::npos,
                "invalid test pipe suffix");
        const std::wstring mode = arguments[1];
        require(mode == L"--server" || mode == L"--client", "invalid mode");
        const auto match = mode == L"--server" ? serve(prefix + suffix)
                                               : connect(prefix + suffix);
        return match == ipc::PipeLoginMatch::Denied ? 1 : 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
