#include "../ipc/installation.h"
#include "../setup/upgrade.h"
#include <boost/property_tree/json_parser.hpp>
#include <cassert>
#include <iostream>
#include <sstream>

namespace upgrade = fcitx::setup::upgrade;
namespace fs = std::filesystem;
using Tree = boost::property_tree::ptree;

namespace {
void saveManifest(const fs::path &root, const std::vector<std::string> &files,
                  std::string_view version) {
    Tree tree, list, dependencies;
    tree.put("schemaVersion", 1);
    tree.put("architecture", "x64");
    tree.put("protocolVersion", fcitx::win32::ipc::kVersion);
    tree.put("version", version);
    tree.put("package", "test-package");
    for (const auto &path : files) {
        Tree file;
        file.put("path", path);
        file.put("size", fs::file_size(root / path));
        file.put("sha256", upgrade::sha256(root / path));
        list.push_back({"", file});
    }
    tree.add_child("files", list);
    tree.add_child("dependencies", dependencies);
    std::ostringstream stream;
    boost::property_tree::write_json(stream, tree);
    upgrade::writeText(root / "setup" / "runtime-manifest.json", stream.str());
}

class FakeMachine : public upgrade::Machine {
  public:
    explicit FakeMachine(fs::path path) : root(std::move(path)) {}
    void validate() override {}
    void gate(std::string_view state) override {
        upgrade::writeText(root / "setup" / "install-state",
                           std::string(state) + "\n");
    }
    bool sameBoot() override { return boot; }
    void markBoot(bool present) override { boot = present; }
    void task(bool enable, std::string_view) override {
        if (!enable && taskRemovalError)
            upgrade::fail(ERROR_ACCESS_DENIED);
        armed = enable;
    }
    void controls(bool install, const fs::path &) override { routed = install; }
    void snapshot(const fs::path &) override {
        snapshotVersion = activeVersion;
    }
    void restore(const fs::path &) override {
        activeVersion = snapshotVersion;
        ++restores;
    }
    void registerVersion(const upgrade::Manifest &value) override {
        activeVersion = value.version;
        if (registrationError)
            upgrade::fail(ERROR_INSTALL_FAILURE);
    }
    void verifyVersion(const upgrade::Manifest &value) override {
        assert(activeVersion == value.version);
    }
    DWORD dependencies(const fs::path &, const upgrade::Manifest &) override {
        return dependencyError;
    }
    fs::path root;
    bool boot = true, armed = false, routed = false;
    bool registrationError = false;
    bool taskRemovalError = false;
    DWORD dependencyError = 0;
    unsigned restores = 0;
    std::string activeVersion = "1.0.0.0", snapshotVersion;
};

struct Crash {};
class Fixture {
  public:
    explicit Fixture(unsigned index)
        : root(fs::temp_directory_path() /
               ("fcitx5-upgrade-engine-" +
                std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(index))),
          source(root / "source"), machine(root), engine(root, machine) {
        fs::create_directories(root / "setup");
        fs::create_directories(source / "setup");
        paths = {"bin/Fcitx5.exe",         "tsf/fcitx5-x86_64.dll",
                 "tsf/x86/fcitx5-x86.dll", "setup/Fcitx5SetupHelper.exe",
                 "setup/managed-install",  "settings/Fcitx5Settings.exe",
                 "share/data.txt"};
        for (const auto &path : paths) {
            fs::create_directories((root / path).parent_path());
            fs::create_directories((source / path).parent_path());
            upgrade::writeText(root / path, "old:" + path);
            upgrade::writeText(source / path, "new:" + path);
        }
        upgrade::writeText(root / "share/obsolete.txt", "obsolete");
        auto old = paths;
        old.push_back("share/obsolete.txt");
        saveManifest(root, old, "1.0.0.0");
        upgrade::writeText(source / "share/new.txt", "added");
        paths.push_back("share/new.txt");
        saveManifest(source, paths, "2.0.0.0");
        machine.gate("installed");
    }
    ~Fixture() { fs::remove_all(root); }
    void stage() {
        assert(engine.begin(source / "setup/runtime-manifest.json") == 0);
        for (const auto &path : paths) {
            const auto target = root / "setup/upgrades/current/payload" / path;
            fs::create_directories(target.parent_path());
            fs::copy_file(source / path, target);
        }
        assert(engine.seal() == ERROR_SUCCESS_REBOOT_REQUIRED);
        assert(machine.boot && machine.armed && machine.routed);
        assert(upgrade::readText(root / "bin/Fcitx5.exe") ==
               "old:bin/Fcitx5.exe");
        assert(upgrade::readText(root / "setup/install-state") ==
               "installed\n");
    }
    void oldVersion() {
        assert(upgrade::readText(root / "tsf/fcitx5-x86_64.dll") ==
               "old:tsf/fcitx5-x86_64.dll");
        assert(upgrade::readText(root / "tsf/x86/fcitx5-x86.dll") ==
               "old:tsf/x86/fcitx5-x86.dll");
        assert(upgrade::readText(root / "bin/Fcitx5.exe") ==
               "old:bin/Fcitx5.exe");
        assert(upgrade::readText(root / "share/obsolete.txt") == "obsolete");
        assert(!fs::exists(root / "share/new.txt"));
        assert(upgrade::readText(root / "setup/install-state") ==
               "installed\n");
        assert(machine.activeVersion == "1.0.0.0");
    }
    fs::path root, source;
    std::vector<std::string> paths;
    FakeMachine machine;
    upgrade::Engine engine;
};
} // namespace

