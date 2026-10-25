#include "../ipc/pipeidentity.h"
#include "../ipc/transport.h"
#include <cassert>

namespace ipc = fcitx::win32::ipc;

int main() {
    const auto host = ipc::clientEnvironment();
    const auto identity = ipc::uacLoginIdentity();
    assert(host.tokenKnown && !host.appContainer);
    const auto name = ipc::pipeName() + L"-identity-test-" +
                      std::to_wstring(GetCurrentProcessId());
    for (const bool anonymousMessage : {false, true}) {
        auto sddl = L"D:P(A;;GA;;;" + host.userSid + L")";
        sddl += L"S:(ML;;NW;;;LW)";
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        assert(ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr));
        SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
        HANDLE server =
            CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
                             PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
                                 PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                             1, 4096, 4096, 0, &security);
        LocalFree(descriptor);
        assert(server != INVALID_HANDLE_VALUE);

        assert(ImpersonateAnonymousToken(GetCurrentThread()));
        HANDLE client = CreateFileW(
            name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
            nullptr);
        const auto openError = GetLastError();
        assert(RevertToSelf());
        // Authorize the open as the real user, then expose an anonymous sender
        // through dynamic tracking to model Sandboxie's split token views.
        assert(client == INVALID_HANDLE_VALUE);
        assert(openError == ERROR_ACCESS_DENIED);
        client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                             nullptr, OPEN_EXISTING,
                             SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION |
                                 SECURITY_CONTEXT_TRACKING,
                             nullptr);
        assert(client != INVALID_HANDLE_VALUE);
        assert(ConnectNamedPipe(server, nullptr) ||
               GetLastError() == ERROR_PIPE_CONNECTED);
        const char message = 'x';
        DWORD bytes = 0;
        if (anonymousMessage)
            assert(ImpersonateAnonymousToken(GetCurrentThread()));
        assert(WriteFile(client, &message, sizeof(message), &bytes, nullptr));
        if (anonymousMessage)
            assert(RevertToSelf());
        char received = 0;
        assert(ReadFile(server, &received, sizeof(received), &bytes, nullptr));
        assert(received == message);

        ipc::ClientEnvironment caller;
        assert(ipc::authorizedPipeClient(server, host, caller, false));
        const auto decision =
            ipc::authorizedPipeClient(server, identity, caller, false);
        assert(decision.match == (anonymousMessage
                                      ? ipc::PipeLoginMatch::SandboxAnonymous
                                      : ipc::PipeLoginMatch::ExactLogin));
        assert(caller.tokenKnown && !caller.appContainer);
        assert(ipc::sameLogin(host, caller) == !anonymousMessage);
        assert(ipc::authorizedPipeClient(server, host, caller, true) ==
               !anonymousMessage);
        if (anonymousMessage)
            assert(caller.userSid == L"S-1-5-7");
        auto other = host;
        if (!anonymousMessage) {
            other.userSid = L"S-1-0-0";
            assert(!ipc::authorizedPipeClient(server, other, caller, false));
            other = host;
            ++other.authenticationId.LowPart;
            assert(!ipc::authorizedPipeClient(server, other, caller, false));
            other = host;
        }
        ++other.sessionId;
        assert(!ipc::authorizedPipeClient(server, other, caller, false));
        other = host;
        other.tokenKnown = false;
        assert(!ipc::authorizedPipeClient(server, other, caller, false));
        HANDLE threadToken = nullptr;
        assert(!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE,
                                &threadToken));
        assert(GetLastError() == ERROR_NO_TOKEN);
        CloseHandle(client);
        DisconnectNamedPipe(server);
        CloseHandle(server);
    }
    ipc::ClientEnvironment caller;
    const auto failed = ipc::authorizedPipeClient(INVALID_HANDLE_VALUE,
                                                  identity, caller, false);
    assert(!failed && failed.reason == ipc::PipeLoginReason::TokenQuery &&
           failed.error != ERROR_SUCCESS);
    HANDLE token = nullptr;
    assert(!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token));
    assert(GetLastError() == ERROR_NO_TOKEN);
}
