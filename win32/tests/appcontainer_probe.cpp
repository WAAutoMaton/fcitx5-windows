#include "../ipc/pipeidentity.h"
#include "../ipc/transport.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <userenv.h>

namespace ipc = fcitx::win32::ipc;

int child(const wchar_t *name, bool verifyServer, bool appContainer) {
    const auto environment = ipc::clientEnvironment();
    if (!environment.tokenKnown || environment.appContainer != appContainer ||
        (appContainer && !environment.lowIntegrity))
        return 100;
    if (!WaitNamedPipeW(name, 1000))
        return 1000 + GetLastError();
    HANDLE pipe = CreateFileW(name, ipc::kInputPipeAccess, 0, nullptr,
                              OPEN_EXISTING, ipc::kPipeClientFlags, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
        return 2000 + GetLastError();
    DWORD serverId = 0;
    if (verifyServer && !ipc::pipeServerIdentity(pipe, environment, serverId)) {
        const auto error = GetLastError();
        OutputDebugStringW(
            L"AppContainer cannot open host process directly.\n");
        (void)error;
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
        const auto error = GetLastError();
        CloseHandle(pipe);
        return 4000 + error;
    }
    ipc::Frame request;
    request.type = ipc::MessageType::Hello;
    request.requestId = 1;
    ipc::Frame reply;
    auto success = ipc::writeFrame(pipe, request, 1000) &&
                   ipc::readFrame(pipe, reply, 1000) &&
                   reply.type == ipc::MessageType::HelloReply &&
                   reply.requestId == 1;
    if (success && verifyServer) {
        request.type = ipc::MessageType::AuthenticateServer;
        request.requestId = 2;
        success = ipc::writeFrame(pipe, request, 1000) &&
                  ipc::readFrame(pipe, reply, 1000) &&
                  reply.type == ipc::MessageType::ServerIdentityReply;
        if (!success)
            return 6000;
        ipc::Reader reader(reply.payload.data(), reply.payload.size());
        uint64_t value = 0;
        if (!reader.u64(value))
            return 6000;
        HANDLE process =
            reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(value));
        ULONG pid = 0;
        WCHAR expected[32768]{};
        GetModuleFileNameW(nullptr, expected, 32768);
        if (!GetNamedPipeServerProcessId(pipe, &pid) ||
            GetProcessId(process) != pid)
            return 6001;
        success =
            ipc::verifiedServerHandle(pipe, process, environment, expected);
        success =
            success && !ipc::verifiedServerHandle(pipe, process, environment,
                                                  L"C:\\wrong\\Core.exe");
        auto wrong = environment;
        ++wrong.sessionId;
        success = success &&
                  !ipc::verifiedServerHandle(pipe, process, wrong, expected);
        wrong = environment;
        wrong.userSid = L"S-1-0-0";
        success = success &&
                  !ipc::verifiedServerHandle(pipe, process, wrong, expected);
        HANDLE token = nullptr;
        if (appContainer)
            success =
                success && !OpenProcessToken(process, TOKEN_DUPLICATE, &token);
        if (token)
            CloseHandle(token);
        CloseHandle(process);
    }
    if (success) {
        std::vector<uint64_t> times;
        for (uint64_t i = 0; i < 100; ++i) {
            request.type = ipc::MessageType::PollState;
            request.requestId = i + 3;
            const auto start = std::chrono::steady_clock::now();
            if (!ipc::writeFrame(pipe, request, 1000) ||
                !ipc::readFrame(pipe, reply, 1000) ||
                reply.requestId != request.requestId)
                return 8000;
            times.push_back(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count());
        }
        std::sort(times.begin(), times.end());
        const auto start = std::chrono::steady_clock::now();
        success = !ipc::readFrame(pipe, reply, 25) &&
                  GetLastError() == ERROR_SEM_TIMEOUT;
        const auto cancellation =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start)
                .count();
        HANDLE stop = CreateEventW(nullptr, TRUE, TRUE, nullptr);
        success = success && stop &&
                  !ipc::readFrame(pipe, reply, INFINITE, stop) &&
                  GetLastError() == ERROR_OPERATION_ABORTED;
        if (stop)
            CloseHandle(stop);
        ipc::Writer metrics;
        metrics.u64(times[49]);
        metrics.u64(times[94]);
        metrics.u64(times[98]);
        metrics.u64(cancellation);
        request.type = ipc::MessageType::Ack;
        request.requestId = 103;
        request.payload = metrics.take();
        success = success && ipc::writeFrame(pipe, request, 1000);
    }
    CloseHandle(pipe);
    return success ? 0 : 5000 + GetLastError();
}

