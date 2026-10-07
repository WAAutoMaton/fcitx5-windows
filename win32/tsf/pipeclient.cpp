#include "pipeclient.h"
#include <vector>

namespace fcitx {
namespace ipc = win32::ipc;
namespace {

bool readPipeMessage(HANDLE pipe, std::vector<uint8_t> &data) {
    data.resize(ipc::kHeaderSize + ipc::kMaxPayloadSize);
    DWORD bytesRead = 0;
    if (!ReadFile(pipe, data.data(), static_cast<DWORD>(data.size()),
                  &bytesRead, nullptr)) {
        if (GetLastError() != ERROR_MORE_DATA) {
            return false;
        }
    }
    data.resize(bytesRead);
    return bytesRead >= ipc::kHeaderSize;
}

bool writePipeMessage(HANDLE pipe, const std::vector<uint8_t> &data) {
    DWORD bytesWritten = 0;
    return WriteFile(pipe, data.data(), static_cast<DWORD>(data.size()),
                     &bytesWritten, nullptr) &&
           bytesWritten == data.size();
}

bool isExpected(const ipc::Frame &frame, ipc::MessageType type) {
    return frame.type == type && frame.payload.size() <= ipc::kMaxPayloadSize;
}

} // namespace

PipeClient::~PipeClient() { disconnect(); }

bool PipeClient::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pipe_ != INVALID_HANDLE_VALUE) {
        return true;
    }
    if (!WaitNamedPipeW(ipc::kPipeName, 1000)) {
        return false;
    }
    pipe_ = CreateFileW(ipc::kPipeName, GENERIC_READ | GENERIC_WRITE, 0,
                        nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe_ == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr)) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        return false;
    }

    ipc::Frame request;
    request.type = ipc::MessageType::Hello;
    request.requestId = nextRequestId_++;
    ipc::Frame response;
    if (!writeFrame(request) || !readFrame(response) ||
        !isExpected(response, ipc::MessageType::HelloReply)) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        return false;
    }
    ipc::Reader reader(response.payload.data(), response.payload.size());
    uint16_t version = 0;
    if (!reader.u16(version) || version != ipc::kVersion) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        return false;
    }
    return true;
}

void PipeClient::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pipe_ != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(pipe_);
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool PipeClient::createContext(uint64_t capabilities, uint64_t &contextId) {
    ipc::Writer writer;
    writer.string("win32-tsf");
    writer.u64(capabilities);
    ipc::Frame response;
    if (!request(ipc::MessageType::CreateContext, 0, writer.take(), response) ||
        !isExpected(response, ipc::MessageType::CreateContextReply)) {
        return false;
    }
    ipc::Reader reader(response.payload.data(), response.payload.size());
    return reader.u64(contextId) && contextId == response.contextId;
}

bool PipeClient::focusIn(uint64_t contextId) {
    ipc::Frame response;
    return request(ipc::MessageType::FocusIn, contextId, {}, response) &&
           isExpected(response, ipc::MessageType::Ack);
}

bool PipeClient::focusOut(uint64_t contextId) {
    ipc::Frame response;
    return request(ipc::MessageType::FocusOut, contextId, {}, response) &&
           isExpected(response, ipc::MessageType::Ack);
}

bool PipeClient::destroyContext(uint64_t contextId) {
    ipc::Frame response;
    return request(ipc::MessageType::DestroyContext, contextId, {}, response) &&
           isExpected(response, ipc::MessageType::Ack);
}

bool PipeClient::key(uint64_t contextId, bool release, uint32_t virtualKey,
                     uint32_t scanCode, uint32_t modifiers, uint32_t time,
                     KeyReply &reply) {
    ipc::Writer writer;
    writer.u32(virtualKey);
    writer.u32(scanCode);
    writer.u32(modifiers);
    writer.u32(time);
    ipc::Frame response;
    if (!request(release ? ipc::MessageType::KeyUp : ipc::MessageType::KeyDown,
                 contextId, writer.take(), response) ||
        !isExpected(response, ipc::MessageType::KeyReply)) {
        return false;
    }
    ipc::Reader reader(response.payload.data(), response.payload.size());
    uint8_t consumed = 0;
    return reader.u8(consumed) && reader.string(reply.commit) &&
           reader.string(reply.preedit) && reader.u32(reply.preeditCursor) &&
           (reply.consumed = consumed != 0, true);
}

bool PipeClient::request(ipc::MessageType type, uint64_t contextId,
                         const std::vector<uint8_t> &payload,
                         ipc::Frame &response) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pipe_ == INVALID_HANDLE_VALUE) {
        return false;
    }
    ipc::Frame request;
    request.type = type;
    request.requestId = nextRequestId_++;
    request.contextId = contextId;
    request.payload = payload;
    if (!writeFrame(request) || !readFrame(response) ||
        response.requestId != request.requestId) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        return false;
    }
    if (response.type == ipc::MessageType::Error) {
        return false;
    }
    return true;
}

bool PipeClient::readFrame(ipc::Frame &frame) {
    std::vector<uint8_t> data;
    return readPipeMessage(pipe_, data) && ipc::decodeFrame(data, frame);
}

bool PipeClient::writeFrame(const ipc::Frame &frame) {
    return writePipeMessage(pipe_, ipc::encodeFrame(frame));
}

} // namespace fcitx