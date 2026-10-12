#include "../i18n/monitor.h"
#include "../i18n/translator.h"
#include "../ipc/atomicfile.h"
#include <cassert>
#include <fstream>

int main() {
    using namespace fcitx::win32;
    using ipc::UiLanguage;
    assert(i18n::matchLanguage(L"zh-Hant-HK") ==
           UiLanguage::TraditionalChinese);
    assert(i18n::matchLanguage(L"ZH-HANS-SG") == UiLanguage::SimplifiedChinese);
    assert(i18n::matchLanguage(L"zh-TW") == UiLanguage::TraditionalChinese);
    assert(i18n::matchLanguage(L"en-GB") == UiLanguage::English);
    assert(i18n::matchLanguage(L"fr-FR") == UiLanguage::System);
    i18n::Translator english;
    i18n::Translator simplified(UiLanguage::SimplifiedChinese);
    i18n::Translator traditional(UiLanguage::TraditionalChinese);
    assert(english.wide("settings", "Global Settings") == L"Global Settings");
    assert(simplified.wide("settings", "Global Settings") ==
           L"\u5168\u5c40\u8bbe\u7f6e");
    assert(traditional.wide("settings", "Global Settings") ==
           L"\u5168\u57df\u8a2d\u5b9a");
    assert(simplified.wide("settings", "Choose Last Character from Phrase") ==
           L"\u9009\u8bcd\u672b\u5b57");
    assert(traditional.wide("settings", "Choose Last Character from Phrase") ==
           L"\u9078\u8a5e\u672b\u5b57");
    assert(simplified.text("unknown-context", "Global Settings") ==
           "Global Settings");
    assert(traditional.text("settings", "Missing message") ==
           "Missing message");
    assert(simplified.format("settings", "Windows error: {0}", 42) ==
           L"Windows \u9519\u8bef\uff1a42");
    i18n::Translator missing(UiLanguage::SimplifiedChinese,
                             L"nonexistent-localization-directory");
    assert(missing.text("settings", "Global Settings") == "Global Settings");

    wchar_t temporary[MAX_PATH]{};
    assert(GetTempPathW(MAX_PATH, temporary));
    const auto directory =
        std::filesystem::path(temporary) /
        (L"fcitx5-ui-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(directory);
    const auto file = directory / "windows-ui.conf";
    ipc::GlobalSettings actual;
    assert(i18n::readSettings(file, actual) && actual == ipc::GlobalSettings{});
    ipc::GlobalSettings request{UiLanguage::TraditionalChinese, 1};
    assert(i18n::saveSettings(file, request, actual) ==
           ipc::SettingsError::None);
    assert(actual.language == request.language && actual.revision == 2);
    {
        i18n::TranslationMonitor monitor(file);
        const auto deadline = GetTickCount64() + 5000;
        while (monitor.snapshot()->language() !=
                   UiLanguage::TraditionalChinese &&
               GetTickCount64() < deadline) {
            monitor.poll();
            Sleep(10);
        }
        assert(monitor.snapshot()->language() ==
               UiLanguage::TraditionalChinese);
        assert(monitor.snapshot()->wide("settings", "Global Settings") ==
               L"\u5168\u57df\u8a2d\u5b9a");
        monitor.reset();
    }
    assert(i18n::saveSettings(file, request, actual) ==
               ipc::SettingsError::None &&
           actual.revision == 2);
    request.language = UiLanguage::English;
    assert(i18n::saveSettings(file, request, actual) ==
           ipc::SettingsError::Conflict);
    request.revision = actual.revision;
    const auto lockFile = file.wstring() + L".lock";
    HANDLE lock = CreateFileW(lockFile.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(lock != INVALID_HANDLE_VALUE);
    OVERLAPPED overlapped{};
    assert(LockFileEx(lock, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                      0, 1, 0, &overlapped));
    assert(i18n::saveSettings(file, request, actual) ==
           ipc::SettingsError::Busy);
    CloseHandle(lock);
    assert(SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY));
    assert(i18n::saveSettings(file, request, actual) ==
           ipc::SettingsError::SaveFailed);
    assert(actual.language == UiLanguage::TraditionalChinese &&
           actual.revision == 2);
    assert(SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL));
    assert(ipc::replaceFile(
        file, "[General]\nVersion=1\nLanguage=invalid\nRevision=1\n"));
    assert(!i18n::readSettings(file, actual));
    assert(i18n::saveSettings(file, request, actual) ==
           ipc::SettingsError::SaveFailed);
    assert(ipc::replaceFile(file, std::string(4097, 'x')));
    assert(!i18n::readSettings(file, actual));
    std::filesystem::remove_all(directory);
}
