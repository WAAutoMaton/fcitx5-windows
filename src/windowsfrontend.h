#pragma once

#include "../win32/ipc/protocol.h"
#include "windowssettings.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/instance.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <windows.h>

namespace fcitx::win32 {

class WindowsInputContext final : public InputContext {
  public:
    WindowsInputContext(InputContextManager &manager, uint64_t id,
                        std::string program);
    ~WindowsInputContext() override;

    const char *frontend() const override { return "win32"; }

    void setCapabilities(uint64_t value);
    std::string takeCommit();
    std::string preedit() const;
    uint32_t preeditCursor() const;
    ipc::KeyReply snapshot(bool consumed, bool enabled);
    void resetPreedit();

  protected:
    void commitStringImpl(const std::string &text) override;
    void deleteSurroundingTextImpl(int offset, unsigned int size) override;
    void forwardKeyImpl(const ForwardKeyEvent &key) override;
    void updatePreeditImpl() override;

  private:
    uint64_t id_;
    std::string pendingCommit_;
    uint64_t revision_ = 0;
};

class WindowsPipeServer {
  public:
    WindowsPipeServer(Instance &instance, EventDispatcher &dispatcher,
                      std::filesystem::path settingsFile = {});
    ~WindowsPipeServer();

    void start();
    void stop();

  private:
    struct PendingReply {
        std::mutex mutex;
        std::condition_variable condition;
        bool ready = false;
        ipc::Frame frame;
    };

    void run();
    void runClient(HANDLE pipe, uint64_t clientId);
    void process(const ipc::Frame &request,
                 const std::shared_ptr<PendingReply> &pending,
                 uint64_t clientId);
    void complete(const std::shared_ptr<PendingReply> &pending,
                  ipc::Frame response);
    bool readFrame(HANDLE pipe, ipc::Frame &frame);
    bool writeFrame(HANDLE pipe, const ipc::Frame &frame);
    ipc::KeyReply snapshot(WindowsInputContext &context, bool consumed);
    Instance &instance_;
    EventDispatcher &dispatcher_;
    WindowsSettings settings_;
    uint64_t settingsEpoch_ = 1;
    std::atomic_bool stopping_ = false;
    std::thread thread_;
    HANDLE stopEvent_ = nullptr;
    struct ClientThread {
        std::thread thread;
        std::shared_ptr<std::atomic_bool> finished;
    };
    std::vector<ClientThread> clients_;
    std::unordered_map<uint64_t, uint64_t> owners_;
    uint64_t nextContextId_ = 1;
    std::unordered_map<uint64_t, std::unique_ptr<WindowsInputContext>>
        contexts_;
};

} // namespace fcitx::win32
