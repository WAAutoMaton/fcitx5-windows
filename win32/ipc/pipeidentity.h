#pragma once

#include "clientenvironment.h"
#include <aclapi.h>
#include <exception>

namespace fcitx::win32::ipc {

// Identification SQOS permits querying the caller without using its authority.
constexpr DWORD kInputPipeAccess = FILE_READ_DATA | FILE_WRITE_DATA |
                                   FILE_READ_ATTRIBUTES |
                                   FILE_WRITE_ATTRIBUTES | SYNCHRONIZE;
constexpr DWORD kPipeClientFlags =
    FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION;
constexpr DWORD kServerQueryAccess =
    PROCESS_QUERY_LIMITED_INFORMATION | READ_CONTROL;

class ServerIdentityHandle {
  public:
    explicit ServerIdentityHandle(HANDLE pipe) {
        ULONG pid = 0;
        if (!GetNamedPipeClientProcessId(pipe, &pid))
            return;
        client_ = OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid);
        if (!client_)
            return;
        DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), client_,
                        &remote_, kServerQueryAccess, FALSE, 0);
    }
    ~ServerIdentityHandle() {
        if (remote_ && !delivered_) {
            HANDLE local = nullptr;
            if (DuplicateHandle(client_, remote_, GetCurrentProcess(), &local,
                                0, FALSE, DUPLICATE_CLOSE_SOURCE))
                CloseHandle(local);
        }
        if (client_)
            CloseHandle(client_);
    }
    ServerIdentityHandle(const ServerIdentityHandle &) = delete;
    ServerIdentityHandle &operator=(const ServerIdentityHandle &) = delete;
    uint64_t value() const { return reinterpret_cast<ULONG_PTR>(remote_); }
    void delivered() { delivered_ = true; }

  private:
    HANDLE client_ = nullptr;
    HANDLE remote_ = nullptr;
    bool delivered_ = false;
};

inline bool verifiedServerHandle(HANDLE pipe, HANDLE process,
                                 const ClientEnvironment &client,
                                 const std::wstring &expectedImage) {
    ULONG pid = 0, session = 0;
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    std::wstring image(32768, L'\0');
    DWORD size = static_cast<DWORD>(image.size());
    if (!GetNamedPipeServerProcessId(pipe, &pid) ||
        !GetNamedPipeServerSessionId(pipe, &session) ||
        session != client.sessionId || GetProcessId(process) != pid ||
        expectedImage.empty() ||
        !QueryFullProcessImageNameW(process, 0, image.data(), &size))
        return false;
    image.resize(size);
    if (CompareStringOrdinal(image.c_str(), -1, expectedImage.c_str(), -1,
                             TRUE) != CSTR_EQUAL)
        return false;
    const auto error =
        GetSecurityInfo(process, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION,
                        &owner, nullptr, nullptr, nullptr, &descriptor);
    const bool success =
        error == ERROR_SUCCESS && sidString(owner) == client.userSid;
    if (descriptor)
        LocalFree(descriptor);
    return success;
}

inline bool pipeClientIdentity(HANDLE pipe, ClientEnvironment &identity,
                               DWORD *error = nullptr) {
    identity = {};
    if (error)
        *error = ERROR_SUCCESS;
    if (!ImpersonateNamedPipeClient(pipe)) {
        if (error)
            *error = GetLastError();
        return false;
    }
    HANDLE token = nullptr;
    bool success =
        OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token) != FALSE;
    DWORD queryError = success ? ERROR_SUCCESS : GetLastError();
    try {
        if (success)
            identity = tokenEnvironment(token);
    } catch (...) {
        if (token)
            CloseHandle(token);
        if (!RevertToSelf())
            std::terminate();
        if (error)
            *error = ERROR_NOT_ENOUGH_MEMORY;
        return false;
    }
    if (token)
        CloseHandle(token);
    if (!RevertToSelf())
        std::terminate();
    if (error)
        *error = success && identity.tokenKnown
                     ? ERROR_SUCCESS
                     : (queryError ? queryError : ERROR_INVALID_DATA);
    return success && identity.tokenKnown;
}

enum class PipeLoginMatch {
    Denied,
    ExactLogin,
    UacLinkedLogin,
    SandboxAnonymous
};
enum class PipeLoginReason {
    None,
    UnknownToken,
    PeerSession,
    TokenSession,
    InputOnly,
    RestrictedEnvironment,
    DifferentUser,
    NoUacLink,
    DifferentLogin,
    TokenQuery,
    SessionQuery
};

