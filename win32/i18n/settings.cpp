#include "settings.h"
#include "../ipc/atomicfile.h"
#include <algorithm>
#include <boost/property_tree/ini_parser.hpp>
#include <charconv>
#include <limits>
#include <memory>
#include <shlobj.h>
#include <sstream>

namespace fcitx::win32::i18n {
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
constexpr const char *names[] = {"system", "zh-Hans", "zh-Hant", "en"};
} // namespace

UiLanguage matchLanguage(std::wstring_view name) {
    std::wstring tag(name);
    std::transform(tag.begin(), tag.end(), tag.begin(), [](wchar_t c) {
        return c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c;
    });
    tag += L'-';
    if (tag.starts_with(L"en-"))
        return UiLanguage::English;
    if (tag.starts_with(L"zh-hant-"))
        return UiLanguage::TraditionalChinese;
    if (tag.starts_with(L"zh-hans-"))
        return UiLanguage::SimplifiedChinese;
    if (tag.starts_with(L"zh-tw-") || tag.starts_with(L"zh-hk-") ||
        tag.starts_with(L"zh-mo-"))
        return UiLanguage::TraditionalChinese;
    if (tag == L"zh-" || tag.starts_with(L"zh-cn-") ||
        tag.starts_with(L"zh-sg-"))
        return UiLanguage::SimplifiedChinese;
    return UiLanguage::System;
}

UiLanguage resolveLanguage(UiLanguage language) {
    if (ipc::validLanguage(language, false))
        return language;
    ULONG count = 0, length = 0;
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr,
                                     &length) ||
        !length)
        return UiLanguage::English;
    std::wstring buffer(length, L'\0');
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(),
                                     &length))
        return UiLanguage::English;
    size_t position = 0;
    while (position < buffer.size() && buffer[position]) {
        const auto end = buffer.find(L'\0', position);
        if (end == std::wstring::npos)
            break;
        const auto candidate = matchLanguage(
            std::wstring_view(buffer).substr(position, end - position));
        if (candidate != UiLanguage::System)
            return candidate;
        position = end + 1;
    }
    return UiLanguage::English;
}

const char *localeName(UiLanguage language) {
    switch (language) {
    case UiLanguage::SimplifiedChinese:
        return "zh_CN";
    case UiLanguage::TraditionalChinese:
        return "zh_TW";
    default:
        return "en_US";
    }
}

std::filesystem::path settingsPath() {
    PWSTR raw = nullptr;
    const auto result = SHGetKnownFolderPath(
        FOLDERID_RoamingAppData, KF_FLAG_DONT_VERIFY, nullptr, &raw);
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owner(raw,
                                                             CoTaskMemFree);
    if (FAILED(result))
        throw std::runtime_error("Cannot locate UI settings");
    return std::filesystem::path(raw) /
           L"Fcitx5/config/fcitx5/conf/windows-ui.conf";
}

bool readSettings(const std::filesystem::path &path,
                  ipc::GlobalSettings &settings) {
    Handle file{
        CreateFileW(path.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND ||
            GetLastError() == ERROR_PATH_NOT_FOUND) {
            settings = {};
            return true;
        }
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size) || size.QuadPart <= 0 ||
        size.QuadPart > 4096)
        return false;
    std::string data(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(file.value, data.data(), static_cast<DWORD>(data.size()),
                  &read, nullptr) ||
        read != data.size())
        return false;
    try {
        boost::property_tree::ptree tree;
        std::istringstream input(data);
        boost::property_tree::read_ini(input, tree);
        if (tree.size() != 1 ||
            tree.get<std::string>("General.Version") != "1" ||
            tree.get_child("General").size() != 3)
            return false;
        const auto language = tree.get<std::string>("General.Language");
        const auto revision = tree.get<std::string>("General.Revision");
        ipc::GlobalSettings parsed;
        const auto value =
            std::from_chars(revision.data(), revision.data() + revision.size(),
                            parsed.revision);
        if (value.ec != std::errc{} ||
            value.ptr != revision.data() + revision.size() || !parsed.revision)
            return false;
        const auto found =
            std::find(std::begin(names), std::end(names), language);
        if (found == std::end(names))
            return false;
        parsed.language = static_cast<UiLanguage>(found - std::begin(names));
        settings = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

ipc::SettingsError saveSettings(const std::filesystem::path &path,
                                const ipc::GlobalSettings &requested,
                                ipc::GlobalSettings &actual) {
    if (!ipc::validLanguage(requested.language) || !requested.revision)
        return ipc::SettingsError::Invalid;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return ipc::SettingsError::SaveFailed;
    auto lockPath = path;
    lockPath += L".lock";
    Handle lock{CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (lock.value == INVALID_HANDLE_VALUE)
        return ipc::SettingsError::SaveFailed;
    OVERLAPPED overlapped{};
    if (!LockFileEx(lock.value,
                    LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1,
                    0, &overlapped))
        return GetLastError() == ERROR_LOCK_VIOLATION
                   ? ipc::SettingsError::Busy
                   : ipc::SettingsError::SaveFailed;
    ipc::GlobalSettings saved;
    if (!readSettings(path, saved))
        return ipc::SettingsError::SaveFailed;
    actual = saved;
    if (saved.language == requested.language)
        return ipc::SettingsError::None;
    if (saved.revision != requested.revision)
        return ipc::SettingsError::Conflict;
    if (saved.revision == std::numeric_limits<uint64_t>::max())
        return ipc::SettingsError::SaveFailed;
    saved.language = requested.language;
    ++saved.revision;
    boost::property_tree::ptree tree;
    tree.put("General.Version", "1");
    tree.put("General.Language", names[static_cast<unsigned>(saved.language)]);
    tree.put("General.Revision", std::to_string(saved.revision));
    std::ostringstream output;
    boost::property_tree::write_ini(output, tree);
    if (!ipc::replaceFile(path, output.str()))
        return ipc::SettingsError::SaveFailed;
    actual = saved;
    return ipc::SettingsError::None;
}
} // namespace fcitx::win32::i18n
