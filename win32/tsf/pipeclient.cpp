#include "pipeclient.h"
#include "../ipc/transport.h"

namespace fcitx {

PipeClient::~PipeClient() { disconnect(); }

bool PipeClient::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected()) {
        return true;
    }
    const auto name = name_.empty() ? ipc::pipeName() : name_;
    serverProcessId_ = 0;
    connectionStage_ = "WaitNamedPipe";
    if (name.empty() || !WaitNamedPipeW(name.c_str(), 50)) {
        connectionError_ = name.empty() ? ERROR_INVALID_NAME : GetLastError();
        return false;
    }
    connectionStage_ = "CreateFile";
    pipe_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (!connected()) {
        connectionError_ = GetLastError();
        return false;
    }
    ULONG server = 0;
    connectionStage_ = "server ownership";
    if (sandbox_.hostOnly() && (!GetNamedPipeServerProcessId(pipe_, &server) ||
                                !sandbox_.acceptsServer(server))) {
        connectionError_ = ERROR_ACCESS_DENIED;
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        OutputDebugStringW(
            L"Fcitx5: refusing sandboxed or unknown Pipe server.\n");
        return false;
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    const auto fail = [this](DWORD error) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        connectionError_ = error;
        return false;
    };
    ipc::Frame request;
    request.type = ipc::MessageType::Hello;
    request.requestId = nextRequestId_++;
    ipc::Frame response;
    uint16_t version = 0;
    connectionStage_ = "SetNamedPipeHandleState";
    if (!SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr))
        return fail(GetLastError());
    connectionStage_ = "Hello write";
    if (!writeFrame(request))
        return fail(GetLastError());
    connectionStage_ = "Hello read";
    if (!readFrame(response))
        return fail(GetLastError());
    connectionStage_ = "Hello validation";
    if (response.type != ipc::MessageType::HelloReply ||
        response.requestId != request.requestId || response.contextId != 0)
        return fail(ERROR_INVALID_DATA);
    ipc::Reader reader(response.payload.data(), response.payload.size());
    if (!reader.u16(version) || reader.remaining())
        return fail(ERROR_INVALID_DATA);
    if (version != ipc::kVersion)
        return fail(ERROR_REVISION_MISMATCH);
    connectionError_ = ERROR_SUCCESS;
    if (GetNamedPipeServerProcessId(pipe_, &server))
        serverProcessId_ = server;
    connectionStage_ = "connected";
    return true;
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

bool PipeClient::setMode(uint64_t contextId, bool enabled, KeyReply &reply) {
    ipc::Writer writer;
    writer.u8(enabled);
    ipc::Frame response;
    return request(ipc::MessageType::SetMode, contextId, writer.take(),
                   response) &&
           response.type == ipc::MessageType::KeyReply &&
           ipc::decodeKeyReply(response.payload, reply);
}

bool PipeClient::cycleInputMethod(uint64_t contextId, KeyReply &reply) {
    ipc::Frame response;
    return request(ipc::MessageType::CycleInputMethod, contextId, {},
                   response) &&
           response.type == ipc::MessageType::KeyReply &&
           ipc::decodeKeyReply(response.payload, reply);
}
bool PipeClient::selectInputMethod(uint64_t contextId, const std::string &id,
                                   KeyReply &reply) {
    ipc::Writer writer;
    writer.string(id);
    ipc::Frame response;
    return request(ipc::MessageType::SelectInputMethod, contextId,
                   writer.take(), response) &&
           response.type == ipc::MessageType::KeyReply &&
           ipc::decodeKeyReply(response.payload, reply);
}
bool PipeClient::getInputMethods(ipc::InputMethodsReply &reply) {
    ipc::Frame response;
    return request(ipc::MessageType::GetInputMethods, 0, {}, response) &&
           response.type == ipc::MessageType::InputMethodsReply &&
           ipc::decodeInputMethods(response.payload, reply);
}
bool PipeClient::setInputMethods(const ipc::InputMethodSelection &selection,
                                 ipc::InputMethodsReply &reply) {
    ipc::Frame response;
    return request(ipc::MessageType::SetInputMethods, 0,
                   ipc::encodeSelection(selection), response) &&
           response.type == ipc::MessageType::InputMethodsReply &&
           ipc::decodeInputMethods(response.payload, reply);
}
bool PipeClient::getMethodConfig(const std::string &id,
                                 ipc::MethodConfigReply &reply) {
    ipc::Frame response;
    return request(ipc::MessageType::GetMethodConfig, 0,
                   ipc::encodeMethodConfigRequest({id, 0, {}}), response) &&
           response.type == ipc::MessageType::MethodConfigReply &&
           ipc::decodeMethodConfig(response.payload, reply);
}
bool PipeClient::setMethodConfig(const ipc::MethodConfigRequest &config,
                                 ipc::MethodConfigReply &reply) {
    ipc::Frame response;
    return request(ipc::MessageType::SetMethodConfig, 0,
                   ipc::encodeMethodConfigRequest(config), response) &&
           response.type == ipc::MessageType::MethodConfigReply &&
           ipc::decodeMethodConfig(response.payload, reply);
}

