#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <windows.h>
#include "../ipc/protocol.h"

namespace fcitx {
namespace ipc = win32::ipc;

class PipeClient {
  public:
    struct KeyReply {
        bool consumed = false;
        std::string commit;
        std::string preedit;
        uint32_t preeditCursor = 0;
    };

    PipeClient() = default;
    ~PipeClient();

    bool connect();
    void disconnect();
    bool connected() const { return pipe_ != INVALID_HANDLE_VALUE; }

    bool createContext(uint64_t capabilities, uint64_t &contextId);
    bool focusIn(uint64_t contextId);
    bool focusOut(uint64_t contextId);
    bool destroyContext(uint64_t contextId);
    bool key(uint64_t contextId, bool release, uint32_t virtualKey,
             uint32_t scanCode, uint32_t modifiers, uint32_t time,
             KeyReply &reply);

  private:
    bool request(ipc::MessageType type, uint64_t contextId,
                 const std::vector<uint8_t> &payload, ipc::Frame &response);
    bool readFrame(ipc::Frame &frame);
    bool writeFrame(const ipc::Frame &frame);

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    uint64_t nextRequestId_ = 1;
    mutable std::mutex mutex_;
};

} // namespace fcitx