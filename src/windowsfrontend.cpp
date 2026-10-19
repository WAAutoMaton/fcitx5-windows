#include "windowsfrontend.h"
#include "../win32/i18n/settings.h"
#include "../win32/ipc/diagnostic_log.h"
#include "../win32/ipc/pipeidentity.h"
#include "../win32/ipc/service.h"
#include "../win32/ipc/shortcuts.h"
#include "../win32/ipc/transport.h"
#include "windowslogging.h"
#include "windowsplugins.h"
#include <algorithm>
#include <chrono>
#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx/candidatelist.h>
#include <fcitx/globalconfig.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <utility>

namespace fcitx::win32 {
namespace {

constexpr uint32_t kModifierShift = 1U << 0;
constexpr uint32_t kModifierControl = 1U << 1;
constexpr uint32_t kModifierAlt = 1U << 2;
constexpr uint32_t kModifierSuper = 1U << 3;
constexpr uint32_t kModifierCaps = 1U << 4;
constexpr uint32_t kModifierRepeat = 1U << 5;

fcitx::Key keyFromWindows(uint32_t virtualKey, uint32_t scanCode,
                          uint32_t modifiers, uint32_t unicode) {
    fcitx::KeyStates states;
    if (modifiers & kModifierShift) {
        states |= fcitx::KeyState::Shift;
    }
    if (modifiers & kModifierControl) {
        states |= fcitx::KeyState::Ctrl;
    }
    if (modifiers & kModifierAlt) {
        states |= fcitx::KeyState::Alt;
    }
    if (modifiers & kModifierSuper) {
        states |= fcitx::KeyState::Super;
    }
    if (modifiers & kModifierCaps) {
        states |= fcitx::KeyState::CapsLock;
    }
    if (modifiers & kModifierRepeat) {
        states |= fcitx::KeyState::Repeat;
    }

    fcitx::KeySym symbol = FcitxKey_None;
    if (unicode) {
        symbol = Key::keySymFromUnicode(unicode);
    } else if (virtualKey >= 'A' && virtualKey <= 'Z') {
        const bool upper = ((modifiers & kModifierShift) != 0) !=
                           ((modifiers & kModifierCaps) != 0);
        symbol = static_cast<fcitx::KeySym>((upper ? 'A' : 'a') +
                                            (virtualKey - 'A'));
    } else if (virtualKey >= '0' && virtualKey <= '9') {
        symbol = static_cast<fcitx::KeySym>(virtualKey);
    } else {
        switch (virtualKey) {
        case VK_SPACE:
            symbol = FcitxKey_space;
            break;
        case VK_RETURN:
            symbol = FcitxKey_Return;
            break;
        case VK_BACK:
            symbol = FcitxKey_BackSpace;
            break;
        case VK_TAB:
            symbol = FcitxKey_Tab;
            break;
        case VK_ESCAPE:
            symbol = FcitxKey_Escape;
            break;
        case VK_LEFT:
            symbol = FcitxKey_Left;
            break;
        case VK_RIGHT:
            symbol = FcitxKey_Right;
            break;
        case VK_UP:
            symbol = FcitxKey_Up;
            break;
        case VK_DOWN:
            symbol = FcitxKey_Down;
            break;
        case VK_DELETE:
            symbol = FcitxKey_Delete;
            break;
        case VK_HOME:
            symbol = FcitxKey_Home;
            break;
        case VK_END:
            symbol = FcitxKey_End;
            break;
        case VK_PRIOR:
            symbol = FcitxKey_Page_Up;
            break;
        case VK_NEXT:
            symbol = FcitxKey_Page_Down;
            break;
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            symbol = virtualKey == VK_RSHIFT || scanCode == 0x36
                         ? FcitxKey_Shift_R
                         : FcitxKey_Shift_L;
            break;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            symbol = virtualKey == VK_RCONTROL || (scanCode & 0x100)
                         ? FcitxKey_Control_R
                         : FcitxKey_Control_L;
            break;
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            symbol = virtualKey == VK_RMENU || (scanCode & 0x100)
                         ? FcitxKey_Alt_R
                         : FcitxKey_Alt_L;
            break;
        case VK_LWIN:
        case VK_RWIN:
            symbol =
                virtualKey == VK_RWIN ? FcitxKey_Super_R : FcitxKey_Super_L;
            break;
        default:
            break;
        }
    }
    return fcitx::Key(symbol, states, static_cast<int>(scanCode));
}

#ifdef FCITX5_WINDOWS_ASCII_FALLBACK
std::string asciiTextForKey(uint32_t virtualKey, uint32_t modifiers) {
    if (modifiers & (kModifierControl | kModifierAlt | kModifierSuper)) {
        return {};
    }
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        const bool upper = ((modifiers & kModifierShift) != 0) !=
                           ((modifiers & kModifierCaps) != 0);
        return std::string(
            1, static_cast<char>((upper ? 'A' : 'a') + (virtualKey - 'A')));
    }
    if (virtualKey >= '0' && virtualKey <= '9') {
        return std::string(1, static_cast<char>(virtualKey));
    }
    if (virtualKey == VK_SPACE &&
        (modifiers & (kModifierControl | kModifierAlt | kModifierSuper)) == 0) {
        return " ";
    }
    return {};
}
#endif
ipc::Frame makeResponse(const ipc::Frame &request, ipc::MessageType type) {
    ipc::Frame response;
    response.type = type;
    response.requestId = request.requestId;
    response.contextId = request.contextId;
    return response;
}

} // namespace

