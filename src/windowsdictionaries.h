#pragma once

#include "../win32/ipc/protocol.h"
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx/instance.h>
#include <filesystem>
#include <map>

namespace fcitx::win32 {

class WindowsDictionaries {
  public:
    explicit WindowsDictionaries(Instance &instance);
    ipc::DictionariesReply snapshot() const;

  private:
    struct Stamp {
        uintmax_t size;
        std::filesystem::file_time_type modified;
        bool operator==(const Stamp &) const = default;
    };
    using Files = std::map<std::filesystem::path, Stamp>;
    bool scan(Files &files);
    void poll();
    Instance &instance_;
    std::filesystem::path directory_;
    Files observed_;
    Files applied_;
    std::string error_;
    std::unique_ptr<EventSourceTime> timer_;
};

} // namespace fcitx::win32
