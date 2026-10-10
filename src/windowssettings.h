#pragma once

#include "../win32/ipc/protocol.h"
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

  private:
    Instance &instance_;
    std::filesystem::path file_;
    ipc::PinyinSettings settings_;
    bool resetRequired_ = false;
    bool applyProfile(ipc::ShuangpinProfile profile);
    bool save(const ipc::PinyinSettings &settings) const;
};

} // namespace fcitx::win32
