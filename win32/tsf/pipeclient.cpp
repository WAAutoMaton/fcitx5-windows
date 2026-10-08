#include "pipeclient.h"
#include "../ipc/transport.h"

namespace fcitx {

PipeClient::~PipeClient() { disconnect(); }

bool PipeClient::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected()) {
        return true;
    }
    const auto name = ipc::pipeName();
    if (name.empty() || !WaitNamedPipeW(name.c_str(), 50)) {
        return false;
    }
    pipe_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (!connected()) {
        return false;
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    ipc::Frame request;
    request.type = ipc::MessageType::Hello;
    request.requestId = nextRequestId_++;
    ipc::Frame response;
    uint16_t version = 0;
    if (SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr) &&
        writeFrame(request) && readFrame(response) &&
        response.type == ipc::MessageType::HelloReply &&
        response.requestId == request.requestId && response.contextId == 0) {
        ipc::Reader reader(response.payload.data(), response.payload.size());
        if (reader.u16(version) && version == ipc::kVersion &&
            !reader.remaining()) {
            return true;
        }
    }
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
    return false;
}

void PipeClient::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected()) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool PipeClient::createContext(uint64_t capabilities, uint64_t &contextId) {
    contextId = 0;
    ipc::Writer writer;
    writer.string("win32-tsf");
    writer.u64(capabilities);
    ipc::Frame response;
    if (!request(ipc::MessageType::CreateContext, 0, writer.take(), response) ||
        response.type != ipc::MessageType::CreateContextReply) {
        return false;
    }
    ipc::Reader reader(response.payload.data(), response.payload.size());
    uint64_t result = 0;
    if (!reader.u64(result) || !result || result != response.contextId ||
        reader.remaining()) {
        disconnect();
        return false;
    }
    contextId = result;
    return true;
}

bool PipeClient::focusIn(uint64_t contextId) {
    ipc::Frame response;
    return request(ipc::MessageType::FocusIn, contextId, {}, response) &&
           response.type == ipc::MessageType::Ack && response.payload.empty();
}

bool PipeClient::focusOut(uint64_t contextId) {
    ipc::Frame response;
    return request(ipc::MessageType::FocusOut, contextId, {}, response) &&
           response.type == ipc::MessageType::Ack && response.payload.empty();
}

bool PipeClient::destroyContext(uint64_t contextId) {
    ipc::Frame response;
    return request(ipc::MessageType::DestroyContext, contextId, {}, response) &&
           response.type == ipc::MessageType::Ack && response.payload.empty();
}

bool PipeClient::key(uint64_t contextId, bool release, uint32_t virtualKey,
                     uint32_t scanCode, uint32_t modifiers, uint32_t time,
                     KeyReply &reply, uint32_t unicode) {
    ipc::Writer writer;
    writer.u32(virtualKey);
    writer.u32(scanCode);
    writer.u32(modifiers);
    writer.u32(time);
    writer.u32(unicode);
    ipc::Frame response;
    return request(release ? ipc::MessageType::KeyUp
                           : ipc::MessageType::KeyDown,
                   contextId, writer.take(), response) &&
           response.type == ipc::MessageType::KeyReply &&
           ipc::decodeKeyReply(response.payload, reply);
}

bool PipeClient::poll(uint64_t contextId, KeyReply &reply) {
    ipc::Frame response;
    return request(ipc::MessageType::PollState, contextId, {}, response) &&
           response.type == ipc::MessageType::KeyReply &&
           ipc::decodeKeyReply(response.payload, reply);
}

bool PipeClient::reset(uint64_t contextId, KeyReply &reply) {
    ipc::Frame response;
    return request(ipc::MessageType::Reset, contextId, {}, response) &&
           response.type == ipc::MessageType::KeyReply &&
           ipc::decodeKeyReply(response.payload, reply);
}

bool PipeClient::request(ipc::MessageType type, uint64_t contextId,
                         const std::vector<uint8_t> &payload,
                         ipc::Frame &response) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected()) {
        return false;
    }
    ipc::Frame frame;
    frame.type = type;
    frame.requestId = nextRequestId_++;
    frame.contextId = contextId;
    frame.payload = payload;
    if (!writeFrame(frame) || !readFrame(response) ||
        response.requestId != frame.requestId ||
        (type != ipc::MessageType::CreateContext &&
         response.contextId != contextId)) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        return false;
    }
    return response.type != ipc::MessageType::Error;
}

bool PipeClient::readFrame(ipc::Frame &frame) {
    return ipc::readFrame(pipe_, frame, 500);
}

bool PipeClient::writeFrame(const ipc::Frame &frame) {
    return ipc::writeFrame(pipe_, frame, 500);
}

} // namespace fcitx
