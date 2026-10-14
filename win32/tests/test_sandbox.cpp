#include "../ipc/sandbox.h"
#include "../ipc/service.h"
#include "../ipc/transport.h"
#include "../tsf/pipeclient.h"
#include <cassert>
#include <future>
#include <iostream>
#include <string>

namespace ipc = fcitx::win32::ipc;
namespace {
DWORD boxedPid = GetCurrentProcessId();
LONG queryStatus = 0;
ULONG64 flags = 1;
LONG WINAPI query(HANDLE pid, WCHAR *box, WCHAR *, WCHAR *, ULONG *) {
    if (queryStatus)
        return queryStatus;
    if (reinterpret_cast<ULONG_PTR>(pid) != boxedPid)
        return static_cast<LONG>(0xc000000b);
    wcscpy_s(box, 34, L"TestBox");
    return 0;
}
ULONG64 WINAPI info(HANDLE, ULONG) { return flags; }
} // namespace

int wmain(int count, wchar_t **arguments) {
    if (count == 2) {
        HANDLE server = CreateNamedPipeW(arguments[1], PIPE_ACCESS_DUPLEX,
                                         PIPE_TYPE_MESSAGE |
                                             PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                         1, 4096, 4096, 0, nullptr);
        assert(server != INVALID_HANDLE_VALUE);
        assert(ConnectNamedPipe(server, nullptr) ||
               GetLastError() == ERROR_PIPE_CONNECTED);
        ipc::Frame request;
        DWORD bytes = 0;
        std::vector<uint8_t> data(4096);
        assert(ReadFile(server, data.data(), static_cast<DWORD>(data.size()),
                        &bytes, nullptr));
        data.resize(bytes);
        assert(ipc::decodeFrame(data, request));
        assert(request.type == ipc::MessageType::Hello);
        ipc::Writer payload;
        payload.u16(ipc::kVersion);
        request.type = ipc::MessageType::HelloReply;
        request.payload = payload.take();
        const auto reply = ipc::encodeFrame(request);
        assert(WriteFile(server, reply.data(), static_cast<DWORD>(reply.size()),
                         &bytes, nullptr));
        for (const auto operation : {ipc::MessageType::StopServices,
                                     ipc::MessageType::OpenUserDataFolder}) {
            data.resize(4096);
            assert(ReadFile(server, data.data(),
                            static_cast<DWORD>(data.size()), &bytes, nullptr));
            data.resize(bytes);
            assert(ipc::decodeFrame(data, request));
            assert(request.type == operation && request.contextId == 0 &&
                   request.payload.empty());
            ipc::Writer result;
            result.u32(operation == ipc::MessageType::StopServices
                           ? ERROR_BUSY
                           : ERROR_SUCCESS);
            request.type = ipc::MessageType::ServiceReply;
            request.payload = result.take();
            const auto response = ipc::encodeFrame(request);
            assert(WriteFile(server, response.data(),
                             static_cast<DWORD>(response.size()), &bytes,
                             nullptr));
        }
        FlushFileBuffers(server);
        CloseHandle(server);
        return 0;
    }
    assert(count == 3 && std::wstring_view(arguments[1]) == L"--fixture");
    assert(!ipc::SandboxieApi(false, nullptr, nullptr).hostOnly());
    assert(ipc::SandboxieApi(true, nullptr, nullptr).hostOnly());
    ipc::SandboxieApi sandbox(true, query, info);
    assert(sandbox.hostOnly());
    assert(!sandbox.acceptsServer(boxedPid));
    assert(sandbox.acceptsServer(boxedPid + 1));
    queryStatus = static_cast<LONG>(0xc0000022);
    assert(sandbox.process(boxedPid) == ipc::SandboxState::Unknown);
    assert(sandbox.hostOnly() && !sandbox.acceptsServer(boxedPid + 1));
    queryStatus = 0;
    flags = 0x80000000;
    assert(!sandbox.hostOnly());
    flags = 0;
    assert(sandbox.hostOnly());
    flags = 1;
    assert(ipc::SandboxieApi(true, query, nullptr).hostOnly());

    // A valid protocol handshake must not override a boxed server rejection.
    const auto name =
        L"\\\\.\\pipe\\fcitx5-sandbox-test-" + std::to_wstring(boxedPid);
    HANDLE server =
        CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
                         PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                         1, 4096, 4096, 0, nullptr);
    assert(server != INVALID_HANDLE_VALUE);
    fcitx::PipeClient client(name, sandbox);
    assert(!client.connect() &&
           client.connectionError() == ERROR_ACCESS_DENIED);
    assert(std::string_view(client.connectionStage()) == "server ownership");
    assert(!client.connected());
    CloseHandle(server);

    wchar_t executable[32768]{};
    assert(GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable)));
    auto command =
        L"\"" + std::wstring(executable) + L"\" \"" + name + L"-host\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    assert(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                          &child));
    CloseHandle(child.hThread);
    fcitx::PipeClient host(name + L"-host", sandbox);
    const auto deadline = GetTickCount64() + 3000;
    while (!host.connect() && GetTickCount64() < deadline)
        Sleep(10);
    assert(host.connected() && host.connectionError() == ERROR_SUCCESS);
    assert(std::string_view(host.connectionStage()) == "connected");
    assert(host.serverProcessId() == child.dwProcessId);
    DWORD error = 0;
    assert(!host.hostCommand(ipc::MessageType::OpenSettings, error) &&
           error == ERROR_INVALID_PARAMETER);
    assert(host.hostCommand(ipc::MessageType::StopServices, error) &&
           error == ERROR_BUSY);
    assert(host.hostCommand(ipc::MessageType::OpenUserDataFolder, error) &&
           error == ERROR_SUCCESS);
    host.disconnect();
    assert(WaitForSingleObject(child.hProcess, 3000) == WAIT_OBJECT_0);
    DWORD exit = 1;
    assert(GetExitCodeProcess(child.hProcess, &exit) && exit == 0);
    CloseHandle(child.hProcess);

    const auto module = LoadLibraryW(arguments[2]);
    assert(module);
    const auto setState = reinterpret_cast<void(WINAPI *)(ULONG64, LONG)>(
        GetProcAddress(module, "SetSandboxState"));
    assert(setState && ipc::hostOnlyClient());
    auto denied = [](auto action) {
        bool rejected = false;
        try {
            action();
        } catch (const std::system_error &error) {
            rejected = error.code().value() == ERROR_ACCESS_DENIED;
        }
        assert(rejected);
    };
    denied([] { ipc::ServiceProcess service(L"Core"); });
    denied([] { ipc::ServiceController control(L"missing/Fcitx5.exe"); });
    denied([] { ipc::launchServiceExecutable(L"missing/Fcitx5.exe", true); });
    setState(0x80000000, 0);
    assert(ipc::hostServicesAllowed());
    setState(1, static_cast<LONG>(0xc0000022));
    denied([] { ipc::requireHostServices(); });
    FreeLibrary(module);
    std::cout << "PASS: sandbox/unknown launch refusal, host injection and "
                 "real host/boxed Pipe checks\n";
}