WindowsInputContext::WindowsInputContext(InputContextManager &manager,
                                         uint64_t id, std::string program)
    : InputContext(manager, program), id_(id) {}

WindowsInputContext::~WindowsInputContext() { destroy(); }

void WindowsInputContext::setCapabilities(uint64_t value) {
    setCapabilityFlags(CapabilityFlags(value));
}

std::string WindowsInputContext::takeCommit() {
    auto result = std::move(pendingCommit_);
    pendingCommit_.clear();
    return result;
}

std::string WindowsInputContext::preedit() const {
    const auto &clientPreedit = inputPanel().clientPreedit();
    if (!clientPreedit.empty()) {
        return clientPreedit.toString();
    }
    return inputPanel().preedit().toString();
}

uint32_t WindowsInputContext::preeditCursor() const {
    const auto &clientPreedit = inputPanel().clientPreedit();
    if (!clientPreedit.empty()) {
        return static_cast<uint32_t>(std::max(clientPreedit.cursor(), 0));
    }
    return static_cast<uint32_t>(std::max(inputPanel().preedit().cursor(), 0));
}

void WindowsInputContext::commitStringImpl(const std::string &text) {
    pendingCommit_ += text;
}

void WindowsInputContext::deleteSurroundingTextImpl(int, unsigned int) {}

void WindowsInputContext::forwardKeyImpl(const ForwardKeyEvent &) {}

void WindowsInputContext::updatePreeditImpl() {}

void WindowsInputContext::resetPreedit() {
    auto committed = takeCommit();
    reset();
    pendingCommit_ = std::move(committed);
}

ipc::KeyReply WindowsInputContext::candidateSnapshot() const {
    ipc::KeyReply reply;
    reply.preedit = preedit();
    reply.preeditCursor =
        std::min(preeditCursor(), static_cast<uint32_t>(reply.preedit.size()));
    if (const auto list = inputPanel().candidateList()) {
        const auto size =
            std::min(list->size(), static_cast<int>(ipc::kMaxCandidates));
        for (int index = 0; index < size; ++index) {
            const auto &candidate = list->candidate(index);
            reply.candidates.push_back({candidate.text().toString(),
                                        candidate.hasCustomLabel()
                                            ? candidate.customLabel().toString()
                                            : list->label(index).toString(),
                                        candidate.comment().toString()});
        }
        if (list->cursorIndex() >= 0 && list->cursorIndex() < size) {
            reply.selected = static_cast<uint32_t>(list->cursorIndex());
        }
        if (const auto pages = list->toPageable()) {
            reply.hasPrev = pages->hasPrev();
            reply.hasNext = pages->hasNext();
        }
    }
    return reply;
}

void WindowsInputContext::updateCandidateIdentity(ipc::KeyReply &reply) {
    const auto source = inputPanel().candidateList();
    const auto pageable = source ? source->toPageable() : nullptr;
    const auto page = pageable ? pageable->currentPage() : -1;
    if (source != candidateSource_ || page != candidatePage_ ||
        reply.candidates != candidateIdentity_.candidates ||
        reply.preedit != candidateIdentity_.preedit ||
        reply.inputMethod != candidateIdentity_.inputMethod ||
        reply.settingsRevision != candidateIdentity_.settingsRevision) {
        ++candidateGeneration_;
        candidateSource_ = source;
        candidatePage_ = page;
        candidateIdentity_ = reply;
        candidateIdentity_.commit.clear();
    }
    reply.candidateGeneration =
        reply.candidates.empty() ? 0 : candidateGeneration_;
}

ipc::KeyReply WindowsInputContext::snapshot(bool consumed, bool enabled) {
    auto reply = candidateSnapshot();
    reply.consumed = consumed;
    reply.enabled = enabled;
    reply.commit = takeCommit();
    reply.revision = ++revision_;
    return reply;
}

