#include "pipeclient.h"
#include "../ipc/pipeidentity.h"
#include "../ipc/transport.h"
#include <filesystem>

namespace fcitx {
namespace {
struct ConnectionTrace {
    PipeClient &client;
    win32::diagnostic::Logger *logger;
    ULONGLONG started = GetTickCount64();
    ~ConnectionTrace() {
        if (logger && logger->enabled())
            logger->event(win32::diagnostic::Level::Info, "pipe_connect",
                          {{"stage", client.connectionStage()},
                           {"error", client.connectionError()},
                           {"server_pid", client.serverProcessId()},
                           {"elapsed_ms", GetTickCount64() - started}});
    }
};
} // namespace

PipeClient::~PipeClient() { disconnect(); }

bool PipeClient::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    const ConnectionTrace trace{*this, logger_};
    if (connected()) {
        return true;
    }
    const auto environment = ipc::clientEnvironment(sandbox_);
    const auto policy = ipc::isolationPolicy(environment);
    const bool restricted = policy == ipc::IsolationPolicy::RestrictedInput;
    if (policy == ipc::IsolationPolicy::Refuse || !environment.tokenKnown) {
        connectionError_ = ERROR_ACCESS_DENIED;
        connectionStage_ = "client environment";
        return false;
    }
    const auto name =
        name_.empty() ? (restricted ? ipc::inputPipeName() : ipc::pipeName())
                      : name_;
    serverProcessId_ = 0;
    connectionStage_ = "WaitNamedPipe";
    if (name.empty() || !WaitNamedPipeW(name.c_str(), 50)) {
        connectionError_ = name.empty() ? ERROR_INVALID_NAME : GetLastError();
        return false;
    }
    connectionStage_ = "CreateFile";
    pipe_ = CreateFileW(
        name.c_str(),
        restricted ? ipc::kInputPipeAccess : GENERIC_READ | GENERIC_WRITE, 0,
        nullptr, OPEN_EXISTING, ipc::kPipeClientFlags, nullptr);
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
    if (restricted) {
        connectionStage_ = "server identity handshake";
        request.type = ipc::MessageType::AuthenticateServer;
        request.requestId = nextRequestId_++;
        if (!writeFrame(request) || !readFrame(response))
            return fail(GetLastError());
        uint64_t value = 0;
        ipc::Reader identity(response.payload.data(), response.payload.size());
        if (response.type != ipc::MessageType::ServerIdentityReply ||
            response.requestId != request.requestId || response.contextId ||
            !identity.u64(value) || !value || identity.remaining() ||
            value >= UINTPTR_MAX - 1)
            return fail(ERROR_INVALID_DATA);
        const auto process =
            reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(value));
        HMODULE module = nullptr;
        WCHAR modulePath[32768]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ipc::inputPipeName),
                           &module);
        const auto length = GetModuleFileNameW(module, modulePath, 32768);
        const auto expected = length && length < 32768
                                  ? (std::filesystem::path(modulePath)
                                         .parent_path()
                                         .parent_path() /
                                     L"bin" / L"Fcitx5.exe")
                                        .lexically_normal()
                                        .wstring()
                                  : std::wstring{};
        ULONG peerId = 0;
        const bool bound = GetNamedPipeServerProcessId(pipe_, &peerId) &&
                           GetProcessId(process) == peerId;
        const bool verified =
            bound &&
            ipc::verifiedServerHandle(pipe_, process, environment, expected);
        if (bound)
            CloseHandle(process);
        if (!verified)
            return fail(ERROR_ACCESS_DENIED);
    }
    connectionError_ = ERROR_SUCCESS;
    if (GetNamedPipeServerProcessId(pipe_, &server))
        serverProcessId_ = server;
    connectionStage_ = "connected";
    return true;
}

void PipeClient::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected()) {
        if (logger_)
            logger_->event(win32::diagnostic::Level::Info, "pipe_disconnect");
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

bool PipeClient::candidateAction(uint64_t contextId,
                                 uint64_t candidateGeneration,
                                 ipc::CandidateAction action, uint32_t index,
                                 KeyReply &reply) {
    ipc::Writer writer;
    writer.u64(candidateGeneration);
    writer.u8(static_cast<uint8_t>(action));
    writer.u32(index);
    ipc::Frame response;
    return request(ipc::MessageType::CandidateAction, contextId, writer.take(),
                   response) &&
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

bool PipeClient::getPlugins(ipc::PluginsReply &reply) {
    ipc::Frame response;
    return request(ipc::MessageType::GetPlugins, 0, {}, response) &&
           response.type == ipc::MessageType::PluginsReply &&
           ipc::decodePluginsReply(response.payload, reply);
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
    const auto started = GetTickCount64();
    if (!connected()) {
        return false;
    }
    ipc::Frame frame;
    frame.type = type;
    frame.requestId = nextRequestId_++;
    frame.contextId = contextId;
    frame.payload = payload;
    const bool transferred = writeFrame(frame) && readFrame(response);
    if (!transferred ||
        response.requestId != frame.requestId ||
        (type != ipc::MessageType::CreateContext &&
         response.contextId != contextId)) {
        const auto error = transferred ? ERROR_INVALID_DATA : GetLastError();
        if (logger_ && logger_->enabled())
            logger_->event(win32::diagnostic::Level::Warning,
                           "pipe_request_failed",
                           {{"type", static_cast<unsigned>(type)},
                            {"request", frame.requestId},
                            {"payload_bytes", payload.size()},
                            {"error", error},
                            {"elapsed_ms", GetTickCount64() - started}});
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        SetLastError(error);
        return false;
    }
    if (logger_) {
        if (response.type == ipc::MessageType::KeyReply) {
            ipc::KeyReply reply;
            if (ipc::decodeKeyReply(response.payload, reply))
                logger_->setEnabled(reply.detailedLogging);
        } else if (response.type == ipc::MessageType::GlobalSettingsReply) {
            ipc::GlobalSettingsReply reply;
            if (ipc::decodeGlobalSettingsReply(response.payload, reply) &&
                reply.error == ipc::SettingsError::None)
                logger_->setEnabled(reply.settings.detailedLogging);
        }
        if (logger_->enabled())
            logger_->event(win32::diagnostic::Level::Debug, "pipe_request",
                           {{"type", static_cast<unsigned>(type)},
                            {"request", frame.requestId},
                            {"context", contextId},
                            {"payload_bytes", payload.size()},
                 {"reply_bytes", response.payload.size()},
                 {"reply_type", static_cast<unsigned>(response.type)},
                 {"server_pid", serverProcessId_},
                            {"elapsed_ms", GetTickCount64() - started}});
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
