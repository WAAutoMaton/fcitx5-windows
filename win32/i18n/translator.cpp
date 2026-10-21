#include "translator.h"
#include "../ipc/installation.h"
#include <boost/locale/encoding_utf.hpp>
#include <boost/locale/gnu_gettext.hpp>
#include <boost/locale/message.hpp>
#include <windows.h>

namespace fcitx::win32::i18n {
std::filesystem::path Translator::resourceDirectory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&Translator::resourceDirectory), &module))
        throw std::runtime_error("Cannot locate translation module");
    std::wstring path(32768, L'\0');
    const auto size = GetModuleFileNameW(module, path.data(),
                                         static_cast<DWORD>(path.size()));
    if (!size || size >= path.size())
        throw std::runtime_error("Cannot locate translation directory");
    path.resize(size);
    return ipc::installationRootForModulePath(path) / L"share/locale";
}

Translator::Translator(UiLanguage language,
                       const std::filesystem::path &directory)
    : language_(resolveLanguage(language)), locale_(std::locale::classic()) {
    if (language_ == UiLanguage::English)
        return;
    try {
        const auto path = directory.empty() ? resourceDirectory() : directory;
        boost::locale::gnu_gettext::messages_info info;
        info.language = language_ == UiLanguage::English ? "en" : "zh";
        info.country = language_ == UiLanguage::SimplifiedChinese    ? "CN"
                       : language_ == UiLanguage::TraditionalChinese ? "TW"
                                                                     : "US";
        info.encoding = "UTF-8";
        info.domains.emplace_back("fcitx5-windows");
        info.paths.emplace_back(".");
        info.callback = [path](const std::string &file, const std::string &) {
            // Keep path handling UTF-16 even when the installation directory is
            // non-ASCII.
            const auto fullPath =
                path / std::filesystem::path(
                           boost::locale::conv::utf_to_utf<wchar_t>(file));
            std::vector<char> bytes;
            HANDLE handle =
                CreateFileW(fullPath.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                return bytes;
            LARGE_INTEGER size{};
            if (GetFileSizeEx(handle, &size) && size.QuadPart > 0 &&
                size.QuadPart <= 4 * 1024 * 1024) {
                bytes.resize(static_cast<size_t>(size.QuadPart));
                DWORD read = 0;
                if (!ReadFile(handle, bytes.data(),
                              static_cast<DWORD>(bytes.size()), &read,
                              nullptr) ||
                    read != bytes.size())
                    bytes.clear();
            }
            CloseHandle(handle);
            return bytes;
        };
        locale_ = std::locale(
            locale_,
            boost::locale::gnu_gettext::create_messages_facet<char>(info));
    } catch (...) {
    }
}

std::string Translator::text(const char *context, const char *english) const {
    try {
        return boost::locale::translate(context, english).str(locale_);
    } catch (...) {
        return english;
    }
}

std::wstring Translator::wide(const char *context, const char *english) const {
    try {
        return boost::locale::conv::utf_to_utf<wchar_t>(text(context, english));
    } catch (...) {
        return boost::locale::conv::utf_to_utf<wchar_t>(english);
    }
}
} // namespace fcitx::win32::i18n