WindowsPipeServer::WindowsPipeServer(Instance &instance,
                                     EventDispatcher &dispatcher,
                                     std::filesystem::path settingsFile,
                                     std::filesystem::path uiSettingsFile,
                                     ipc::UiLanguage activeLanguage,
                                     bool detailedLogging)
    : instance_(instance), dispatcher_(dispatcher),
      settings_(instance, std::move(settingsFile)), dictionaries_(instance),
      uiSettingsFile_(uiSettingsFile.empty() ? i18n::settingsPath()
                                             : std::move(uiSettingsFile)),
      activeLanguage_(i18n::resolveLanguage(activeLanguage)),
      detailedLogging_(detailedLogging), settingsEpoch_(settings_.revision()) {
    ipc::GlobalSettings global;
    if (i18n::readSettings(uiSettingsFile_, global))
        shortcuts_ = global.shortcuts;
    // Windows owns method selection; upstream default hotkeys must not remain
    // active after the user replaces or clears the Windows shortcuts.
    RawConfig hotkeys;
    for (const auto *name :
         {"TriggerKeys", "AltTriggerKeys", "ActivateKeys", "DeactivateKeys",
          "EnumerateForwardKeys", "EnumerateBackwardKeys",
          "EnumerateGroupForwardKeys", "EnumerateGroupBackwardKeys"})
        hotkeys.get(std::string("Hotkey/") + name, true);
    instance_.globalConfig().load(hotkeys, true);
}

ipc::KeyReply WindowsPipeServer::snapshot(WindowsInputContext &context,
                                          bool consumed) {
    auto reply = context.snapshot(consumed, settings_.chineseMode(&context));
    settings_.decorate(&context, reply);
    reply.settingsRevision = settingsEpoch_;
    reply.detailedLogging = detailedLogging_;
    context.updateCandidateIdentity(reply);
    if (diagnostic::coreLogger().enabled())
        diagnostic::coreLogger().event(
            diagnostic::Level::Debug, "core_snapshot",
            {{"input_method", reply.inputMethod},
             {"settings_revision", reply.settingsRevision},
             {"candidate_generation", reply.candidateGeneration},
             {"candidate_count", reply.candidates.size()},
             {"selected", reply.selected},
             {"preedit_bytes", reply.preedit.size()},
             {"commit_bytes", reply.commit.size()},
             {"consumed", consumed ? "true" : "false"}});
    return reply;
}

WindowsPipeServer::~WindowsPipeServer() { stop(); }

void WindowsPipeServer::start() {
    if (thread_.joinable()) {
        return;
    }
    stopping_ = false;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) {
        return;
    }
    thread_ = std::thread([this]() { run(false); });
    inputThread_ = std::thread([this]() { run(true); });
}

void WindowsPipeServer::stop() {
    stopping_ = true;
    if (stopEvent_) {
        SetEvent(stopEvent_);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (inputThread_.joinable()) {
        inputThread_.join();
    }
    for (auto &client : clients_) {
        client.thread.join();
    }
    clients_.clear();
    contexts_.clear();
    owners_.clear();
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
}

void WindowsPipeServer::run(bool inputOnly) {
    const auto name = inputOnly ? ipc::inputPipeName() : ipc::pipeName();
    const auto descriptor = inputOnly ? ipc::inputPipeSecurity(ipc::userSid())
                                      : L"D:P(A;;GA;;;" + ipc::userSid() + L")";
    PSECURITY_DESCRIPTOR security = nullptr;
    if (name.empty() ||
        !ConvertStringSecurityDescriptorToSecurityDescriptorW(
            descriptor.c_str(), SDDL_REVISION_1, &security, nullptr)) {
        return;
    }
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security,
                                   FALSE};
    bool first = true;
    while (!stopping_) {
        HANDLE pipe = CreateNamedPipeW(
            name.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
                PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES, ipc::kHeaderSize + ipc::kMaxPayloadSize,
            ipc::kHeaderSize + ipc::kMaxPayloadSize, 0, &attributes);
        if (pipe == INVALID_HANDLE_VALUE) {
            FCITX_ERROR() << "Unable to create Fcitx5 Windows named pipe";
            break;
        }
        first = false;
        OVERLAPPED connection{};
        connection.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!connection.hEvent) {
            CloseHandle(pipe);
            break;
        }
        BOOL connected = ConnectNamedPipe(pipe, &connection);
        if (!connected) {
            const auto error = GetLastError();
            if (error == ERROR_PIPE_CONNECTED) {
                connected = TRUE;
            } else if (error == ERROR_IO_PENDING) {
                HANDLE events[] = {connection.hEvent, stopEvent_};
                if (WaitForMultipleObjects(2, events, FALSE, INFINITE) ==
                    WAIT_OBJECT_0) {
                    DWORD transferred = 0;
                    connected = GetOverlappedResult(pipe, &connection,
                                                    &transferred, FALSE);
                } else {
                    CancelIoEx(pipe, &connection);
                    DWORD transferred = 0;
                    GetOverlappedResult(pipe, &connection, &transferred, TRUE);
                }
            }
        }
        CloseHandle(connection.hEvent);
        if (connected && !stopping_) {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            std::erase_if(clients_, [](ClientThread &client) {
                if (!client.finished->load()) {
                    return false;
                }
                client.thread.join();
                return true;
            });
            const auto inputClients = std::count_if(
                clients_.begin(), clients_.end(),
                [](const ClientThread &client) { return client.inputOnly; });
            if (clients_.size() >= 64 || (inputOnly && inputClients >= 16)) {
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
                continue;
            }
            auto finished = std::make_shared<std::atomic_bool>(false);
            std::thread worker(
                [this, pipe, id = ++nextClientId_, finished, inputOnly] {
                    runClient(pipe, id, inputOnly);
                    finished->store(true);
                });
            clients_.push_back(
                {std::move(worker), std::move(finished), inputOnly});
        } else {
            CloseHandle(pipe);
        }
    }
    LocalFree(security);
}

