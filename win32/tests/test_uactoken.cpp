#include "../ipc/clientenvironment.h"
#include <cassert>
#include <iostream>

int main() {
    namespace ipc = fcitx::win32::ipc;
    const auto identity = ipc::uacLoginIdentity();
    std::cout << "tokenKnown=" << identity.local.tokenKnown
              << " elevationKnown=" << identity.local.elevationKnown
              << " elevation=" << identity.local.elevationType
              << " linkedError=" << identity.linkedError
              << " validLink=" << ipc::validUacLink(identity) << '\n';
    assert(identity.local.tokenKnown);
    DWORD handlesBefore = 0, handlesAfter = 0;
    assert(GetProcessHandleCount(GetCurrentProcess(), &handlesBefore));
    for (unsigned i = 0; i < 100; ++i) {
        const auto snapshot = ipc::uacLoginIdentity();
        assert(ipc::sameLogin(identity.local, snapshot.local));
        assert(ipc::validUacLink(identity) == ipc::validUacLink(snapshot));
    }
    assert(GetProcessHandleCount(GetCurrentProcess(), &handlesAfter));
    assert(handlesBefore == handlesAfter);
    assert(ImpersonateAnonymousToken(GetCurrentThread()));
    ipc::UacLoginIdentity impersonated;
    try {
        impersonated = ipc::uacLoginIdentity();
    } catch (...) {
        assert(RevertToSelf());
        throw;
    }
    assert(RevertToSelf());
    // Opening our primary token may be denied while impersonating. Never
    // substitute the anonymous thread token for our process identity.
    assert(!impersonated.local.tokenKnown ||
           ipc::sameLogin(identity.local, impersonated.local));
    if (!impersonated.local.tokenKnown)
        assert(!ipc::validUacLink(impersonated) && impersonated.linkedError);
    assert(identity.local.elevationKnown);
    if (identity.local.elevationType == TokenElevationTypeDefault) {
        std::cout << "SKIP: no split token; real UAC relation not exercised\n";
        return 77;
    }
    assert(identity.linkedError == ERROR_SUCCESS);
    assert(ipc::validUacLink(identity));
    assert(identity.local.userSid == identity.linked.userSid);
    assert(identity.local.sessionId == identity.linked.sessionId);
    return 0;
}
