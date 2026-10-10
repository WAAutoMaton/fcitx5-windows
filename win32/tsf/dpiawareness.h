#pragma once

#include <windows.h>

namespace fcitx {
class ScopedDpiAwareness {
  public:
    explicit ScopedDpiAwareness(DPI_AWARENESS_CONTEXT context)
        : previous_(SetThreadDpiAwarenessContext(context)) {}
    ScopedDpiAwareness(const ScopedDpiAwareness &) = delete;
    ScopedDpiAwareness &operator=(const ScopedDpiAwareness &) = delete;
    ~ScopedDpiAwareness() {
        if (previous_) {
            SetThreadDpiAwarenessContext(previous_);
        }
    }
    bool valid() const { return previous_ != nullptr; }

  private:
    DPI_AWARENESS_CONTEXT previous_;
};
} // namespace fcitx
