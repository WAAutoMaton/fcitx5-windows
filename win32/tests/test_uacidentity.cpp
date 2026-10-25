#include "../ipc/pipeidentity.h"
#include <cassert>

namespace ipc = fcitx::win32::ipc;

int main() {
    ipc::UacLoginIdentity identity;
    auto &host = identity.local;
    host.tokenKnown = host.elevationKnown = true;
    host.isolation = ipc::IsolationKind::Host;
    host.userSid = L"S-1-5-21-123";
    host.sessionId = 2;
    host.authenticationId.LowPart = 10;
    host.elevationType = TokenElevationTypeLimited;
    identity.linked = host;
    identity.linked.authenticationId.LowPart = 20;
    identity.linked.elevationType = TokenElevationTypeFull;
    identity.linkedError = ERROR_SUCCESS;
    assert(ipc::validUacLink(identity));
    const auto caller = identity.linked;
    using Match = ipc::PipeLoginMatch;
    const auto decide = [](const auto &snapshot, const auto &peer,
                           bool input = false) {
        return ipc::pipeLoginDecision(snapshot, peer, 2, input).match;
    };
    assert(decide(identity, host) == Match::ExactLogin);
    assert(decide(identity, host, true) == Match::ExactLogin);
    assert(!ipc::sameLogin(host, caller));
    assert(decide(identity, caller) == Match::UacLinkedLogin);
    assert(decide(identity, caller, true) == Match::Denied);
    auto reverse = identity;
    std::swap(reverse.local, reverse.linked);
    assert(decide(reverse, host) == Match::UacLinkedLogin);

    for (unsigned mutation = 0; mutation < 12; ++mutation) {
        auto peer = caller;
        switch (mutation) {
        case 0:
            peer.userSid = L"S-1-5-21-456";
            break;
        case 1:
            ++peer.sessionId;
            break;
        case 2:
            ++peer.authenticationId.LowPart;
            break;
        case 3:
            ++peer.authenticationId.HighPart;
            break;
        case 4:
            peer.elevationKnown = false;
            break;
        case 5:
            peer.elevationType = TokenElevationTypeLimited;
            break;
        case 6:
            peer.elevationType = TokenElevationTypeDefault;
            break;
        case 7:
            peer.appContainer = true;
            break;
        case 8:
            peer.lowIntegrity = true;
            break;
        case 9:
            peer.tokenKnown = false;
            break;
        case 10:
            peer.userSid = L"S-1-5-7";
            peer.appContainer = true;
            break;
        case 11:
            peer.userSid.clear();
            break;
        }
        assert(decide(identity, peer) == Match::Denied);
    }
    for (unsigned mutation = 0; mutation < 14; ++mutation) {
        auto snapshot = identity;
        switch (mutation) {
        case 0:
            snapshot.linkedError = ERROR_ACCESS_DENIED;
            break;
        case 1:
            snapshot.linked.tokenKnown = false;
            break;
        case 2:
            snapshot.linked.elevationKnown = false;
            break;
        case 3:
            snapshot.linked.lowIntegrity = true;
            break;
        case 4:
            snapshot.linked.appContainer = true;
            break;
        case 5:
            ++snapshot.linked.sessionId;
            break;
        case 6:
            snapshot.linked.userSid = L"S-1-5-21-456";
            break;
        case 7:
            snapshot.linked.elevationType = TokenElevationTypeLimited;
            break;
        case 8:
            snapshot.local.elevationKnown = false;
            break;
        case 9:
            snapshot.local.elevationType = TokenElevationTypeDefault;
            break;
        case 10:
            snapshot.local.lowIntegrity = true;
            break;
        case 11:
            snapshot.local.appContainer = true;
            break;
        case 12:
            snapshot.local.isolation = ipc::IsolationKind::Unknown;
            break;
        case 13:
            snapshot.local.isolation = ipc::IsolationKind::Sandboxie;
            break;
        }
        assert(decide(snapshot, caller) == Match::Denied);
        // Missing linked metadata cannot break an existing exact login.
        assert(decide(snapshot, snapshot.local) == Match::ExactLogin);
    }
    auto unknown = identity;
    unknown.local.tokenKnown = false;
    assert(decide(unknown, caller) == Match::Denied);
    assert(!ipc::pipeLoginDecision(identity, caller, 3, false));
    auto anonymous = caller;
    anonymous.userSid = L"S-1-5-7";
    anonymous.sessionId = 0;
    assert(decide(identity, anonymous) == Match::SandboxAnonymous);
    assert(decide(identity, anonymous, true) == Match::Denied);
    assert(!ipc::pipeLoginDecision(identity, anonymous, 3, false));
    auto strict = identity;
    strict.linked = {};
    strict.linkedError = ERROR_ACCESS_DENIED;
    assert(decide(strict, host) == Match::ExactLogin);
    assert(decide(strict, host, true) == Match::ExactLogin);
    assert(decide(strict, caller) == Match::Denied);
}