void WindowsPipeServer::runClient(HANDLE pipe, uint64_t clientId,
                                  bool inputOnly) {
    const auto host = ipc::clientEnvironment();
    ipc::ClientEnvironment caller;
    ipc::Frame hello;
    bool restricted = inputOnly;
    uint64_t previousRequest = 0;
    bool authenticated = false;
    if (!ipc::readFrame(pipe, hello, 2000, stopEvent_, 4096) ||
        hello.type != ipc::MessageType::Hello || hello.contextId ||
        !hello.payload.empty() || !hello.requestId ||
        !ipc::authorizedPipeClient(pipe, host, caller, inputOnly)) {
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        return;
    }
    restricted = restricted || caller.appContainer;
    previousRequest = hello.requestId;
    ipc::Writer version;
    version.u16(ipc::kVersion);
    auto greeting = makeResponse(hello, ipc::MessageType::HelloReply);
    greeting.payload = version.take();
    bool ready = writeFrame(pipe, greeting);
    while (ready && !stopping_) {
        ipc::Frame request;
        if (!ipc::readFrame(
                pipe, request,
                restricted ? (authenticated ? 60000 : 2000) : INFINITE,
                stopEvent_, restricted ? 65536 : ipc::kMaxPayloadSize) ||
            request.requestId <= previousRequest) {
            break;
        }
        previousRequest = request.requestId;
        if (request.type == ipc::MessageType::AuthenticateServer) {
            if (authenticated || request.contextId || !request.payload.empty())
                break;
            ipc::ServerIdentityHandle identity(pipe);
            if (!identity.value())
                break;
            auto reply =
                makeResponse(request, ipc::MessageType::ServerIdentityReply);
            ipc::Writer writer;
            writer.u64(identity.value());
            reply.payload = writer.take();
            if (!writeFrame(pipe, reply))
                break;
            identity.delivered();
            authenticated = true;
            continue;
        }
        if (restricted &&
            (!authenticated || !ipc::restrictedRequestAllowed(request.type))) {
            writeFrame(pipe, makeResponse(request, ipc::MessageType::Error));
            break;
        }
        auto pending = std::make_shared<PendingReply>();
        dispatcher_.schedule([this, request = std::move(request), pending,
                              clientId, restricted]() {
            process(request, pending, clientId, restricted);
        });
        std::unique_lock<std::mutex> lock(pending->mutex);
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!pending->ready && !stopping_ &&
               (!restricted || std::chrono::steady_clock::now() < deadline)) {
            pending->condition.wait_for(lock, std::chrono::milliseconds(100));
        }
        if (!pending->ready) {
            break;
        }
        auto response = std::move(pending->frame);
        auto afterReply = std::move(pending->afterReply);
        lock.unlock();
        if (restricted && response.type == ipc::MessageType::KeyReply) {
            ipc::KeyReply snapshot;
            if (!ipc::decodeKeyReply(response.payload, snapshot))
                break;
            snapshot.inputMethodIcon.clear();
            response.payload = ipc::encodeKeyReply(snapshot);
        }
        const auto written = writeFrame(pipe, response);
        if (afterReply) {
            dispatcher_.schedule(
                [action = std::move(afterReply), written] { action(written); });
        }
        if (!written) {
            break;
        }
    }
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    dispatcher_.schedule([this, clientId] {
        for (auto iter = owners_.begin(); iter != owners_.end();) {
            if (iter->second == clientId) {
                contexts_.erase(iter->first);
                iter = owners_.erase(iter);
            } else {
                ++iter;
            }
        }
    });
}

