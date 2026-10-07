#include "windowsfrontend.h"
#include <algorithm>
#include <fcitx-utils/key.h>
#include <fcitx/inputpanel.h>
#include <fcitx-utils/log.h>
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
                          uint32_t modifiers) {
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
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        const bool upper = ((modifiers & kModifierShift) != 0) !=
                           ((modifiers & kModifierCaps) != 0);
        symbol = static_cast<fcitx::KeySym>(
            (upper ? 'A' : 'a') + (virtualKey - 'A'));
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
            symbol = FcitxKey_Shift_L;
            break;
        case VK_CONTROL:
            symbol = FcitxKey_Control_L;
            break;
        case VK_MENU:
            symbol = FcitxKey_Alt_L;
            break;
        case VK_LWIN:
        case VK_RWIN:
            symbol = FcitxKey_Super_L;
            break;
        default:
            break;
        }
    }
    return fcitx::Key(symbol, states, static_cast<int>(scanCode));
}

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

#ifdef FCITX5_WINDOWS_ASCII_FALLBACK
std::string asciiTextForKey(uint32_t virtualKey, uint32_t modifiers) {
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        const bool upper = ((modifiers & kModifierShift) != 0) !=
                           ((modifiers & kModifierCaps) != 0);
        return std::string(1, static_cast<char>(
                                  (upper ? 'A' : 'a') + (virtualKey - 'A')));
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
bool writePipeMessage(HANDLE pipe, const std::vector<uint8_t> &data) {
    DWORD bytesWritten = 0;
    return WriteFile(pipe, data.data(), static_cast<DWORD>(data.size()),
                     &bytesWritten, nullptr) &&
           bytesWritten == data.size();
}

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

WindowsPipeServer::WindowsPipeServer(Instance &instance,
                                     EventDispatcher &dispatcher)
    : instance_(instance), dispatcher_(dispatcher) {}

WindowsPipeServer::~WindowsPipeServer() { stop(); }

void WindowsPipeServer::start() {
    if (thread_.joinable()) {
        return;
    }
    stopping_ = false;
    thread_ = std::thread([this]() { run(); });
}

void WindowsPipeServer::stop() {
    stopping_ = true;
    closePipe();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void WindowsPipeServer::closePipe() {
    std::lock_guard<std::mutex> lock(pipeMutex_);
    if (pipe_ != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(pipe_);
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

void WindowsPipeServer::run() {
    while (!stopping_) {
        HANDLE pipe = CreateNamedPipeW(
            ipc::kPipeName, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1,
            ipc::kHeaderSize + ipc::kMaxPayloadSize,
            ipc::kHeaderSize + ipc::kMaxPayloadSize, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            FCITX_ERROR() << "Unable to create Fcitx5 Windows named pipe";
            return;
        }
        {
            std::lock_guard<std::mutex> lock(pipeMutex_);
            pipe_ = pipe;
        }
        BOOL connected = ConnectNamedPipe(pipe, nullptr)
                             ? TRUE
                             : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected && !stopping_) {
            runClient(pipe);
        }
        closePipe();
    }
}

void WindowsPipeServer::runClient(HANDLE pipe) {
    while (!stopping_) {
        ipc::Frame request;
        if (!readFrame(pipe, request)) {
            return;
        }
        auto pending = std::make_shared<PendingReply>();
        dispatcher_.schedule([this, request = std::move(request), pending]() {
            process(request, pending);
        });
        std::unique_lock<std::mutex> lock(pending->mutex);
        pending->condition.wait(lock, [&]() {
            return pending->ready || stopping_.load();
        });
        if (!pending->ready) {
            return;
        }
        auto response = std::move(pending->frame);
        lock.unlock();
        if (!writeFrame(pipe, response)) {
            return;
        }
    }
}

void WindowsPipeServer::process(
    const ipc::Frame &request, const std::shared_ptr<PendingReply> &pending) {
    using ipc::MessageType;
    auto response = makeResponse(request, MessageType::Ack);
    ipc::Reader reader(request.payload.data(), request.payload.size());
    ipc::Writer writer;

    switch (request.type) {
    case MessageType::Hello:
        response.type = MessageType::HelloReply;
        writer.u16(ipc::kVersion);
        response.payload = writer.take();
        break;
    case MessageType::CreateContext: {
        std::string program;
        uint64_t capabilities = 0;
        if (!reader.string(program) || !reader.u64(capabilities)) {
            response.type = MessageType::Error;
            writer.string("invalid create_context request");
            response.payload = writer.take();
            break;
        }
        const auto contextId = nextContextId_++;
        auto context = std::make_unique<WindowsInputContext>(
            instance_.inputContextManager(), contextId, std::move(program));
        context->setCapabilities(capabilities);
        contexts_.emplace(contextId, std::move(context));
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
        if (!reader.u32(virtualKey) || !reader.u32(scanCode) ||
            !reader.u32(modifiers) || !reader.u32(time)) {
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
        auto key = keyFromWindows(virtualKey, scanCode, modifiers);
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
        writer.u8(consumed ? 1 : 0);
        writer.string(context.takeCommit());
        writer.string(context.preedit());
        writer.u32(context.preeditCursor());
        response.payload = writer.take();
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
    std::vector<uint8_t> data;
    if (!readPipeMessage(pipe, data)) {
        return false;
    }
    return ipc::decodeFrame(data, frame);
}

bool WindowsPipeServer::writeFrame(HANDLE pipe, const ipc::Frame &frame) {
    return writePipeMessage(pipe, ipc::encodeFrame(frame));
}

} // namespace fcitx::win32