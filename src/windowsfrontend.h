#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/instance.h>
#include <windows.h>
#include "../win32/ipc/protocol.h"

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

  protected:
    void commitStringImpl(const std::string &text) override;
    void deleteSurroundingTextImpl(int offset, unsigned int size) override;
    void forwardKeyImpl(const ForwardKeyEvent &key) override;
    void updatePreeditImpl() override;

  private:
    uint64_t id_;
    std::string pendingCommit_;
};

class WindowsPipeServer {
  public:
    WindowsPipeServer(Instance &instance, EventDispatcher &dispatcher);
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
    void runClient(HANDLE pipe);
    void process(const ipc::Frame &request,
                 const std::shared_ptr<PendingReply> &pending);
    void complete(const std::shared_ptr<PendingReply> &pending,
                  ipc::Frame response);
    bool readFrame(HANDLE pipe, ipc::Frame &frame);
    bool writeFrame(HANDLE pipe, const ipc::Frame &frame);
    void closePipe();

    Instance &instance_;
    EventDispatcher &dispatcher_;
    std::atomic_bool stopping_ = false;
    std::thread thread_;
    std::mutex pipeMutex_;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    uint64_t nextContextId_ = 1;
    std::unordered_map<uint64_t, std::unique_ptr<WindowsInputContext>> contexts_;
};

} // namespace fcitx::win32