int main() {
    {
        Fixture test(1);
        test.stage();
        assert(test.engine.complete() == ERROR_SUCCESS_REBOOT_REQUIRED);
        assert(test.engine.begin(test.source / "setup/runtime-manifest.json") ==
               ERROR_SUCCESS_REBOOT_REQUIRED);
        test.machine.boot = false;
        {
            upgrade::FileGuard occupied(test.root / "tsf/fcitx5-x86_64.dll");
            assert(test.engine.complete() == ERROR_SUCCESS_REBOOT_REQUIRED);
            assert(upgrade::transactionState(test.root) == "staged");
        }
        test.oldVersion();
        test.machine.boot = false;
        {
            upgrade::FileGuard occupied(test.root / "tsf/x86/fcitx5-x86.dll");
            assert(test.engine.complete() == ERROR_SUCCESS_REBOOT_REQUIRED);
            assert(upgrade::transactionState(test.root) == "staged");
        }
        test.oldVersion();
        test.machine.boot = false;
        assert(test.engine.complete() == 0);
        assert(test.machine.activeVersion == "2.0.0.0");
        assert(!test.machine.armed && !upgrade::hasTransaction(test.root));
        assert(!fs::exists(test.root / "share/obsolete.txt"));
        assert(upgrade::readText(test.root / "share/new.txt") == "added");
        upgrade::verify(
            test.root,
            upgrade::manifest(test.root / "setup/runtime-manifest.json").files);
        assert(test.engine.query().find("committed") != std::string::npos);
    }
    {
        Fixture test(2);
        test.stage();
        assert(test.engine.cancel() == 0);
        assert(!test.machine.armed && !test.machine.routed);
        test.oldVersion();
    }
    {
        Fixture test(3);
        test.stage();
        test.machine.boot = false;
        test.machine.registrationError = true;
        assert(test.engine.complete() == ERROR_INSTALL_FAILURE);
        test.oldVersion();
        assert(test.machine.restores == 1);
        assert(!test.machine.armed);
    }
    unsigned index = 10;
    for (auto checkpoint :
         {"applying", "bin/Fcitx5.exe", "tsf/x86/fcitx5-x86.dll",
          "share/new.txt", "verifying", "commit-ready", "committed"}) {
        Fixture test(index++);
        test.stage();
        test.machine.boot = false;
        test.engine.checkpoint = [&](std::string_view point) {
            if (point == checkpoint)
                throw Crash{};
        };
        bool crashed = false;
        try {
            test.engine.complete();
        } catch (const Crash &) {
            crashed = true;
        }
        assert(crashed);
        upgrade::Engine recovery(test.root, test.machine);
        const auto result = recovery.complete(true);
        if (std::string_view(checkpoint) == "commit-ready" ||
            std::string_view(checkpoint) == "committed") {
            assert(result == 0);
            assert(test.machine.activeVersion == "2.0.0.0");
            assert(recovery.query().find("committed") != std::string::npos);
        } else {
            assert(result == ERROR_INSTALL_FAILURE);
            test.oldVersion();
        }
    }
    {
        Fixture test(30);
        test.stage();
        upgrade::writeText(test.root /
                               "setup/upgrades/current/payload/share/data.txt",
                           "tampered");
        test.machine.boot = false;
        bool rejected = false;
        try {
            test.engine.complete();
        } catch (const std::system_error &) {
            rejected = true;
        }
        assert(rejected);
        test.oldVersion();
    }
    {
        Fixture test(31);
        test.stage();
        test.machine.boot = false;
        test.machine.dependencyError = ERROR_SUCCESS_REBOOT_REQUIRED;
        assert(test.engine.complete() == ERROR_SUCCESS_REBOOT_REQUIRED);
        test.oldVersion();
        assert(test.machine.boot);
        assert(upgrade::transactionState(test.root) == "staged");
    }
    {
        Fixture test(32);
        test.stage();
        test.machine.boot = false;
        test.engine.checkpoint = [](std::string_view point) {
            if (point == "bin/Fcitx5.exe")
                throw Crash{};
        };
        try {
            test.engine.complete();
            assert(false);
        } catch (const Crash &) {
        }
        const auto backup =
            test.root / "setup/upgrades/current/backup/bin/Fcitx5.exe";
        upgrade::writeText(backup, "corrupted backup");
        upgrade::Engine recovery(test.root, test.machine);
        assert(recovery.complete(true) == ERROR_CRC);
        assert(upgrade::readText(test.root / "setup/install-state") ==
               "pending\n");
        assert(upgrade::transactionState(test.root) == "recovery-required");
        upgrade::writeText(backup, "old:bin/Fcitx5.exe");
        assert(recovery.complete(true) == ERROR_INSTALL_FAILURE);
        test.oldVersion();
    }
    {
        Fixture test(33);
        fs::create_directories(test.root / "setup/upgrades/current");
        assert(test.engine.cancel() == 0);
        assert(!upgrade::hasTransaction(test.root));
        test.oldVersion();
    }
    {
        Fixture test(34);
        auto old = upgrade::readText(test.root / "setup/runtime-manifest.json");
        Tree value;
        std::istringstream stream(old);
        boost::property_tree::read_json(stream, value);
        value.put("protocolVersion", fcitx::win32::ipc::kVersion - 1);
        std::ostringstream output;
        boost::property_tree::write_json(output, value);
        upgrade::writeText(test.root / "setup/runtime-manifest.json",
                           output.str());
        test.stage();
        test.machine.boot = false;
        assert(test.engine.complete() == 0);
        assert(test.machine.activeVersion == "2.0.0.0");
    }
    {
        Fixture test(35);
        test.stage();
        test.machine.boot = false;
        upgrade::writeText(test.root / "share/new.txt", "unowned file");
        assert(test.engine.complete() == ERROR_FILE_EXISTS);
        assert(upgrade::readText(test.root / "share/new.txt") ==
               "unowned file");
        assert(upgrade::readText(test.root / "bin/Fcitx5.exe") ==
               "old:bin/Fcitx5.exe");
        assert(upgrade::readText(test.root / "setup/install-state") ==
               "installed\n");
    }
    {
        Fixture test(36);
        test.stage();
        test.machine.boot = false;
        upgrade::writeText(test.root / "bin/Fcitx5.exe",
                           "another installer changed it");
        assert(test.engine.complete() == ERROR_REVISION_MISMATCH);
        assert(upgrade::readText(test.root / "bin/Fcitx5.exe") ==
               "another installer changed it");
        assert(upgrade::readText(test.root / "setup/install-state") ==
               "installed\n");
    }
    {
        Fixture test(37);
        test.stage();
        test.engine.checkpoint = [](std::string_view point) {
            if (point == "cancelling")
                throw Crash{};
        };
        try {
            test.engine.cancel();
            assert(false);
        } catch (const Crash &) {
        }
        upgrade::Engine recovery(test.root, test.machine);
        assert(recovery.complete(true) == 0);
        assert(!test.machine.armed && !test.machine.routed);
        test.oldVersion();
    }
    {
        Fixture test(38);
        test.stage();
        test.machine.boot = false;
        test.machine.taskRemovalError = true;
        bool failed = false;
        try {
            test.engine.complete();
        } catch (const std::system_error &) {
            failed = true;
        }
        assert(failed);
        assert(upgrade::transactionState(test.root) == "committed");
        assert(test.machine.activeVersion == "2.0.0.0");
        assert(upgrade::readText(test.root / "setup/install-state") ==
               "installed\n");
        test.machine.taskRemovalError = false;
        upgrade::Engine recovery(test.root, test.machine);
        assert(recovery.complete(true) == 0);
        assert(!upgrade::hasTransaction(test.root) && !test.machine.armed);
    }
    {
        Fixture test(39);
        test.stage();
        test.machine.boot = false;
        const auto path = test.root / "bin/Fcitx5.exe";
        upgrade::Handle file(CreateFileW(path.c_str(), GENERIC_READ,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         nullptr, OPEN_EXISTING, 0, nullptr));
        assert(file);
        upgrade::Handle mapping(CreateFileMappingW(
            file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
        assert(mapping);
        const auto view = MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0);
        assert(view);
        file = upgrade::Handle();
        try {
            test.engine.complete();
        } catch (const std::system_error &) {
            assert(upgrade::transactionState(test.root) == "committed");
        }
        assert(memcmp(view, "old:bin/Fcitx5.exe", 18) == 0);
        assert(test.machine.activeVersion == "2.0.0.0");
        UnmapViewOfFile(view);
        mapping = upgrade::Handle();
        assert(test.engine.complete(true) == 0);
        assert(!upgrade::hasTransaction(test.root));
    }
    {
        Fixture test(40);
        fs::remove(test.root / "tsf/x86/fcitx5-x86.dll");
        std::vector<std::string> legacy;
        for (const auto &path : test.paths)
            if (path != "tsf/x86/fcitx5-x86.dll" && path != "share/new.txt")
                legacy.push_back(path);
        legacy.push_back("share/obsolete.txt");
        saveManifest(test.root, legacy, "1.0.0.0");
        test.stage();
        test.machine.boot = false;
        assert(test.engine.complete() == 0);
        assert(upgrade::readText(test.root / "tsf/x86/fcitx5-x86.dll") ==
               "new:tsf/x86/fcitx5-x86.dll");
    }
    {
        Fixture test(41);
        auto single = test.paths;
        std::erase(single, "tsf/x86/fcitx5-x86.dll");
        saveManifest(test.source, single, "2.0.0.0");
        bool rejected = false;
        try {
            test.engine.begin(test.source / "setup/runtime-manifest.json");
        } catch (const std::system_error &error) {
            rejected = error.code().value() == ERROR_REVISION_MISMATCH;
        }
        assert(rejected && !upgrade::hasTransaction(test.root));
        test.oldVersion();
    }
    std::cout << "PASS: stage, same-boot refusal, dual-architecture lock "
                 "deferral, duplicate, "
                 "cancel, commit, rollback and durable crash recovery\n";
}
