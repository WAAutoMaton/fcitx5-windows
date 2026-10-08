#include "../tsf/pipeclient.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

fcitx::PipeClient::KeyReply key(fcitx::PipeClient &client, uint64_t context,
                                uint32_t value, uint32_t modifiers = 0) {
    fcitx::PipeClient::KeyReply reply;
    if (!client.key(context, false, value, 0, modifiers, 0, reply)) {
        throw std::runtime_error("key request failed for VK " +
                                 std::to_string(value));
    }
    return reply;
}

fcitx::PipeClient::KeyReply type(fcitx::PipeClient &client, uint64_t context,
                                 const std::string &text) {
    fcitx::PipeClient::KeyReply reply;
    for (const auto letter : text) {
        reply = key(client, context, static_cast<uint32_t>(letter));
        require(reply.consumed && reply.commit.empty(),
                "pinyin letter was not composed");
    }
    return reply;
}

uint64_t create(fcitx::PipeClient &client) {
    uint64_t context = 0;
    require(client.connect(), "Core connection failed");
    require(client.createContext(1ULL << 1, context) && client.focusIn(context),
            "context creation failed");
    return context;
}

} // namespace

int main(int argumentCount, char **arguments) {
    try {
        fcitx::PipeClient client;
        const auto context = create(client);
        if (argumentCount == 2 && std::string(arguments[1]) == "--ready") {
            require(client.focusOut(context) && client.destroyContext(context),
                    "ready context cleanup failed");
            return 0;
        }
        auto reply = type(client, context, "NIHAO");
        const std::string hello = "\xe4\xbd\xa0\xe5\xa5\xbd";
        require(reply.enabled && !reply.preedit.empty() &&
                    !reply.candidates.empty(),
                "pinyin snapshot missing");
        const auto candidate = std::find_if(
            reply.candidates.begin(), reply.candidates.end(),
            [&hello](const auto &item) { return item.text == hello; });
        require(candidate != reply.candidates.end(),
                "real dictionary did not produce nihao");
        const auto index =
            static_cast<uint32_t>(candidate - reply.candidates.begin());
        require(index < 9, "candidate is not on the first page");
        const auto selected = key(client, context, '1' + index);
        require(selected.consumed && selected.commit == hello &&
                    selected.preedit.empty(),
                "numeric selection failed");
        require(client.reset(context, reply), "reset failed");
        reply = type(client, context, "NIHAO");
        const auto first = reply.candidates.front().text;
        reply = key(client, context, VK_SPACE);
        require(reply.consumed && reply.commit == first,
                "space selection failed");
        require(client.reset(context, reply), "reset failed");
        reply = type(client, context, "NI");
        reply = key(client, context, VK_BACK);
        require(reply.consumed && !reply.preedit.empty(), "backspace failed");
        reply = key(client, context, VK_ESCAPE);
        require(reply.consumed && reply.preedit.empty() &&
                    reply.candidates.empty(),
                "escape failed");
        reply = key(client, context, 'C', 2);
        require(!reply.consumed && reply.commit.empty(),
                "Ctrl+C was intercepted");
        reply = key(client, context, VK_SPACE, 2);
        require(reply.consumed && !reply.enabled, "English mode switch failed");
        reply = key(client, context, 'A');
        require(!reply.consumed && reply.commit.empty(),
                "English key was not passed through");
        reply = key(client, context, VK_SPACE, 2);
        require(reply.consumed && reply.enabled, "Chinese mode switch failed");
        type(client, context, "NI");
        fcitx::PipeClient second;
        const auto secondContext = create(second);
        require(second.poll(secondContext, reply) && reply.preedit.empty(),
                "contexts leaked preedit");
        require(client.poll(context, reply) && !reply.preedit.empty(),
                "second connection reset first context");
        fcitx::PipeClient::KeyReply unauthorized;
        require(!second.poll(context, unauthorized),
                "cross-connection context access allowed");
        require(client.focusOut(context) && client.destroyContext(context),
                "context destruction failed");
        require(second.focusOut(secondContext) &&
                    second.destroyContext(secondContext),
                "second destruction failed");
        std::cout
            << "PASS: real pinyin preedit, candidates, numeric/space "
               "selection, reset, shortcuts, modes and isolated clients\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
