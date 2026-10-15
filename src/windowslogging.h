#pragma once

#include <ostream>
#include <streambuf>

namespace fcitx::win32 {
void applyDiagnosticLogging(bool enabled);

// Upstream messages may contain addon data. Persist only their level/length;
// Windows-specific events carry the audited diagnostic fields.
class CoreLogStream : private std::streambuf {
  public:
    CoreLogStream();
    ~CoreLogStream();

  private:
    std::streamsize xsputn(const char *data, std::streamsize count) override;
    int_type overflow(int_type ch) override;
    int sync() override;
    std::ostream stream_{this};
};
} // namespace fcitx::win32
