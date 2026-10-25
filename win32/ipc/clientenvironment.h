#pragma once

#include "sandbox.h"
#include <appmodel.h>
#include <sddl.h>
#include <string>
#include <vector>

namespace fcitx::win32::ipc {

enum class IsolationKind {
    Host,
    Sandboxie,
    AppContainer,
    SandboxieAppContainer,
    Unknown
};

struct ClientEnvironment {
    IsolationKind isolation = IsolationKind::Unknown;
    std::wstring userSid;
    std::wstring appContainerSid;
    DWORD sessionId = 0;
    LUID authenticationId{};
    bool lowIntegrity = false;
    bool hasPackageIdentity = false;
    bool tokenKnown = false;
    bool appContainer = false;
    TOKEN_ELEVATION_TYPE elevationType = TokenElevationTypeDefault;
    bool elevationKnown = false;
};

class TokenQueryHandle {
  public:
    explicit TokenQueryHandle(HANDLE value) : value_(value) {}
    ~TokenQueryHandle() {
        if (value_)
            CloseHandle(value_);
    }
    TokenQueryHandle(const TokenQueryHandle &) = delete;
    TokenQueryHandle &operator=(const TokenQueryHandle &) = delete;

  private:
    HANDLE value_;
};

inline std::wstring sidString(PSID sid) {
    LPWSTR value = nullptr;
    if (!sid || !IsValidSid(sid) || !ConvertSidToStringSidW(sid, &value))
        return {};
    std::wstring result(value);
    LocalFree(value);
    return result;
}

inline std::vector<BYTE> tokenData(HANDLE token, TOKEN_INFORMATION_CLASS kind) {
    DWORD size = 0;
    GetTokenInformation(token, kind, nullptr, 0, &size);
    std::vector<BYTE> data(size);
    if (!size || !GetTokenInformation(token, kind, data.data(), size, &size))
        return {};
    return data;
}

inline ClientEnvironment tokenEnvironment(HANDLE token) {
    ClientEnvironment result;
    DWORD appContainer = 0, size = 0;
    TOKEN_STATISTICS statistics{};
    auto user = tokenData(token, TokenUser);
    auto integrity = tokenData(token, TokenIntegrityLevel);
    if (user.empty() || integrity.empty() ||
        !GetTokenInformation(token, TokenIsAppContainer, &appContainer,
                             sizeof(appContainer), &size) ||
        !GetTokenInformation(token, TokenSessionId, &result.sessionId,
                             sizeof(result.sessionId), &size) ||
        !GetTokenInformation(token, TokenStatistics, &statistics,
                             sizeof(statistics), &size))
        return result;
    result.userSid =
        sidString(reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid);
    auto sid =
        reinterpret_cast<TOKEN_MANDATORY_LABEL *>(integrity.data())->Label.Sid;
    if (result.userSid.empty() || !IsValidSid(sid) ||
        !*GetSidSubAuthorityCount(sid))
        return result;
    result.lowIntegrity =
        *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1) <
        SECURITY_MANDATORY_MEDIUM_RID;
    result.authenticationId = statistics.AuthenticationId;
    result.appContainer = appContainer != 0;
    if (result.appContainer) {
        auto package = tokenData(token, TokenAppContainerSid);
        if (package.empty())
            return result;
        result.appContainerSid = sidString(
            reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION *>(package.data())
                ->TokenAppContainer);
        if (result.appContainerSid.empty())
            return result;
    }
    result.tokenKnown = true;
    result.elevationKnown =
        GetTokenInformation(token, TokenElevationType, &result.elevationType,
                            sizeof(result.elevationType), &size) != FALSE;
    result.isolation =
        result.appContainer ? IsolationKind::AppContainer : IsolationKind::Host;
    return result;
}

inline void classifyClientEnvironment(ClientEnvironment &result,
                                      const SandboxieApi &sandbox) {
    UINT32 size = 0;
    result.hasPackageIdentity =
        GetCurrentPackageFullName(&size, nullptr) == ERROR_INSUFFICIENT_BUFFER;
    const auto state = sandbox.process(GetCurrentProcessId());
    if (!result.tokenKnown || state == SandboxState::Unknown)
        result.isolation = IsolationKind::Unknown;
    else if (state == SandboxState::Boxed)
        result.isolation = result.appContainer
                               ? IsolationKind::SandboxieAppContainer
                               : IsolationKind::Sandboxie;
}

inline ClientEnvironment clientEnvironment(const SandboxieApi &sandbox = {}) {
    HANDLE token = nullptr;
    ClientEnvironment result;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        const TokenQueryHandle lifetime(token);
        result = tokenEnvironment(token);
    }
    classifyClientEnvironment(result, sandbox);
    return result;
}

inline bool sameLogin(const ClientEnvironment &a, const ClientEnvironment &b) {
    return a.tokenKnown && b.tokenKnown && a.userSid == b.userSid &&
           a.sessionId == b.sessionId &&
           a.authenticationId.HighPart == b.authenticationId.HighPart &&
           a.authenticationId.LowPart == b.authenticationId.LowPart;
}

struct UacLoginIdentity {
    ClientEnvironment local;
    ClientEnvironment linked;
    DWORD linkedError = ERROR_NO_SUCH_LOGON_SESSION;
};

inline bool validUacLink(const UacLoginIdentity &identity) {
    const auto &a = identity.local;
    const auto &b = identity.linked;
    return identity.linkedError == ERROR_SUCCESS && a.tokenKnown &&
           b.tokenKnown && a.elevationKnown && b.elevationKnown &&
           a.userSid == b.userSid && a.sessionId == b.sessionId &&
           !a.appContainer && !b.appContainer && !a.lowIntegrity &&
           !b.lowIntegrity &&
           ((a.elevationType == TokenElevationTypeLimited &&
             b.elevationType == TokenElevationTypeFull) ||
            (a.elevationType == TokenElevationTypeFull &&
             b.elevationType == TokenElevationTypeLimited));
}

// Always query our primary token before impersonating a pipe peer.
inline UacLoginIdentity uacLoginIdentity(const SandboxieApi &sandbox = {}) {
    UacLoginIdentity result;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        result.linkedError = GetLastError();
        return result;
    }
    const TokenQueryHandle lifetime(token);
    result.local = tokenEnvironment(token);
    classifyClientEnvironment(result.local, sandbox);
    if (!result.local.elevationKnown ||
        result.local.elevationType == TokenElevationTypeDefault)
        return result;
    TOKEN_LINKED_TOKEN linked{};
    DWORD size = 0;
    if (!GetTokenInformation(token, TokenLinkedToken, &linked, sizeof(linked),
                             &size)) {
        result.linkedError = GetLastError();
        return result;
    }
    const TokenQueryHandle linkedLifetime(linked.LinkedToken);
    result.linked = tokenEnvironment(linked.LinkedToken);
    result.linkedError =
        result.linked.tokenKnown ? ERROR_SUCCESS : ERROR_INVALID_DATA;
    return result;
}

} // namespace fcitx::win32::ipc
