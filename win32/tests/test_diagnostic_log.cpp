#include "../ipc/diagnostic_log.h"
#include <boost/property_tree/json_parser.hpp>
#include <cassert>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <windows.h>

namespace diagnostic = fcitx::win32::diagnostic;
using namespace std::chrono_literals;

namespace {
void put(const std::filesystem::path &path, size_t size = 64) {
    std::ofstream output(path, std::ios::binary);
    output << std::string(size, 'x');
    assert(output.good());
}
std::string contents(const std::filesystem::path &directory) {
    std::string result;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (!diagnostic::ownedLogName(entry.path().filename().string()))
            continue;
        std::ifstream input(entry.path());
        result.append(std::istreambuf_iterator<char>(input), {});
    }
    return result;
}
} // namespace

int main() {
    diagnostic::Options defaults;
    assert(defaults.retention == 7 * 24h);
    assert(defaults.directoryBytes == 512U * 1024U * 1024U);

    wchar_t temp[MAX_PATH]{};
    assert(GetTempPathW(MAX_PATH, temp));
    const auto root =
        std::filesystem::path(temp) /
        (L"fcitx5-diagnostics-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(GetTickCount64()));
    diagnostic::Options options;
    options.directory = root / "off";
    {
        diagnostic::Logger logger("core", "test-build", options);
        logger.event(diagnostic::Level::Info, "disabled");
        logger.maintain();
        assert(logger.flush(5s));
        assert(!std::filesystem::exists(options.directory));
        logger.setEnabled(true);
        assert(logger.flush(5s));
        logger.event(diagnostic::Level::Info, "escaped",
                     {{"metadata", "line\n\r\t\"\\"}});
        assert(logger.flush(5s));
        auto text = contents(options.directory);
        std::istringstream lines(text);
        std::string line;
        bool found = false;
        while (std::getline(lines, line)) {
            boost::property_tree::ptree json;
            std::istringstream input(line);
            boost::property_tree::read_json(input, json);
            assert(json.get<std::string>("role") == "core");
            assert(json.get<DWORD>("pid") == GetCurrentProcessId());
            assert(!json.get<std::string>("session").empty());
            assert(json.get<std::string>("build") == "test-build");
            assert(json.get<std::string>("time").back() == 'Z');
            if (json.get<std::string>("event") == "escaped") {
                found = true;
                assert(json.get<std::string>("metadata") == "line\n\r\t\"\\");
            }
        }
        assert(found);
        logger.setEnabled(false);
        logger.event(diagnostic::Level::Error, "must_not_record");
        assert(logger.flush(5s));
        assert(contents(options.directory) == text);
    }

    assert(diagnostic::ownedLogName("core-20261015.log.42"));
    assert(diagnostic::ownedLogName("tsf-123-20261015.log"));
    assert(diagnostic::ownedLogName("settings-123-20261015.log.1"));
    for (const auto name :
         {"core-other.log", "core-20261015.log.backup", "tsf-20261015.log",
          "settings-x-20261015.log", "core-20261015.log.1.txt"})
        assert(!diagnostic::ownedLogName(name));

    options.directory = root / "retention";
    std::filesystem::create_directories(options.directory);
    const auto expired = options.directory / "core-20000101.log";
    const auto retained = options.directory / "core-20261010.log";
    const auto foreign = options.directory / "core-not-ours.log";
    put(expired);
    put(retained);
    put(foreign);
    std::filesystem::last_write_time(
        expired, std::filesystem::file_time_type::clock::now() - 8 * 24h);
    std::filesystem::last_write_time(
        retained, std::filesystem::file_time_type::clock::now() - 6 * 24h);
    {
        diagnostic::Logger logger("core", {}, options);
        logger.maintain();
        assert(logger.flush(5s));
        assert(!std::filesystem::exists(expired));
        assert(std::filesystem::exists(retained));
        assert(std::filesystem::exists(foreign));
    }

    options.directory = root / "rotation";
    options.rotateBytes = 1024;
    options.processBytes = 4096;
    options.directoryBytes = 8192;
    {
        diagnostic::Logger logger("core", {}, options);
        logger.setEnabled(true);
        assert(logger.flush(5s));
        for (unsigned i = 0; i < 40; ++i) {
            logger.event(diagnostic::Level::Info, "rotation",
                         {{"sequence", i}});
            assert(logger.flush(5s));
        }
        const auto status = logger.status();
        assert(status.fileCount > 1 &&
               status.totalBytes <= options.processBytes);
        bool current = false, rotated = false;
        for (const auto &entry :
             std::filesystem::directory_iterator(options.directory)) {
            const auto name = entry.path().filename().string();
            current = current ||
                      (name.starts_with("core-") && name.ends_with(".log"));
            rotated = rotated ||
                      (name.starts_with("core-") && name.ends_with(".log.1"));
        }
        assert(current && rotated);
        for (const auto &entry :
             std::filesystem::directory_iterator(options.directory))
            assert(entry.file_size() <= options.rotateBytes);
    }

    options.directory = root / "capacity";
    options.directoryBytes = 128;
    std::filesystem::create_directories(options.directory);
    put(options.directory / "tsf-1-20261015.log", 128);
    put(options.directory / "settings-2-20261015.log", 128);
    put(options.directory / "unrelated.txt", 256);
    const auto activePath = options.directory / "tsf-99-20261015.log";
    put(activePath, 64);
    std::filesystem::last_write_time(
        activePath, std::filesystem::file_time_type::clock::now() - 31 * 24h);
    const auto active = CreateFileW(
        activePath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(active != INVALID_HANDLE_VALUE);
    {
        diagnostic::Logger logger("core", {}, options);
        logger.maintain();
        assert(logger.flush(5s));
        assert(logger.status().totalBytes <= 128);
        assert(std::filesystem::exists(options.directory / "unrelated.txt"));
        assert(std::filesystem::exists(activePath));
    }
    CloseHandle(active);

    options.directory = root / "daily";
    options.directoryBytes = 8192;
    auto clock = std::chrono::system_clock::now();
    options.clock = [&] { return clock; };
    {
        diagnostic::Logger logger("settings", {}, options);
        logger.setEnabled(true);
        assert(logger.flush(5s));
        clock += 24h;
        logger.event(diagnostic::Level::Info, "next_day");
        assert(logger.flush(5s));
        assert(logger.status().fileCount == 2);
    }
    options.clock = {};

    options.directory = root / "concurrent";
    options.directoryBytes = 8U * 1024U * 1024U;
    options.processBytes = options.directoryBytes;
    options.rotateBytes = options.directoryBytes;
    options.queueLimit = 128;
    {
        diagnostic::Logger logger("tsf", {}, options);
        logger.setEnabled(true);
        assert(logger.flush(5s));
        std::vector<std::thread> threads;
        for (unsigned thread = 0; thread < 4; ++thread) {
            threads.emplace_back([&logger, thread] {
                for (unsigned index = 0; index < 500; ++index)
                    logger.event(diagnostic::Level::Debug, "concurrent",
                                 {{"producer", thread}, {"index", index}});
            });
        }
        for (auto &thread : threads)
            thread.join();
        assert(logger.flush(5s));
        assert(logger.status().queued == 0);
        std::istringstream lines(contents(options.directory));
        std::string line;
        while (std::getline(lines, line)) {
            boost::property_tree::ptree json;
            std::istringstream input(line);
            boost::property_tree::read_json(input, json);
            assert(json.get<std::string>("role") == "tsf");
        }
    }

    options.directory = root / "overflow";
    options.directoryBytes = 8192;
    options.queueLimit = 0;
    {
        diagnostic::Logger logger("core", {}, options);
        logger.setEnabled(true);
        logger.event(diagnostic::Level::Info, "overflow");
        assert(logger.status().dropped >= 2);
        logger.maintain();
        assert(logger.flush(5s));
        assert(contents(options.directory).find("log_queue_dropped") !=
               std::string::npos);
    }

    options.directory = root / "unwritable";
    options.queueLimit = 4096;
    put(options.directory);
    {
        diagnostic::Logger logger("tsf", {}, options);
        logger.setEnabled(true);
        assert(logger.flush(5s));
        assert(logger.status().state == diagnostic::State::Unavailable);
        assert(logger.status().error != 0);
        logger.setEnabled(false);
        assert(logger.status().state == diagnostic::State::Off);
    }
    std::filesystem::remove_all(root);
}
