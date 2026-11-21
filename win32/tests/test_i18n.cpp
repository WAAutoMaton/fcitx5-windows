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
    assert(i18n::matchLanguage(L"ja-JP") == UiLanguage::Japanese);
    assert(i18n::matchLanguage(L"JA") == UiLanguage::Japanese);
    assert(i18n::matchLanguage(L"fr-FR") == UiLanguage::System);
    assert(std::string_view(i18n::localeName(UiLanguage::Japanese)) == "ja_JP");
    i18n::Translator english;
    i18n::Translator simplified(UiLanguage::SimplifiedChinese);
    i18n::Translator traditional(UiLanguage::TraditionalChinese);
    i18n::Translator japanese(UiLanguage::Japanese);
    assert(english.wide("settings", "Global Settings") == L"Global Settings");
    assert(simplified.wide("settings", "Global Settings") ==
           L"\u5168\u5c40\u8bbe\u7f6e");
    assert(traditional.wide("settings", "Global Settings") ==
           L"\u5168\u57df\u8a2d\u5b9a");
    assert(japanese.wide("settings", "Global Settings") ==
           L"\u30b0\u30ed\u30fc\u30d0\u30eb\u8a2d\u5b9a");
    assert(simplified.wide("settings", "Choose Last Character from Phrase") ==
           L"\u9009\u8bcd\u672b\u5b57");
    assert(traditional.wide("settings", "Choose Last Character from Phrase") ==
           L"\u9078\u8a5e\u672b\u5b57");
    assert(
        japanese.wide("settings", "Choose Last Character from Phrase") ==
        L"\u8a9e\u53e5\u306e\u672b\u5c3e\u306e\u6587\u5b57\u3092\u9078\u629e");
    assert(simplified.text("unknown-context", "Global Settings") ==
           "Global Settings");
    assert(traditional.text("settings", "Missing message") ==
           "Missing message");
    assert(japanese.text("settings", "Missing message") == "Missing message");
    assert(simplified.format("settings", "Windows error: {0}", 42) ==
           L"Windows \u9519\u8bef\uff1a42");
    assert(japanese.format("settings", "Windows error: {0}", 42) ==
           L"Windows \u30a8\u30e9\u30fc: 42");
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
    {
        i18n::TranslationMonitor monitor(file);
        monitor.setLanguageReader([](UiLanguage &language) {
            language = UiLanguage::SimplifiedChinese;
            return true;
        });
        const auto deadline = GetTickCount64() + 5000;
        while (monitor.snapshot()->language() !=
                   UiLanguage::SimplifiedChinese &&
               GetTickCount64() < deadline) {
            monitor.poll();
            Sleep(10);
        }
        assert(monitor.snapshot()->language() == UiLanguage::SimplifiedChinese);
        monitor.setLanguageReader([](UiLanguage &) { return false; });
        monitor.poll();
        Sleep(50);
        monitor.poll();
        assert(monitor.snapshot()->language() == UiLanguage::SimplifiedChinese);
        monitor.reset();
    }
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
    assert(ipc::replaceFile(file,
                            "[General]\nVersion=1\nLanguage=en\nRevision=7\n"));
    assert(i18n::readSettings(file, actual) && !actual.detailedLogging &&
           actual.revision == 7);
    request = actual;
    assert(i18n::saveSettings(file, request, actual) ==
           ipc::SettingsError::None);
    {
        std::ifstream input(file);
        const std::string contents{std::istreambuf_iterator<char>(input), {}};
        assert(contents.find("Version=4") != std::string::npos);
        assert(contents.find("DetailedLogging=false") != std::string::npos);
        assert(contents.find("CandidateLayout=vertical") != std::string::npos);
    }
    request.detailedLogging = true;
    assert(i18n::saveSettings(file, request, actual) ==
               ipc::SettingsError::None &&
           actual.detailedLogging && actual.revision == 8);
    request.language = UiLanguage::SimplifiedChinese;
    assert(i18n::saveSettings(file, request, actual) ==
               ipc::SettingsError::Conflict &&
           actual.detailedLogging && actual.language == UiLanguage::English);
    request = actual;
    request.detailedLogging = false;
    assert(i18n::saveSettings(file, request, actual) ==
               ipc::SettingsError::None &&
           !actual.detailedLogging && actual.revision == 9);
    request = actual;
    request.shortcuts[0] = "Control+Shift+J\nAlt+<59>";
    request.shortcuts[5].clear();
    assert(i18n::saveSettings(file, request, actual) ==
           ipc::SettingsError::None);
    ipc::GlobalSettings reloaded;
    assert(i18n::readSettings(file, reloaded) && reloaded == actual &&
           reloaded.shortcuts == request.shortcuts);
    request = actual;
    request.shortcuts[1] = request.shortcuts[0];
    assert(i18n::saveSettings(file, request, actual) ==
           ipc::SettingsError::Invalid);
    request = actual;
    request.shortcuts[0] = "Control+NotAKey";
    assert(i18n::saveSettings(file, request, actual) ==
           ipc::SettingsError::Invalid);
    assert(ipc::replaceFile(file, "[General]\nVersion=2\nLanguage=en\nRevision="
                                  "23\nDetailedLogging=true\n"));
    assert(i18n::readSettings(file, reloaded) && reloaded.revision == 23 &&
           reloaded.detailedLogging &&
           reloaded.shortcuts == ipc::defaultShortcuts());
    assert(ipc::replaceFile(
        file, "[General]\nVersion=3\nLanguage=en\nRevision=24\n"
              "DetailedLogging=true\n[Shortcuts]\n0=Control+Shift+J\n"
              "1=Control+Alt+1\n2=Control+Alt+2\n3=Control+Alt+3\n"
              "4=Control+Alt+4\n5=\n"));
    assert(i18n::readSettings(file, reloaded) && reloaded.revision == 24 &&
           reloaded.candidateLayout == ipc::CandidateLayout::Vertical &&
           reloaded.shortcuts[0] == "Control+Shift+J" &&
           reloaded.shortcuts[5].empty());
    request = reloaded;
    request.candidateLayout = ipc::CandidateLayout::Horizontal;
    assert(i18n::saveSettings(file, request, actual) ==
           ipc::SettingsError::None);
    assert(i18n::readSettings(file, reloaded) && reloaded == actual &&
           reloaded.revision == 25 && reloaded.shortcuts == request.shortcuts &&
           reloaded.candidateLayout == request.candidateLayout);
    request = reloaded;
    request.language = UiLanguage::Japanese;
    assert(i18n::saveSettings(file, request, actual) ==
           ipc::SettingsError::None);
    assert(i18n::readSettings(file, reloaded) && reloaded == actual &&
           reloaded.language == UiLanguage::Japanese &&
           reloaded.revision == 26);
    request.candidateLayout = ipc::CandidateLayout::Vertical;
    assert(i18n::saveSettings(file, request, actual) ==
           ipc::SettingsError::Conflict);
    assert(ipc::replaceFile(
        file,
        "[General]\nVersion=2\nLanguage=en\nRevision=1\nDetailedLogging=1\n"));
    assert(!i18n::readSettings(file, actual));
    assert(ipc::replaceFile(
        file, "[General]\nVersion=1\nLanguage=invalid\nRevision=1\n"));
    assert(!i18n::readSettings(file, actual));
    request = actual;
    assert(i18n::saveSettings(file, request, actual) ==
           ipc::SettingsError::SaveFailed);
    assert(ipc::replaceFile(file, std::string(4097, 'x')));
    assert(!i18n::readSettings(file, actual));
    std::filesystem::remove_all(directory);
}
