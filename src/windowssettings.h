#pragma once

#include "../win32/ipc/protocol.h"
#include <fcitx-config/rawconfig.h>
#include <fcitx/instance.h>
#include <filesystem>

namespace fcitx::win32 {

class WindowsSettings {
  public:
    explicit WindowsSettings(Instance &instance,
                             std::filesystem::path file = {});
    ipc::SettingsReply
    snapshot(ipc::SettingsError error = ipc::SettingsError::None) const;
    ipc::SettingsError apply(const ipc::PinyinSettings &settings);
    ipc::SettingsError openWindow() const;
    const char *chineseEntry() const;
    uint64_t revision() const { return settings_.revision; }
    bool resetRequired() const { return resetRequired_; }
    bool chineseMode(InputContext *context) const;
    ipc::InputMethodsReply
    inputMethods(ipc::SettingsError error = ipc::SettingsError::None) const;
    ipc::SettingsError
    applySelection(const ipc::InputMethodSelection &selection);
    ipc::MethodConfigReply
    methodConfig(const std::string &id,
                 ipc::SettingsError error = ipc::SettingsError::None) const;
    ipc::SettingsError
    applyMethodConfig(const ipc::MethodConfigRequest &request);
    bool select(InputContext *context, const std::string &id);
    bool cycle(InputContext *context);
    bool selectIndex(InputContext *context, size_t index);
    bool setMode(InputContext *context, bool enabled);
    void decorate(InputContext *context, ipc::KeyReply &reply) const;
    void reconcile(InputContext *context);

  private:
    Instance &instance_;
    std::filesystem::path file_;
    ipc::PinyinSettings settings_;
    bool resetRequired_ = false;
    std::vector<ipc::InputMethodInfo> methods_;
    std::vector<std::string> enabled_;
    RawConfig overrides_;
    void initializeMethods();
    const ipc::InputMethodInfo *method(const std::string &id) const;
    bool selected(const std::string &id) const;
    std::string configKey(const std::string &id) const;
    RawConfig methodValues(const InputMethodEntry &entry) const;
    void setMethodValues(const InputMethodEntry &entry,
                         const RawConfig &values);
    bool applyProfile(ipc::ShuangpinProfile profile);
    bool save(const ipc::PinyinSettings &settings) const;
};

} // namespace fcitx::win32
