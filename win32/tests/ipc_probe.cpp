#include "../tsf/pipeclient.h"
#include <cassert>

int main() {
    fcitx::PipeClient client;
    if (!client.connect()) {
        return 1;
    }
    uint64_t contextId = 0;
    if (!client.createContext((1ULL << 1) | (1ULL << 4), contextId) ||
        !client.focusIn(contextId)) {
        return 2;
    }

    for (uint32_t key : {static_cast<uint32_t>('A'),
                         static_cast<uint32_t>('B'),
                         static_cast<uint32_t>(VK_SPACE)}) {
        fcitx::PipeClient::KeyReply reply;
        if (!client.key(contextId, false, key, 0, 0, 0, reply) ||
            !reply.consumed || reply.commit.size() != 1) {
            return 3;
        }
    }

    assert(client.focusOut(contextId));
    assert(client.destroyContext(contextId));
    return 0;
}