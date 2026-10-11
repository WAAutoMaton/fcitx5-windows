#pragma once

#include "settings.h"
#include <format>
#include <locale>
#include <memory>
#include <string>

namespace fcitx::win32::i18n {
constexpr const char *mark(const char *, const char *english) {
    return english;
}
class Translator {
  public:
    explicit Translator(UiLanguage language = UiLanguage::English,
                        const std::filesystem::path &directory = {});
    std::string text(const char *context, const char *english) const;
    std::wstring wide(const char *context, const char *english) const;
    template <typename... Args>
    std::wstring format(const char *context, const char *english,
                        Args &&...args) const {
        try {
            return std::vformat(wide(context, english),
                                std::make_wformat_args(args...));
        } catch (const std::format_error &) {
            const Translator fallback;
            return std::vformat(fallback.wide(context, english),
                                std::make_wformat_args(args...));
        }
    }
    UiLanguage language() const { return language_; }
    static std::filesystem::path resourceDirectory();

  private:
    UiLanguage language_;
    std::locale locale_;
};
using Translation = std::shared_ptr<const Translator>;
} // namespace fcitx::win32::i18n
