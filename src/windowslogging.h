#pragma once

#include <ostream>
#include <streambuf>

namespace fcitx::win32 {
void applyDiagnosticLogging(bool enabled);

// Collect the fragments of each upstream message until Fcitx flushes it.
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
