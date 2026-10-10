#include "../ipc/service.h"
#include <cassert>
#include <future>
#include <iostream>

namespace ipc = fcitx::win32::ipc;
namespace fs = std::filesystem;

namespace {
struct Counters {
    LONG core = 0;
    LONG settings = 0;
    DWORD corePid = 0;
};

std::wstring executablePath() {
    std::wstring path(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, path.data(),
                                         static_cast<DWORD>(path.size()));
    assert(size && size < path.size());
    path.resize(size);
    return path;
}

void waitStarts(Counters *counts, LONG core, LONG settings) {
    for (unsigned attempt = 0; attempt < 200; ++attempt) {
        if (InterlockedCompareExchange(&counts->core, 0, 0) == core &&
            InterlockedCompareExchange(&counts->settings, 0, 0) == settings) {
            return;
        }
        Sleep(10);
    }
    assert(false && "service child did not start");
}
} // namespace

int main() {
    const auto executable = fs::path(executablePath());
    const auto child = executable.filename() != L"test_service.exe";
    std::wstring identity;
    if (child) {
        identity.resize(512);
        const auto size = GetEnvironmentVariableW(L"FCITX5_TEST_SERVICE_ID",
                                                  identity.data(), 512);
        assert(size && size < 512);
        identity.resize(size);
    } else {
        identity = ipc::serviceIdentity() + L"-test-" +
                   std::to_wstring(GetCurrentProcessId());
        assert(SetEnvironmentVariableW(L"FCITX5_TEST_SERVICE_ID",
                                       identity.c_str()));
    }
    ipc::ServiceHandle mapping(CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Counters),
        (L"Local\\" + identity + L"-counts").c_str()));
    assert(mapping.get());
    auto *counts = static_cast<Counters *>(MapViewOfFile(
        mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Counters)));
    assert(counts);
    if (child) {
        const auto core = executable.filename() == L"Fcitx5.exe";
        ipc::ServiceProcess process(core ? L"Core" : L"Settings", identity);
        if (process.acquire()) {
            if (core) {
                counts->corePid = GetCurrentProcessId();
                InterlockedIncrement(&counts->core);
            } else {
                InterlockedIncrement(&counts->settings);
            }
            while (!process.stopping()) {
                Sleep(10);
            }
        }
        UnmapViewOfFile(counts);
        return 0;
    }
    const auto prefix =
        fs::temp_directory_path() /
        (L"fcitx5-service-test-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(prefix / "bin");
    fs::create_directories(prefix / "settings");
    const auto core = prefix / "bin" / "Fcitx5.exe";
    const auto settings = prefix / "settings" / "Fcitx5Settings.exe";
    fs::copy_file(executable, core, fs::copy_options::overwrite_existing);
    fs::copy_file(executable, settings, fs::copy_options::overwrite_existing);
    try {
        std::vector<std::future<bool>> starts;
        for (unsigned i = 0; i < 4; ++i) {
            starts.push_back(std::async(std::launch::async, [core, identity] {
                ipc::ServiceController control(core, identity);
                return control.ensureCore();
            }));
        }
        for (auto &start : starts) {
            assert(start.get());
        }
        waitStarts(counts, 1, 0);
        const auto firstPid = counts->corePid;
        {
            ipc::ServiceController control(core, identity);
            assert(control.running(L"Core") && !control.running(L"Settings"));
            ipc::launchServiceExecutable(core, true);
            Sleep(100);
            assert(counts->core == 1);
            control.stop();
            assert(!control.running(L"Core"));
        }
        // No live controller or owner handles remain; manual stop persists.
        {
            ipc::ServiceController control(core, identity);
            assert(!control.ensureCore());
            assert(!control.restart());
        }
        waitStarts(counts, 2, 0);
        assert(counts->corePid != firstPid);
        ipc::launchServiceExecutable(settings, true);
        waitStarts(counts, 2, 1);
        {
            ipc::ServiceController control(core, identity);
            assert(control.restart());
            assert(!control.running(L"Settings"));
        }
        waitStarts(counts, 3, 1);
        ipc::launchServiceExecutable(settings, true);
        waitStarts(counts, 3, 2);
        {
            ipc::ServiceController control(core, identity);
            control.stop();
            control.stop();
            assert(!control.running(L"Core") && !control.running(L"Settings"));
        }
        {
            ipc::ServiceController missing(prefix / "missing.exe", identity);
            bool failed = false;
            try {
                missing.restart();
            } catch (const std::system_error &) {
                failed = true;
            }
            assert(failed && !missing.running(L"Core"));
            assert(!missing.ensureCore());
        }
    } catch (...) {
        ipc::ServiceController control(core, identity);
        control.stop();
        throw;
    }
    UnmapViewOfFile(counts);
    fs::remove(ipc::serviceStatePath(identity));
    // Owner release precedes the final Windows process teardown by a few ms.
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        std::error_code error;
        fs::remove_all(prefix, error);
        if (!error) {
            break;
        }
        Sleep(20);
    }
    assert(!fs::exists(prefix));
    std::cout
        << "PASS: concurrent start, single instance, manual stop, restart, "
           "settings lifetime and missing executable\n";
}
