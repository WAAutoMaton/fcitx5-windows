#include "windowsfrontend.h"
#include "../win32/ipc/transport.h"
#include <algorithm>
#include <chrono>
#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputmethodentry.h>
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

ipc::KeyReply WindowsInputContext::snapshot(bool consumed, bool enabled) {
    ipc::KeyReply reply;
    reply.consumed = consumed;
    reply.enabled = enabled;
    reply.commit = takeCommit();
    reply.preedit = preedit();
    reply.preeditCursor =
        std::min(preeditCursor(), static_cast<uint32_t>(reply.preedit.size()));
    reply.revision = ++revision_;
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

WindowsPipeServer::WindowsPipeServer(Instance &instance,
                                     EventDispatcher &dispatcher)
    : instance_(instance), dispatcher_(dispatcher) {}

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
    thread_ = std::thread([this]() { run(); });
}

void WindowsPipeServer::stop() {
    stopping_ = true;
    if (stopEvent_) {
        SetEvent(stopEvent_);
    }
    if (thread_.joinable()) {
        thread_.join();
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

void WindowsPipeServer::run() {
    const auto name = ipc::pipeName();
    const auto descriptor = L"D:P(A;;GA;;;" + ipc::userSid() + L")";
    PSECURITY_DESCRIPTOR security = nullptr;
    if (name.empty() ||
        !ConvertStringSecurityDescriptorToSecurityDescriptorW(
            descriptor.c_str(), SDDL_REVISION_1, &security, nullptr)) {
        return;
    }
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security,
                                   FALSE};
    uint64_t clientId = 0;
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
            std::erase_if(clients_, [](ClientThread &client) {
                if (!client.finished->load()) {
                    return false;
                }
                client.thread.join();
                return true;
            });
            if (clients_.size() >= 64) {
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
                continue;
            }
            auto finished = std::make_shared<std::atomic_bool>(false);
            std::thread worker([this, pipe, id = ++clientId, finished] {
                runClient(pipe, id);
                finished->store(true);
            });
            clients_.push_back({std::move(worker), std::move(finished)});
        } else {
            CloseHandle(pipe);
        }
    }
    LocalFree(security);
}

void WindowsPipeServer::runClient(HANDLE pipe, uint64_t clientId) {
    while (!stopping_) {
        ipc::Frame request;
        if (!readFrame(pipe, request)) {
            break;
        }
        auto pending = std::make_shared<PendingReply>();
        dispatcher_.schedule(
            [this, request = std::move(request), pending, clientId]() {
                process(request, pending, clientId);
            });
        std::unique_lock<std::mutex> lock(pending->mutex);
        while (!pending->ready && !stopping_) {
            pending->condition.wait_for(lock, std::chrono::milliseconds(100));
        }
        if (!pending->ready) {
            break;
        }
        auto response = std::move(pending->frame);
        lock.unlock();
        if (!writeFrame(pipe, response)) {
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
                                uint64_t clientId) {
    using ipc::MessageType;
    auto response = makeResponse(request, MessageType::Ack);
    ipc::Reader reader(request.payload.data(), request.payload.size());
    ipc::Writer writer;
    if ((request.type == MessageType::Hello &&
         (request.contextId || !request.payload.empty())) ||
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
        (!owners_.contains(request.contextId) ||
         owners_.at(request.contextId) != clientId)) {
        response.type = MessageType::Error;
        complete(pending, std::move(response));
        return;
    }

    switch (request.type) {
    case MessageType::Hello:
        response.type = MessageType::HelloReply;
        writer.u16(ipc::kVersion);
        response.payload = writer.take();
        break;
    case MessageType::CreateContext: {
        std::string program;
        uint64_t capabilities = 0;
        if (!reader.string(program) || !reader.u64(capabilities) ||
            reader.remaining()) {
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
            instance_.setCurrentInputMethod(iter->second.get(), "pinyin", true);
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
        const auto entry = instance_.inputMethodEntry(&context);
        response.payload = ipc::encodeKeyReply(context.snapshot(
            consumed, entry && entry->uniqueName() == "pinyin"));
        break;
    }
    case MessageType::Reset:
    case MessageType::PollState: {
        auto &context = *contexts_.at(request.contextId);
        if (request.type == MessageType::Reset) {
            context.reset();
            context.takeCommit();
        }
        const auto entry = instance_.inputMethodEntry(&context);
        response.type = MessageType::KeyReply;
        response.payload = ipc::encodeKeyReply(
            context.snapshot(false, entry && entry->uniqueName() == "pinyin"));
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