void WindowsPipeServer::process(const ipc::Frame &request,
                                const std::shared_ptr<PendingReply> &pending,
                                uint64_t clientId, bool restricted) {
    using ipc::MessageType;
    if (diagnostic::coreLogger().enabled())
        diagnostic::coreLogger().event(
            diagnostic::Level::Debug, "core_request",
            {{"type", static_cast<unsigned>(request.type)},
             {"request", request.requestId},
             {"context", request.contextId},
             {"client", clientId},
             {"payload_bytes", request.payload.size()},
             {"dispatch_ms", GetTickCount64() - pending->submittedAt}});
    auto response = makeResponse(request, MessageType::Ack);
    ipc::Reader reader(request.payload.data(), request.payload.size());
    ipc::Writer writer;
    if ((ipc::hostCommand(request.type) &&
         !ipc::validHostCommandRequest(request)) ||
        (request.type == MessageType::Hello &&
         (request.contextId || !request.payload.empty())) ||
        (ipc::globalSettingsRequest(request.type) &&
         (request.contextId ||
          (request.type != MessageType::SetSettings &&
           request.type != MessageType::SetGlobalSettings &&
           request.type != MessageType::SetInputMethods &&
           request.type != MessageType::GetMethodConfig &&
           request.type != MessageType::SetMethodConfig &&
           !request.payload.empty()))) ||
        (request.type == MessageType::CreateContext && request.contextId) ||
        ((request.type == MessageType::FocusIn ||
          request.type == MessageType::FocusOut ||
          request.type == MessageType::DestroyContext ||
          request.type == MessageType::Reset ||
          request.type == MessageType::PollState) &&
         !request.payload.empty())) {
        response.type = MessageType::Error;
        complete(pending, std::move(response));
        return;
    }
    if (request.type != MessageType::Hello &&
        request.type != MessageType::CreateContext &&
        !ipc::globalSettingsRequest(request.type) &&
        (!owners_.contains(request.contextId) ||
         owners_.at(request.contextId) != clientId)) {
        diagnostic::coreLogger().event(diagnostic::Level::Warning,
                                       "context_owner_rejected");
        response.type = MessageType::Error;
        complete(pending, std::move(response));
        return;
    }

    switch (request.type) {
    case MessageType::OpenUserDataFolder:
    case MessageType::RestartServices:
    case MessageType::StopServices: {
        const auto error = hostCommands_.reserve(request.type);
        writer.u32(error);
        response.type = MessageType::ServiceReply;
        response.payload = writer.take();
        if (error == ERROR_SUCCESS) {
            pending->afterReply = [this, type = request.type](bool delivered) {
                try {
                    hostCommands_.afterReply(
                        type, delivered, [](const wchar_t *arguments) {
                            const auto core =
                                StandardPaths::fcitxPath("bindir") /
                                "Fcitx5.exe";
                            ipc::launchServiceExecutable(core, true, arguments);
                        });
                } catch (const std::exception &error) {
                    FCITX_ERROR()
                        << "Host service command failed: " << error.what();
                }
            };
        }
        break;
    }
    case MessageType::GetGlobalSettings:
    case MessageType::SetGlobalSettings: {
        ipc::GlobalSettingsReply reply;
        reply.coreActiveLanguage = activeLanguage_;
        if (!i18n::readSettings(uiSettingsFile_, reply.settings)) {
            reply.error = ipc::SettingsError::SaveFailed;
        } else if (request.type == MessageType::SetGlobalSettings) {
            ipc::GlobalSettings requested;
            reply.error = ipc::decodeGlobalSettings(request.payload, requested)
                              ? i18n::saveSettings(uiSettingsFile_, requested,
                                                   reply.settings)
                              : ipc::SettingsError::Invalid;
        }
        if (reply.error == ipc::SettingsError::None) {
            shortcuts_ = reply.settings.shortcuts;
            detailedLogging_ = reply.settings.detailedLogging;
            applyDiagnosticLogging(detailedLogging_);
        }
        if (diagnostic::coreLogger().enabled())
            diagnostic::coreLogger().event(
                reply.error == ipc::SettingsError::None
                    ? diagnostic::Level::Info
                    : diagnostic::Level::Warning,
                "global_settings",
                {{"revision", reply.settings.revision},
                 {"error", static_cast<unsigned>(reply.error)}});
        reply.resolvedLanguage = i18n::resolveLanguage(reply.settings.language);
        response.type = MessageType::GlobalSettingsReply;
        response.payload = ipc::encodeGlobalSettingsReply(reply);
        break;
    }
    case MessageType::GetInputMethods:
    case MessageType::SetInputMethods: {
        auto error = ipc::SettingsError::None;
        if (request.type == MessageType::SetInputMethods) {
            ipc::InputMethodSelection selection;
            const bool valid = ipc::decodeSelection(request.payload, selection);
            error = valid ? settings_.applySelection(selection)
                          : ipc::SettingsError::Invalid;
            if (valid && settings_.resetRequired()) {
                ++settingsEpoch_;
                for (auto &[id, context] : contexts_) {
                    context->reset();
                    context->takeCommit();
                    settings_.reconcile(context.get());
                    context->takeCommit();
                }
            }
        }
        response.type = MessageType::InputMethodsReply;
        response.payload =
            ipc::encodeInputMethods(settings_.inputMethods(error));
        break;
    }
    case MessageType::GetMethodConfig:
    case MessageType::SetMethodConfig: {
        ipc::MethodConfigRequest config;
        if (!ipc::decodeMethodConfigRequest(request.payload, config) ||
            (request.type == MessageType::GetMethodConfig &&
             (config.revision || !config.values.empty()))) {
            response.type = MessageType::Error;
            break;
        }
        auto error = ipc::SettingsError::None;
        if (request.type == MessageType::SetMethodConfig) {
            error = settings_.applyMethodConfig(config);
            if (settings_.resetRequired()) {
                ++settingsEpoch_;
                for (auto &[id, context] : contexts_) {
                    context->reset();
                    context->takeCommit();
                }
            }
        }
        response.type = MessageType::MethodConfigReply;
        response.payload =
            ipc::encodeMethodConfig(settings_.methodConfig(config.id, error));
        break;
    }
    case MessageType::GetPlugins:
        response.type = MessageType::PluginsReply;
        response.payload =
            ipc::encodePluginsReply(pluginSnapshot(instance_.addonManager()));
        break;
    case MessageType::GetDictionaries: {
        response.type = MessageType::DictionariesReply;
        const auto dictionaries = dictionaries_.snapshot();
        if (diagnostic::coreLogger().enabled())
            diagnostic::coreLogger().event(
                diagnostic::Level::Debug, "dictionary_status",
                {{"available", dictionaries.available ? "true" : "false"},
                 {"count", dictionaries.dictionaries.size()},
                 {"error", dictionaries.error.empty() ? "false" : "true"}});
        response.payload = ipc::encodeDictionariesReply(dictionaries);
        break;
    }
    case MessageType::OpenSettings:
    case MessageType::GetSettings:
    case MessageType::SetSettings: {
        auto error = ipc::SettingsError::None;
        if (request.type == MessageType::OpenSettings) {
            if (restricted && GetTickCount64() < nextRestrictedSettings_) {
                error = ipc::SettingsError::Busy;
            } else {
                if (restricted)
                    nextRestrictedSettings_ = GetTickCount64() + 2000;
                error = settings_.openWindow();
            }
        } else if (request.type == MessageType::SetSettings) {
            ipc::PinyinSettings requested;
            const bool valid = ipc::decodeSettings(request.payload, requested);
            error = valid ? settings_.apply(requested)
                          : ipc::SettingsError::Invalid;
            if (valid && settings_.resetRequired()) {
                ++settingsEpoch_;
                std::vector<WindowsInputContext *> enabled;
                for (auto &[id, context] : contexts_) {
                    const auto *entry =
                        instance_.inputMethodEntry(context.get());
                    if (entry && entry->addon() == "pinyin") {
                        enabled.push_back(context.get());
                    }
                    context->resetPreedit();
                }
                instance_.inputMethodManager().setDefaultInputMethod(
                    settings_.chineseEntry());
                for (auto *context : enabled) {
                    instance_.setCurrentInputMethod(
                        context, settings_.chineseEntry(), true);
                }
            }
        }
        response.type = MessageType::SettingsReply;
        response.payload = ipc::encodeSettingsReply(settings_.snapshot(error));
        break;
    }
    case MessageType::Hello:
        response.type = MessageType::HelloReply;
        writer.u16(ipc::kVersion);
        response.payload = writer.take();
        break;
    case MessageType::CreateContext: {
        std::string program;
        uint64_t capabilities = 0;
        if (!reader.string(program) || !reader.u64(capabilities) ||
            reader.remaining() ||
            (restricted && std::count_if(owners_.begin(), owners_.end(),
                                         [clientId](const auto &owner) {
                                             return owner.second == clientId;
                                         }) >= 4)) {
            response.type = MessageType::Error;
            writer.string("invalid create_context request");
            response.payload = writer.take();
            break;
        }
        const auto contextId = nextContextId_++;
        auto context = std::make_unique<WindowsInputContext>(
            instance_.inputContextManager(), contextId, std::move(program));
        context->setCapabilities(capabilities & ((1ULL << 1) | (1ULL << 38)));
        contexts_.emplace(contextId, std::move(context));
        owners_.emplace(contextId, clientId);
        response.type = MessageType::CreateContextReply;
        response.contextId = contextId;
        writer.u64(contextId);
        response.payload = writer.take();
        break;
    }
    case MessageType::FocusIn: {
        auto iter = contexts_.find(request.contextId);
        if (iter == contexts_.end()) {
            response.type = MessageType::Error;
            writer.string("unknown context");
        } else {
            iter->second->focusIn();
            instance_.setCurrentInputMethod(iter->second.get(),
                                            settings_.chineseEntry(), true);
        }
        response.payload = writer.take();
        break;
    }
    case MessageType::FocusOut: {
        auto iter = contexts_.find(request.contextId);
        if (iter == contexts_.end()) {
            response.type = MessageType::Error;
            writer.string("unknown context");
        } else {
            iter->second->reset();
            iter->second->takeCommit();
            iter->second->shortcutKeys.reset();
            iter->second->focusOut();
        }
        response.payload = writer.take();
        break;
    }
    case MessageType::DestroyContext: {
        auto iter = contexts_.find(request.contextId);
        if (iter == contexts_.end()) {
            response.type = MessageType::Error;
            writer.string("unknown context");
        } else {
            if (iter->second->hasFocus()) {
                iter->second->shortcutKeys.reset();
                iter->second->focusOut();
            }
            contexts_.erase(iter);
            owners_.erase(request.contextId);
        }
        response.payload = writer.take();
        break;
    }
    case MessageType::KeyDown:
    case MessageType::KeyUp: {
        uint32_t virtualKey = 0;
        uint32_t scanCode = 0;
        uint32_t modifiers = 0;
        uint32_t time = 0;
        uint32_t unicode = 0;
        if (!reader.u32(virtualKey) || !reader.u32(scanCode) ||
            !reader.u32(modifiers) || !reader.u32(time) ||
            !reader.u32(unicode) || unicode > 0x10ffff ||
            (unicode >= 0xd800 && unicode <= 0xdfff) || reader.remaining()) {
            response.type = MessageType::Error;
            writer.string("invalid key request");
            response.payload = writer.take();
            break;
        }
        auto iter = contexts_.find(request.contextId);
        if (iter == contexts_.end()) {
            response.type = MessageType::Error;
            writer.string("unknown context");
            response.payload = writer.take();
            break;
        }
        auto &context = *iter->second;
        if (!context.hasFocus()) {
            response.type = MessageType::Error;
            break;
        }
        const bool release = request.type == MessageType::KeyUp;
        const bool held = virtualKey < context.shortcutKeys.size() &&
                          context.shortcutKeys.test(virtualKey);
        const auto action =
            ipc::shortcutAction(shortcuts_, virtualKey, scanCode, modifiers);
        if (held || action >= 0) {
            bool consumed = held;
            if (!release && action >= 0 && !(modifiers & kModifierRepeat)) {
                context.takeCommit();
                consumed = action == 0
                               ? settings_.cycle(&context)
                               : settings_.selectIndex(&context, action - 1);
                context.takeCommit();
                if (consumed && virtualKey < context.shortcutKeys.size())
                    context.shortcutKeys.set(virtualKey);
            }
            if (release && virtualKey < context.shortcutKeys.size())
                context.shortcutKeys.reset(virtualKey);
            response.type = MessageType::KeyReply;
            response.payload = ipc::encodeKeyReply(snapshot(context, consumed));
            break;
        }
        auto key = keyFromWindows(virtualKey, scanCode, modifiers, unicode);
        KeyEvent event(&context, key, request.type == MessageType::KeyUp,
                       static_cast<int>(time));
        bool consumed = context.keyEvent(event);
#ifdef FCITX5_WINDOWS_ASCII_FALLBACK
        if (!consumed && request.type == MessageType::KeyDown &&
            instance_.inputMethodEntry(&context) == nullptr) {
            auto text = asciiTextForKey(virtualKey, modifiers);
            if (!text.empty()) {
                context.commitString(text);
                consumed = true;
            }
        }
#endif
        response.type = MessageType::KeyReply;
        response.payload = ipc::encodeKeyReply(snapshot(context, consumed));
        break;
    }
    case MessageType::CandidateAction: {
        uint64_t generation = 0;
        uint8_t action = 0;
        uint32_t index = 0;
        auto &context = *contexts_.at(request.contextId);
        auto identity = context.candidateSnapshot();
        settings_.decorate(&context, identity);
        identity.settingsRevision = settingsEpoch_;
        context.updateCandidateIdentity(identity);
        const auto list = context.inputPanel().candidateList();
        const auto reject = [&](const char *reason) {
            response.type = MessageType::Error;
            ipc::Writer error;
            error.string(reason);
            response.payload = error.take();
        };
        if (!reader.u64(generation) || !reader.u8(action) ||
            action > static_cast<uint8_t>(ipc::CandidateAction::Abort) ||
            !reader.u32(index) || reader.remaining() || !context.hasFocus() ||
            !generation || generation != identity.candidateGeneration ||
            !list) {
            reject("stale or invalid candidate action");
            break;
        }
        if (action == static_cast<uint8_t>(ipc::CandidateAction::Abort)) {
            context.reset();
            context.takeCommit();
        } else if (index >= identity.candidates.size() ||
                   list->candidate(index).isPlaceHolder()) {
            reject("invalid candidate index");
            break;
        } else if (action ==
                   static_cast<uint8_t>(ipc::CandidateAction::Select)) {
            const auto cursor = list->toCursorModifiable();
            if (!cursor) {
                reject("candidate cursor is not modifiable");
                break;
            }
            cursor->setCursorIndex(static_cast<int>(index));
            context.updateUserInterface(UserInterfaceComponent::InputPanel);
        } else {
            list->candidate(index).select(&context);
        }
        response.type = MessageType::KeyReply;
        response.payload = ipc::encodeKeyReply(snapshot(context, false));
        break;
    }
    case MessageType::SetMode: {
        uint8_t enabled = 0;
        if (!reader.u8(enabled) || enabled > 1 || reader.remaining()) {
            response.type = MessageType::Error;
            break;
        }
        auto &context = *contexts_.at(request.contextId);
        if (!context.hasFocus()) {
            response.type = MessageType::Error;
            break;
        }
        context.takeCommit();
        settings_.setMode(&context, enabled != 0);
        context.takeCommit();
        response.type = MessageType::KeyReply;
        response.payload = ipc::encodeKeyReply(snapshot(context, false));
        break;
    }
    case MessageType::CycleInputMethod:
    case MessageType::SelectInputMethod: {
        auto &context = *contexts_.at(request.contextId);
        std::string id;
        if (!context.hasFocus() ||
            (request.type == MessageType::SelectInputMethod &&
             (!reader.string(id) || !ipc::validMethodId(id))) ||
            reader.remaining()) {
            response.type = MessageType::Error;
            break;
        }
        context.takeCommit();
        // A removed entry falls back to the current list on focus/reconnect.
        if (request.type == MessageType::CycleInputMethod) {
            settings_.cycle(&context);
        } else if (!settings_.select(&context, id)) {
            settings_.reconcile(&context);
        }
        context.takeCommit();
        response.type = MessageType::KeyReply;
        response.payload = ipc::encodeKeyReply(snapshot(context, false));
        break;
    }
    case MessageType::Reset:
    case MessageType::PollState: {
        auto &context = *contexts_.at(request.contextId);
        if (request.type == MessageType::Reset) {
            context.reset();
            context.takeCommit();
        }
        response.type = MessageType::KeyReply;
        response.payload = ipc::encodeKeyReply(snapshot(context, false));
        break;
    }
    default:
        response.type = MessageType::Error;
        writer.string("unsupported message");
        response.payload = writer.take();
        break;
    }
    complete(pending, std::move(response));
}

void WindowsPipeServer::complete(const std::shared_ptr<PendingReply> &pending,
                                 ipc::Frame response) {
    if (diagnostic::coreLogger().enabled())
        diagnostic::coreLogger().event(
            diagnostic::Level::Debug, "core_reply",
            {{"type", static_cast<unsigned>(response.type)},
             {"request", response.requestId},
             {"context", response.contextId},
             {"elapsed_ms", GetTickCount64() - pending->submittedAt}});
    {
        std::lock_guard<std::mutex> lock(pending->mutex);
        pending->frame = std::move(response);
        pending->ready = true;
    }
    pending->condition.notify_one();
}

bool WindowsPipeServer::readFrame(HANDLE pipe, ipc::Frame &frame) {
    return ipc::readFrame(pipe, frame, INFINITE, stopEvent_);
}

bool WindowsPipeServer::writeFrame(HANDLE pipe, const ipc::Frame &frame) {
    return ipc::writeFrame(pipe, frame, 1000, stopEvent_);
}

} // namespace fcitx::win32