struct PipeLoginDecision {
    PipeLoginMatch match = PipeLoginMatch::Denied;
    PipeLoginReason reason = PipeLoginReason::None;
    DWORD error = ERROR_SUCCESS;
    explicit operator bool() const { return match != PipeLoginMatch::Denied; }
};

inline PipeLoginDecision pipeLoginDecision(const UacLoginIdentity &identity,
                                           const ClientEnvironment &caller,
                                           ULONG peerSession, bool inputOnly) {
    const auto &host = identity.local;
    if (!host.tokenKnown || !caller.tokenKnown)
        return {PipeLoginMatch::Denied, PipeLoginReason::UnknownToken};
    if (peerSession != host.sessionId)
        return {PipeLoginMatch::Denied, PipeLoginReason::PeerSession};
    const bool anonymous = caller.userSid == L"S-1-5-7";
    if (!anonymous && caller.sessionId != host.sessionId)
        return {PipeLoginMatch::Denied, PipeLoginReason::TokenSession};
    if (sameLogin(host, caller))
        return {PipeLoginMatch::ExactLogin};
    // Sandboxie opens with the original user's authority under the existing
    // user-only DACL, then exposes an anonymous sender. Only the kernel peer
    // session retains the session boundary in this separate exception.
    if (!inputOnly && !caller.appContainer && anonymous)
        return {PipeLoginMatch::SandboxAnonymous};
    if (inputOnly)
        return {PipeLoginMatch::Denied, PipeLoginReason::InputOnly};
    if (host.isolation != IsolationKind::Host || host.appContainer ||
        caller.appContainer || host.lowIntegrity || caller.lowIntegrity)
        return {PipeLoginMatch::Denied, PipeLoginReason::RestrictedEnvironment};
    if (host.userSid != caller.userSid)
        return {PipeLoginMatch::Denied, PipeLoginReason::DifferentUser};
    if (!validUacLink(identity))
        return {PipeLoginMatch::Denied, PipeLoginReason::NoUacLink};
    if (!caller.elevationKnown ||
        caller.elevationType != identity.linked.elevationType ||
        !sameLogin(identity.linked, caller))
        return {PipeLoginMatch::Denied, PipeLoginReason::DifferentLogin};
    return {PipeLoginMatch::UacLinkedLogin};
}

inline PipeLoginDecision authorizedPipeClient(HANDLE pipe,
                                              const UacLoginIdentity &host,
                                              ClientEnvironment &caller,
                                              bool inputOnly) {
    DWORD error = ERROR_SUCCESS;
    if (!pipeClientIdentity(pipe, caller, &error))
        return {PipeLoginMatch::Denied, PipeLoginReason::TokenQuery, error};
    ULONG session = 0;
    if (!GetNamedPipeClientSessionId(pipe, &session))
        return {PipeLoginMatch::Denied, PipeLoginReason::SessionQuery,
                GetLastError()};
    return pipeLoginDecision(host, caller, session, inputOnly);
}

inline bool authorizedPipeClient(HANDLE pipe, const ClientEnvironment &host,
                                 ClientEnvironment &caller, bool inputOnly) {
    return static_cast<bool>(
        authorizedPipeClient(pipe, UacLoginIdentity{host}, caller, inputOnly));
}

inline bool pipeServerIdentity(HANDLE pipe, const ClientEnvironment &client,
                               DWORD &serverId) {
    ULONG pid = 0, session = 0;
    if (!GetNamedPipeServerProcessId(pipe, &pid) ||
        !GetNamedPipeServerSessionId(pipe, &session) ||
        session != client.sessionId)
        return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    HANDLE token = nullptr;
    ClientEnvironment server;
    if (OpenProcessToken(process, TOKEN_QUERY, &token)) {
        server = tokenEnvironment(token);
        CloseHandle(token);
    }
    CloseHandle(process);
    if (!sameLogin(client, server) || server.appContainer ||
        server.lowIntegrity)
        return false;
    serverId = pid;
    return true;
}

inline std::wstring inputPipeSecurity(const std::wstring &user) {
    // 0x100183 excludes FILE_CREATE_PIPE_INSTANCE (0x4). Only this endpoint
    // is writable at low integrity; the management pipe keeps its original ACL.
    return L"D:P(A;;GA;;;" + user + L")(A;;0x100183;;;AC)S:(ML;;NW;;;LW)";
}

} // namespace fcitx::win32::ipc
