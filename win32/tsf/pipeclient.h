#pragma once

#include "../ipc/protocol.h"
#include "../ipc/sandbox.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <windows.h>

namespace fcitx {
namespace ipc = win32::ipc;

class PipeClient {
  public:
    using KeyReply = ipc::KeyReply;

    explicit PipeClient(std::wstring name = {}, ipc::SandboxieApi sandbox = {})
        : name_(std::move(name)), sandbox_(sandbox) {}
    ~PipeClient();

    bool connect();
    void disconnect();
    bool connected() const { return pipe_ != INVALID_HANDLE_VALUE; }
    DWORD connectionError() const { return connectionError_; }

    bool createContext(uint64_t capabilities, uint64_t &contextId);
    bool focusIn(uint64_t contextId);
    bool focusOut(uint64_t contextId);
    bool destroyContext(uint64_t contextId);
    bool key(uint64_t contextId, bool release, uint32_t virtualKey,
             uint32_t scanCode, uint32_t modifiers, uint32_t time,
             KeyReply &reply, uint32_t unicode = 0);
    bool poll(uint64_t contextId, KeyReply &reply);
    bool reset(uint64_t contextId, KeyReply &reply);
    bool setMode(uint64_t contextId, bool enabled, KeyReply &reply);
    bool cycleInputMethod(uint64_t contextId, KeyReply &reply);
    bool selectInputMethod(uint64_t contextId, const std::string &id,
                           KeyReply &reply);
    bool getInputMethods(ipc::InputMethodsReply &reply);
    bool setInputMethods(const ipc::InputMethodSelection &selection,
                         ipc::InputMethodsReply &reply);
    bool getMethodConfig(const std::string &id, ipc::MethodConfigReply &reply);
    bool setMethodConfig(const ipc::MethodConfigRequest &config,
                         ipc::MethodConfigReply &reply);
    bool getSettings(ipc::SettingsReply &reply);
    bool getGlobalSettings(ipc::GlobalSettingsReply &reply);
    bool setGlobalSettings(const ipc::GlobalSettings &settings,
                           ipc::GlobalSettingsReply &reply);
    bool getDictionaries(ipc::DictionariesReply &reply);
    bool setSettings(const ipc::PinyinSettings &settings,
                     ipc::SettingsReply &reply);
    bool openSettings(ipc::SettingsReply &reply);
    bool hostCommand(ipc::MessageType command, DWORD &error);

  private:
    bool request(ipc::MessageType type, uint64_t contextId,
                 const std::vector<uint8_t> &payload, ipc::Frame &response);
    bool readFrame(ipc::Frame &frame);
    bool writeFrame(const ipc::Frame &frame);

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::wstring name_;
    ipc::SandboxieApi sandbox_;
    DWORD connectionError_ = ERROR_SUCCESS;
    uint64_t nextRequestId_ = 1;
    mutable std::mutex mutex_;
};

} // namespace fcitx
