#include "../ipc/isolationpolicy.h"
#include "../ipc/pipeidentity.h"
#include <cassert>

int main() {
    namespace ipc = fcitx::win32::ipc;
    auto environment = ipc::clientEnvironment();
    assert(environment.tokenKnown && !environment.appContainer);
    assert(ipc::sameLogin(environment, environment));
    auto other = environment;
    other.userSid = L"S-1-0-0";
    assert(!ipc::sameLogin(environment, other));
    other = environment;
    ++other.sessionId;
    assert(!ipc::sameLogin(environment, other));
    other = environment;
    ++other.authenticationId.LowPart;
    assert(!ipc::sameLogin(environment, other));
    using Kind = ipc::IsolationKind;
    using Policy = ipc::IsolationPolicy;
    environment.isolation = Kind::Host;
    environment.lowIntegrity = false;
    assert(ipc::isolationPolicy(environment) == Policy::HostServices);
    environment.lowIntegrity = true;
    assert(ipc::isolationPolicy(environment) == Policy::HostOnly);
    environment.isolation = Kind::AppContainer;
    assert(ipc::isolationPolicy(environment) == Policy::RestrictedInput);
    environment.isolation = Kind::Sandboxie;
    assert(ipc::isolationPolicy(environment) == Policy::HostOnly);
    environment.isolation = Kind::Unknown;
    assert(ipc::isolationPolicy(environment) == Policy::HostOnly);
    environment.appContainer = true;
    assert(ipc::isolationPolicy(environment) == Policy::Refuse);
    environment.appContainer = false;
    environment.isolation = Kind::SandboxieAppContainer;
    assert(ipc::isolationPolicy(environment) == Policy::Refuse);
    for (unsigned i = 0; i <= 65535; ++i) {
        const auto type = static_cast<ipc::MessageType>(i);
        if (ipc::restrictedRequestAllowed(type)) {
            assert(!ipc::hostCommand(type));
            assert(type != ipc::MessageType::SetSettings &&
                   type != ipc::MessageType::SetGlobalSettings &&
                   type != ipc::MessageType::SetInputMethods &&
                   type != ipc::MessageType::SetMethodConfig &&
                   type != ipc::MessageType::GetDictionaries &&
                   type != ipc::MessageType::GetPlugins &&
                   type != ipc::MessageType::GetMethodConfig);
        }
    }
    assert(ipc::restrictedRequestAllowed(ipc::MessageType::KeyDown));
    assert(ipc::restrictedRequestAllowed(ipc::MessageType::OpenSettings));
    assert(!(ipc::kInputPipeAccess & FILE_CREATE_PIPE_INSTANCE));
}