bool PipeClient::getSettings(ipc::SettingsReply &reply) {
    ipc::Frame response;
    return request(ipc::MessageType::GetSettings, 0, {}, response) &&
           response.type == ipc::MessageType::SettingsReply &&
           ipc::decodeSettingsReply(response.payload, reply);
}

bool PipeClient::getDictionaries(ipc::DictionariesReply &reply) {
    ipc::Frame response;
    return request(ipc::MessageType::GetDictionaries, 0, {}, response) &&
           response.type == ipc::MessageType::DictionariesReply &&
           ipc::decodeDictionariesReply(response.payload, reply);
}

bool PipeClient::getGlobalSettings(ipc::GlobalSettingsReply &reply) {
    ipc::Frame response;
    return request(ipc::MessageType::GetGlobalSettings, 0, {}, response) &&
           response.type == ipc::MessageType::GlobalSettingsReply &&
           ipc::decodeGlobalSettingsReply(response.payload, reply);
}

bool PipeClient::setGlobalSettings(const ipc::GlobalSettings &settings,
                                   ipc::GlobalSettingsReply &reply) {
    ipc::Frame response;
    return request(ipc::MessageType::SetGlobalSettings, 0,
                   ipc::encodeGlobalSettings(settings), response) &&
           response.type == ipc::MessageType::GlobalSettingsReply &&
           ipc::decodeGlobalSettingsReply(response.payload, reply);
}

bool PipeClient::setSettings(const ipc::PinyinSettings &settings,
                             ipc::SettingsReply &reply) {
    ipc::Frame response;
    return request(ipc::MessageType::SetSettings, 0,
                   ipc::encodeSettings(settings), response) &&
           response.type == ipc::MessageType::SettingsReply &&
           ipc::decodeSettingsReply(response.payload, reply);
}

bool PipeClient::openSettings(ipc::SettingsReply &reply) {
    ULONG server = 0;
    if (connected() && GetNamedPipeServerProcessId(pipe_, &server)) {
        AllowSetForegroundWindow(server);
    }
    ipc::Frame response;
    return request(ipc::MessageType::OpenSettings, 0, {}, response) &&
           response.type == ipc::MessageType::SettingsReply &&
           ipc::decodeSettingsReply(response.payload, reply);
}

bool PipeClient::hostCommand(ipc::MessageType command, DWORD &error) {
    error = ERROR_INVALID_PARAMETER;
    if (!ipc::hostCommand(command))
        return false;
    ULONG server = 0;
    if (connected() && GetNamedPipeServerProcessId(pipe_, &server))
        AllowSetForegroundWindow(server);
    ipc::Frame response;
    if (!request(command, 0, {}, response) ||
        response.type != ipc::MessageType::ServiceReply)
        return false;
    ipc::Reader reader(response.payload.data(), response.payload.size());
    uint32_t result = 0;
    if (!reader.u32(result) || reader.remaining())
        return false;
    error = result;
    return true;
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
