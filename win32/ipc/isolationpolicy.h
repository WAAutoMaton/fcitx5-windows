#pragma once

#include "clientenvironment.h"
#include "protocol.h"
#include <system_error>

namespace fcitx::win32::ipc {

enum class IsolationPolicy { HostServices, HostOnly, RestrictedInput, Refuse };

inline IsolationPolicy isolationPolicy(const ClientEnvironment &environment) {
    if (!environment.tokenKnown)
        return IsolationPolicy::HostOnly;
    switch (environment.isolation) {
    case IsolationKind::Host:
        return environment.lowIntegrity ? IsolationPolicy::HostOnly
                                        : IsolationPolicy::HostServices;
    case IsolationKind::Sandboxie:
        return IsolationPolicy::HostOnly;
    case IsolationKind::Unknown:
        return environment.appContainer ? IsolationPolicy::Refuse
                                        : IsolationPolicy::HostOnly;
    case IsolationKind::AppContainer:
        return IsolationPolicy::RestrictedInput;
    case IsolationKind::SandboxieAppContainer:
        return IsolationPolicy::Refuse;
    }
    return IsolationPolicy::Refuse;
}

inline bool hostOnlyClient() {
    return isolationPolicy(clientEnvironment()) !=
           IsolationPolicy::HostServices;
}

inline bool hostServicesAllowed() { return !hostOnlyClient(); }

inline void requireHostServices() {
    if (!hostServicesAllowed()) {
        OutputDebugStringW(L"Fcitx5: restricted or unknown environment; host "
                           L"services only.\n");
        throw std::system_error(ERROR_ACCESS_DENIED, std::system_category());
    }
}

inline bool restrictedRequestAllowed(MessageType type) {
    switch (type) {
    case MessageType::Hello:
    case MessageType::CreateContext:
    case MessageType::DestroyContext:
    case MessageType::FocusIn:
    case MessageType::FocusOut:
    case MessageType::KeyDown:
    case MessageType::KeyUp:
    case MessageType::PollState:
    case MessageType::Reset:
    case MessageType::SetMode:
    case MessageType::CycleInputMethod:
    case MessageType::SelectInputMethod:
    case MessageType::GetGlobalSettings:
    case MessageType::OpenSettings:
    case MessageType::CandidateAction:
        return true;
    default:
        return false;
    }
}

} // namespace fcitx::win32::ipc
