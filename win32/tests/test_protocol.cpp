#include "../ipc/protocol.h"
#include <cassert>
#include <string>

using namespace fcitx::win32::ipc;

int main() {
    Frame source;
    source.type = MessageType::KeyDown;
    source.requestId = 42;
    source.contextId = 7;
    Writer writer;
    writer.u32(65);
    writer.string("测试");
    source.payload = writer.take();

    Frame decoded;
    assert(decodeFrame(encodeFrame(source), decoded));
    assert(decoded.type == source.type);
    assert(decoded.requestId == source.requestId);
    assert(decoded.contextId == source.contextId);
    Reader reader(decoded.payload.data(), decoded.payload.size());
    uint32_t key = 0;
    std::string text;
    assert(reader.u32(key));
    assert(reader.string(text));
    assert(key == 65);
    assert(text == "测试");

    auto invalid = encodeFrame(source);
    invalid[0] ^= 0xff;
    assert(!decodeFrame(invalid, decoded));
    return 0;
}