int wmain(int argc, wchar_t **argv) {
    if (argc == 5 && std::wstring(argv[1]) == L"--child")
        return child(argv[2], std::wstring(argv[3]) == L"verify",
                     std::wstring(argv[4]) == L"app");
    if (argc != 2) {
        std::wcerr << L"Pass an existing AppContainer profile/package family "
                      L"name. No profile is created.\n";
        return 1;
    }
    PSID appSid = nullptr;
    auto status = DeriveAppContainerSidFromAppContainerName(argv[1], &appSid);
    if (FAILED(status))
        return 1;
    const auto environment = ipc::clientEnvironment();
    std::wcout << L"host session=" << environment.sessionId
               << L" appContainerSid=" << ipc::sidString(appSid) << L"\n";
    bool allPassed = true;
    for (bool appContainer : {false, true}) {
        for (bool local : {false, true}) {
            for (bool verify : {false, true}) {
                const auto name = std::wstring(local ? LR"(\\.\pipe\LOCAL\)"
                                                     : LR"(\\.\pipe\)") +
                                  L"fcitx5-appcontainer-probe-" +
                                  std::to_wstring(GetCurrentProcessId()) +
                                  (verify ? L"-verify" : L"-io");
                PSECURITY_DESCRIPTOR security = nullptr;
                const auto sddl = ipc::inputPipeSecurity(environment.userSid);
                if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                        sddl.c_str(), SDDL_REVISION_1, &security, nullptr))
                    return 2;
                SECURITY_ATTRIBUTES attributes{sizeof(attributes), security,
                                               FALSE};
                HANDLE pipe =
                    CreateNamedPipeW(name.c_str(),
                                     PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                                         FILE_FLAG_FIRST_PIPE_INSTANCE,
                                     PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
                                         PIPE_REJECT_REMOTE_CLIENTS,
                                     1, 4096, 4096, 0, &attributes);
                LocalFree(security);
                if (pipe == INVALID_HANDLE_VALUE)
                    return 3;
                OVERLAPPED connection{};
                connection.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                ConnectNamedPipe(pipe, &connection);
                SIZE_T size = 0;
                InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
                std::vector<BYTE> storage(size);
                auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
                    storage.data());
                if (!InitializeProcThreadAttributeList(list, 1, 0, &size))
                    return 4;
                SECURITY_CAPABILITIES capabilities{};
                capabilities.AppContainerSid = appSid;
                if (appContainer &&
                    !UpdateProcThreadAttribute(
                        list, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                        &capabilities, sizeof(capabilities), nullptr, nullptr))
                    return 5;
                STARTUPINFOEXW startup{};
                startup.StartupInfo.cb = sizeof(startup);
                startup.lpAttributeList = list;
                WCHAR executable[32768]{};
                GetModuleFileNameW(nullptr, executable, 32768);
                auto command = L"\"" + std::wstring(executable) +
                               L"\" --child \"" + name + L"\" " +
                               (verify ? L"verify" : L"io") +
                               (appContainer ? L" app" : L" desktop");
                PROCESS_INFORMATION process{};
                const auto launched = CreateProcessW(
                    executable, command.data(), nullptr, nullptr, FALSE,
                    EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr,
                    nullptr, &startup.StartupInfo, &process);
                const auto launchError = GetLastError();
                DeleteProcThreadAttributeList(list);
                bool identified = false;
                if (launched && WaitForSingleObject(connection.hEvent, 3000) ==
                                    WAIT_OBJECT_0) {
                    DWORD transferred = 0;
                    GetOverlappedResult(pipe, &connection, &transferred, FALSE);
                    ipc::Frame request;
                    if (ipc::readFrame(pipe, request, 1000)) {
                        ipc::ClientEnvironment caller;
                        identified = ipc::pipeClientIdentity(pipe, caller) &&
                                     caller.appContainer == appContainer &&
                                     (!appContainer || caller.lowIntegrity) &&
                                     ipc::sameLogin(environment, caller);
                        auto other = environment;
                        ++other.sessionId;
                        identified =
                            identified && !ipc::sameLogin(other, caller);
                        other = environment;
                        ++other.authenticationId.LowPart;
                        identified =
                            identified && !ipc::sameLogin(other, caller);
                        if (identified) {
                            request.type = ipc::MessageType::HelloReply;
                            ipc::Writer version;
                            version.u16(ipc::kVersion);
                            request.payload = version.take();
                            ipc::writeFrame(pipe, request, 1000);
                            if (verify && ipc::readFrame(pipe, request, 1000) &&
                                request.type ==
                                    ipc::MessageType::AuthenticateServer) {
                                ipc::ServerIdentityHandle identity(pipe);
                                ipc::Writer writer;
                                writer.u64(identity.value());
                                request.payload = writer.take();
                                request.type =
                                    ipc::MessageType::ServerIdentityReply;
                                if (ipc::writeFrame(pipe, request, 1000))
                                    identity.delivered();
                            }
                            for (int i = 0; i < 100; ++i) {
                                if (!ipc::readFrame(pipe, request, 1000))
                                    break;
                                ipc::writeFrame(pipe, request, 1000);
                            }
                            if (ipc::readFrame(pipe, request, 1000) &&
                                request.type == ipc::MessageType::Ack) {
                                ipc::Reader metrics(request.payload.data(),
                                                    request.payload.size());
                                uint64_t p50 = 0, p95 = 0, p99 = 0,
                                         cancellation = 0;
                                if (metrics.u64(p50) && metrics.u64(p95) &&
                                    metrics.u64(p99) &&
                                    metrics.u64(cancellation))
                                    std::wcout << L"roundtrip_us p50=" << p50
                                               << L" p95=" << p95 << L" p99="
                                               << p99 << L" cancellation_us="
                                               << cancellation << L"\n";
                            }
                        }
                    }
                }
                CancelIoEx(pipe, &connection);
                DWORD transferred = 0;
                GetOverlappedResult(pipe, &connection, &transferred, TRUE);
                CloseHandle(connection.hEvent);
                DWORD exitCode = launchError;
                if (launched) {
                    if (WaitForSingleObject(process.hProcess, 3000) !=
                        WAIT_OBJECT_0)
                        TerminateProcess(process.hProcess, 99);
                    WaitForSingleObject(process.hProcess, 1000);
                    GetExitCodeProcess(process.hProcess, &exitCode);
                    CloseHandle(process.hThread);
                    CloseHandle(process.hProcess);
                }
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
                std::wcout << name << L" appContainer=" << appContainer
                           << L" verifyServer=" << verify << L" launched="
                           << launched << L" clientExit=" << exitCode
                           << L" callerVerified=" << identified << L"\n";
                if (!local)
                    allPassed =
                        allPassed && launched && exitCode == 0 && identified;
            }
        }
    }
    FreeSid(appSid);
    return allPassed ? 0 : 6;
}
