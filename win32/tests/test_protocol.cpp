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
    invalid = encodeFrame(source);
    invalid[4] = 1;
    assert(!decodeFrame(invalid, decoded));
    KeyReply snapshot;
    snapshot.consumed = true;
    snapshot.preedit = "nihao";
    snapshot.preeditCursor = 5;
    snapshot.revision = 42;
    snapshot.selected = 0;
    snapshot.candidates.push_back({"\xe4\xbd\xa0\xe5\xa5\xbd", "1", "ni hao"});
    KeyReply reply;
    auto payload = encodeKeyReply(snapshot);
    assert(decodeKeyReply(payload, reply));
    assert(reply.consumed && reply.preedit == "nihao" &&
           reply.preeditCursor == 5);
    assert(reply.revision == 42 &&
           reply.candidates.front().text == snapshot.candidates.front().text);
    payload.push_back(0);
    assert(!decodeKeyReply(payload, reply));
    snapshot.preeditCursor = 6;
    assert(!decodeKeyReply(encodeKeyReply(snapshot), reply));
    snapshot.preeditCursor = 5;
    snapshot.selected = 1;
    assert(!decodeKeyReply(encodeKeyReply(snapshot), reply));
    snapshot.selected = UINT32_MAX;
    snapshot.candidates.resize(kMaxCandidates + 1);
    assert(!decodeKeyReply(encodeKeyReply(snapshot), reply));
    return 0;
}
