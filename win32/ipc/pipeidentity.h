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

inline bool pipeClientIdentity(HANDLE pipe, ClientEnvironment &identity) {
    identity = {};
    if (!ImpersonateNamedPipeClient(pipe))
        return false;
    HANDLE token = nullptr;
    bool success =
        OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token) != FALSE;
    try {
        if (success)
            identity = tokenEnvironment(token);
    } catch (...) {
        if (token)
            CloseHandle(token);
        if (!RevertToSelf())
            std::terminate();
        return false;
    }
    if (token)
        CloseHandle(token);
    if (!RevertToSelf())
        std::terminate();
    return success && identity.tokenKnown;
}

inline bool authorizedPipeClient(HANDLE pipe, const ClientEnvironment &host,
                                 ClientEnvironment &caller, bool inputOnly) {
    ULONG session = 0;
    if (!host.tokenKnown || !pipeClientIdentity(pipe, caller) ||
        !GetNamedPipeClientSessionId(pipe, &session) ||
        session != host.sessionId)
        return false;
    if (sameLogin(host, caller))
        return true;
    // Sandboxie security mode exposes an anonymous token to the host after
    // opening the management pipe with the original user's authority. Its
    // existing user-only DACL authorizes that open; use the kernel peer session
    // here because the sandbox token does not retain the user's logon identity.
    return !inputOnly && !caller.appContainer && caller.userSid == L"S-1-5-7";
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
