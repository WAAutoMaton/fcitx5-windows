#include "upgrade.h"
#include "../ipc/installation.h"
#include <algorithm>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <map>
#include <objbase.h>
#include <set>
#include <sstream>

namespace fcitx::setup::upgrade {
namespace {
using Tree = boost::property_tree::ptree;
constexpr uint64_t kMaxBytes = 16ull * 1024 * 1024 * 1024;
constexpr size_t kMaxFiles = 32768;

void uniqueKeys(const Tree &tree) {
    std::set<std::string> keys;
    for (const auto &[key, value] : tree) {
        if (!key.empty() && !keys.insert(key).second)
            fail(ERROR_INVALID_DATA);
        uniqueKeys(value);
    }
}
Tree json(const fs::path &path) {
    const auto text = readText(path);
    Tree tree;
    std::istringstream stream(text);
    try {
        boost::property_tree::read_json(stream, tree);
    } catch (const boost::property_tree::ptree_error &) {
        fail(ERROR_INVALID_DATA);
    }
    uniqueKeys(tree);
    return tree;
}
void save(const fs::path &path, const Tree &tree) {
    std::ostringstream stream;
    boost::property_tree::write_json(stream, tree, false);
    writeText(path, stream.str());
}
struct InsensitiveLess {
    bool operator()(const std::wstring &left, const std::wstring &right) const {
        return CompareStringOrdinal(left.c_str(), int(left.size()),
                                    right.c_str(), int(right.size()),
                                    TRUE) == CSTR_LESS_THAN;
    }
};
std::vector<File> files(const Tree &array, bool dependency) {
    std::vector<File> result;
    uint64_t bytes = 0;
    std::set<std::wstring, InsensitiveLess> paths;
    for (const auto &[key, node] : array) {
        File value{node.get<std::string>("path"), node.get<uint64_t>("size"),
                   node.get<std::string>("sha256")};
        const auto path = relativePath(value.path);
        if (!key.empty() || (!dependency && !runtimePath(path)) ||
            (dependency && value.path != "vc_redist.x64.exe" &&
             value.path != "WindowsAppRuntimeInstall-x64.exe") ||
            value.digest.size() != 64 ||
            !std::all_of(value.digest.begin(), value.digest.end(),
                         [](char c) {
                             return (c >= '0' && c <= '9') ||
                                    (c >= 'a' && c <= 'f');
                         }) ||
            value.size > kMaxBytes || bytes > kMaxBytes - value.size ||
            !paths.insert(wide(value.path)).second ||
            result.size() >= kMaxFiles)
            fail(ERROR_INVALID_DATA);
        bytes += value.size;
        result.push_back(std::move(value));
    }
    // A file cannot simultaneously be a parent directory of another file.
    for (const auto &name : paths) {
        auto parent = fs::path(name).parent_path();
        while (!parent.empty()) {
            if (paths.contains(parent.generic_wstring()))
                fail(ERROR_INVALID_DATA);
            parent = parent.parent_path();
        }
    }
    return result;
}
bool version(std::string_view value) {
    unsigned parts = 0;
    std::istringstream stream{std::string(value)};
    std::string part;
    while (std::getline(stream, part, '.')) {
        if (part.empty() || part.size() > 5 ||
            !std::all_of(part.begin(), part.end(),
                         [](char c) { return c >= '0' && c <= '9'; }) ||
            std::stoul(part) > 65535)
            return false;
        ++parts;
    }
    return parts == 4 && !value.ends_with('.');
}
fs::path transactionPath(const fs::path &root) {
    return root / "setup" / "upgrades" / "current";
}
std::string stateAt(const fs::path &transaction) {
    if (!fs::exists(transaction / "journal.json"))
        return "preparing";
    return json(transaction / "journal.json").get<std::string>("state");
}
std::string idAt(const fs::path &transaction) {
    if (!fs::exists(transaction / "plan.json"))
        return {};
    const auto id = json(transaction / "plan.json").get<std::string>("id");
    GUID guid{};
    if (id.size() != 38 || FAILED(CLSIDFromString(wide(id).c_str(), &guid)))
        fail(ERROR_INVALID_DATA);
    return id;
}
void removeTree(const fs::path &directory) {
    if (!fs::exists(directory))
        return;
    validatePath(directory);
    // Never recurse through a link, including links injected into a payload.
    for (const auto &entry : fs::recursive_directory_iterator(directory))
        validatePath(entry.path());
    fs::remove_all(directory);
}
void validateTree(const fs::path &directory) {
    validatePath(directory);
    for (const auto &entry : fs::recursive_directory_iterator(directory))
        validatePath(entry.path());
}
void reapCompleted(const fs::path &root) {
    const auto directory = root / "setup" / "upgrades";
    if (!fs::exists(directory))
        return;
    validatePath(directory);
    for (const auto &entry : fs::directory_iterator(directory)) {
        const auto name = entry.path().filename().wstring();
        constexpr std::wstring_view prefix = L"completed-";
        if (!name.starts_with(prefix))
            continue;
        const auto suffix = name.substr(prefix.size());
        GUID id{};
        if (suffix.size() == 38 &&
            SUCCEEDED(CLSIDFromString(suffix.c_str(), &id)))
            removeTree(entry.path());
    }
}
Tree inventory(const fs::path &root, const Manifest &target) {
    std::map<std::wstring, std::string, InsensitiveLess> paths;
    for (const auto &file : target.files)
        paths.emplace(wide(file.path), file.path);
    if (fs::exists(root / "setup" / "runtime-manifest.json")) {
        std::set<std::wstring, InsensitiveLess> owned;
        for (const auto &file :
             manifest(root / "setup" / "runtime-manifest.json").files) {
            owned.insert(wide(file.path));
            paths.emplace(wide(file.path), file.path);
        }
        for (const auto &file : target.files) {
            if (!owned.contains(wide(file.path)) &&
                fs::exists(root / relativePath(file.path)))
                fail(ERROR_FILE_EXISTS);
        }
    }
    Tree result;
    for (const auto &[key, path] : paths) {
        Tree node;
        node.put("path", path);
        node.put("existed", fs::exists(root / relativePath(path)));
        result.push_back({"", node});
    }
    return result;
}
void checkSpace(const fs::path &root, const Manifest &value) {
    uint64_t required = 64 * 1024 * 1024;
    for (const auto &file : value.files)
        required += file.size * 3;
    ULARGE_INTEGER available{};
    if (!GetDiskFreeSpaceExW(root.c_str(), &available, nullptr, nullptr))
        fail();
    if (available.QuadPart < required)
        fail(ERROR_DISK_FULL);
}
uint64_t processCreation(DWORD pid) {
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                               FALSE, pid));
    if (!process) {
        if (GetLastError() == ERROR_INVALID_PARAMETER)
            return 0;
        fail();
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process.get(), &created, &exited, &kernel, &user))
        fail();
    const auto waiting = WaitForSingleObject(process.get(), 0);
    if (waiting == WAIT_OBJECT_0)
        return 0;
    if (waiting != WAIT_TIMEOUT)
        fail();
    return (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
void requireOwner(const fs::path &transaction, DWORD owner) {
    if (!fs::exists(transaction / "plan.json"))
        return;
    const auto plan = json(transaction / "plan.json");
    const auto pid = plan.get<DWORD>("ownerPid");
    const auto created = processCreation(pid);
    if (created && created == plan.get<uint64_t>("ownerCreation") &&
        pid != owner)
        fail(ERROR_BUSY);
}
} // namespace

Manifest manifest(const fs::path &path) try {
    const auto tree = json(path);
    Manifest result;
    if (tree.get<unsigned>("schemaVersion") != 1 ||
        tree.get<std::string>("architecture") != "x64")
        fail(ERROR_REVISION_MISMATCH);
    const auto protocol = tree.get<unsigned>("protocolVersion");
    if (!protocol || protocol > UINT16_MAX)
        fail(ERROR_INVALID_DATA);
    result.protocolVersion = uint16_t(protocol);
    result.version = tree.get<std::string>("version");
    result.package = tree.get<std::string>("package");
    result.files = files(tree.get_child("files"), false);
    result.dependencies = files(tree.get_child("dependencies"), true);
    if (!version(result.version) || result.package.empty() ||
        result.package.size() > 128 || result.files.empty())
        fail(ERROR_INVALID_DATA);
    for (const auto required :
         {"bin/Fcitx5.exe", "tsf/fcitx5-x86_64.dll",
          "setup/Fcitx5SetupHelper.exe", "setup/managed-install",
          "settings/Fcitx5Settings.exe"}) {
        if (std::none_of(
                result.files.begin(), result.files.end(),
                [&](const File &file) { return file.path == required; }))
            fail(ERROR_INVALID_DATA);
    }
    return result;
} catch (const boost::property_tree::ptree_error &) {
    fail(ERROR_INVALID_DATA);
}

void verify(const fs::path &root, const std::vector<File> &list) {
    for (const auto &file : list) {
        const auto path = root / relativePath(file.path);
        if (fs::file_size(path) != file.size || sha256(path) != file.digest)
            fail(ERROR_CRC);
    }
}
bool hasTransaction(const fs::path &root) {
    return fs::exists(transactionPath(root));
}
bool hasMaintenance(const fs::path &root) {
    return fs::exists(root / "setup" / "maintenance-host" /
                      "Fcitx5Upgrade.exe");
}
std::string transactionState(const fs::path &root) {
    return hasTransaction(root) ? stateAt(transactionPath(root)) : "none";
}
bool busyState(std::string_view state) {
    return state != "none" && state != "staged" && state != "cancelling" &&
           state != "committed" && state != "rolled-back";
}

Engine::Engine(fs::path root, Machine &machine, DWORD owner)
    : root_(std::move(root)), transaction_(transactionPath(root_)),
      machine_(machine), owner_(owner) {}

void Engine::point(std::string_view value) {
    if (checkpoint)
        checkpoint(value);
}
void Engine::state(std::string_view value, DWORD error) {
    Tree journal;
    journal.put("schemaVersion", 1);
    journal.put("state", std::string(value));
    journal.put("error", error);
    save(transaction_ / "journal.json", journal);
    point(value);
}
void Engine::result(std::string_view value, DWORD error) {
    Tree result;
    result.put("schemaVersion", 1);
    result.put("state", std::string(value));
    result.put("error", error);
    result.put("transactionId", idAt(transaction_));
    result.put("targetVersion",
               fs::exists(transaction_ / "target-manifest.json")
                   ? manifest(transaction_ / "target-manifest.json").version
                   : "unknown");
    save(root_ / "setup" / "upgrade-result.json", result);
}

DWORD Engine::begin(const fs::path &manifestPath) {
    machine_.validate();
    reapCompleted(root_);
    const auto target = manifest(manifestPath);
    if (target.protocolVersion != win32::ipc::kVersion)
        fail(ERROR_REVISION_MISMATCH);
    checkSpace(root_, target);
    if (hasTransaction(root_) && (stateAt(transaction_) == "committed" ||
                                  stateAt(transaction_) == "rolled-back"))
        cleanup();
    if (hasTransaction(root_)) {
        const auto current = stateAt(transaction_);
        if (current == "staged" &&
            sha256(manifestPath) ==
                sha256(transaction_ / "target-manifest.json"))
            return ERROR_SUCCESS_REBOOT_REQUIRED;
        fail(ERROR_INSTALL_ALREADY_RUNNING);
    }
    fs::create_directories(transaction_ / "payload");
    fs::create_directories(transaction_ / "dependencies");
    writeText(transaction_ / "target-manifest.json", readText(manifestPath));
    GUID id{};
    if (FAILED(CoCreateGuid(&id)))
        fail(ERROR_GEN_FAILURE);
    wchar_t text[40]{};
    StringFromGUID2(id, text, 40);
    Tree plan;
    plan.put("schemaVersion", 1);
    plan.put("id", utf8(text));
    plan.put("manifestHash", sha256(transaction_ / "target-manifest.json"));
    plan.put("ownerPid", owner_);
    plan.put("ownerCreation", processCreation(owner_));
    Tree binaries;
    for (auto path :
         {"bin/Fcitx5.exe", "tsf/fcitx5-x86_64.dll",
          "setup/Fcitx5SetupHelper.exe", "settings/Fcitx5Settings.exe"}) {
        Tree file;
        file.put("path", path);
        file.put("sha256", sha256(root_ / path));
        binaries.push_back({"", file});
    }
    plan.add_child("sourceBinaries", binaries);
    save(transaction_ / "plan.json", plan);
    state("preparing");
    return ERROR_SUCCESS;
}

DWORD Engine::seal() {
    machine_.validate();
    if (stateAt(transaction_) != "preparing")
        fail(ERROR_INVALID_STATE);
    requireOwner(transaction_, owner_);
    const auto target = manifest(transaction_ / "target-manifest.json");
    verify(transaction_ / "payload", target.files);
    verify(transaction_ / "dependencies", target.dependencies);
    validateTree(transaction_);
    // Unknown files are not executable SYSTEM payload or implicit ownership.
    std::set<fs::path> expected;
    for (const auto &file : target.files)
        expected.insert(relativePath(file.path));
    for (const auto &entry :
         fs::recursive_directory_iterator(transaction_ / "payload")) {
        if (entry.is_regular_file() &&
            !expected.contains(
                entry.path().lexically_relative(transaction_ / "payload")))
            fail(ERROR_INVALID_DATA);
    }
    machine_.markBoot(true);
    machine_.controls(true, transaction_);
    machine_.task(true, idAt(transaction_));
    state("staged");
    result("staged", ERROR_SUCCESS_REBOOT_REQUIRED);
    return ERROR_SUCCESS_REBOOT_REQUIRED;
}

DWORD Engine::cancel() {
    machine_.validate();
    if (!hasTransaction(root_))
        return cleanup();
    const auto current = stateAt(transaction_);
    if (current == "committed" || current == "rolled-back")
        return cleanup();
    if (current == "preparing")
        requireOwner(transaction_, owner_);
    else if (busyState(current))
        fail(ERROR_BUSY);
    state("cancelling");
    machine_.task(false, idAt(transaction_));
    machine_.markBoot(false);
    // Retain a supported uninstall route until the transaction has disappeared.
    result("cancelled", ERROR_CANCELLED);
    machine_.controls(false, transaction_);
    removeTree(transaction_);
    return ERROR_SUCCESS;
}

DWORD Engine::complete(bool /*recover*/) {
    machine_.validate();
    try {
        if (!hasTransaction(root_))
            return cleanup();
        const auto current = stateAt(transaction_);
        if (current == "cancelling")
            return cancel();
        if (current == "committed" || current == "rolled-back") {
            result(current,
                   json(transaction_ / "journal.json").get<DWORD>("error"));
            return cleanup();
        }
        if (current == "preparing")
            fail(ERROR_INVALID_STATE);
        if (readText(transaction_ / "target-manifest.json").empty() ||
            json(transaction_ / "plan.json").get<std::string>("manifestHash") !=
                sha256(transaction_ / "target-manifest.json"))
            fail(ERROR_CRC);
        if (current != "staged" && current != "validating") {
            // A durable commit record precedes publication of installed. Finish
            // forward after a crash in that window; do not roll back running
            // code.
            if (fs::exists(transaction_ / "commit-ready") &&
                readText(transaction_ / "commit-ready", 128) ==
                    "FCITX5-UPGRADE-COMMIT-1\n") {
                const auto target =
                    manifest(transaction_ / "target-manifest.json");
                verify(root_, target.files);
                machine_.verifyVersion(target);
                machine_.gate("installed");
                state("committed");
                result("committed", ERROR_SUCCESS);
                return cleanup();
            }
            return rollback(ERROR_INSTALL_FAILURE);
        }
        if (machine_.sameBoot())
            return ERROR_SUCCESS_REBOOT_REQUIRED;
        const auto target = manifest(transaction_ / "target-manifest.json");
        const auto plan = json(transaction_ / "plan.json");
        if (plan.get<std::string>("manifestHash") !=
            sha256(transaction_ / "target-manifest.json"))
            fail(ERROR_CRC);
        verify(transaction_ / "payload", target.files);
        verify(transaction_ / "dependencies", target.dependencies);
        machine_.gate("maintenance");
        state("validating");
        Tree entries;
        std::vector<std::pair<std::string, FileGuard>> guards;
        try {
            entries = inventory(root_, target);
            for (const auto &[key, node] : entries) {
                if (node.get<bool>("existed")) {
                    const auto path = node.get<std::string>("path");
                    guards.emplace_back(
                        path, FileGuard(root_ / relativePath(path), false,
                                        transaction_ / "retired"));
                }
            }
            for (const auto &[unused, file] :
                 plan.get_child("sourceBinaries")) {
                const auto path = file.get<std::string>("path");
                const auto guard = std::find_if(
                    guards.begin(), guards.end(),
                    [&](const auto &entry) { return entry.first == path; });
                if (guard == guards.end() ||
                    sha256(guard->second.get()) !=
                        file.get<std::string>("sha256"))
                    fail(ERROR_REVISION_MISMATCH);
            }
        } catch (const std::system_error &error) {
            guards.clear();
            machine_.gate("installed");
            machine_.markBoot(true);
            state("staged", DWORD(error.code().value()));
            result("staged", DWORD(error.code().value()));
            const auto code = DWORD(error.code().value());
            return code == ERROR_SHARING_VIOLATION ||
                           code == ERROR_LOCK_VIOLATION ||
                           code == ERROR_USER_MAPPED_FILE
                       ? ERROR_SUCCESS_REBOOT_REQUIRED
                       : code;
        }
        try {
            const auto dependency = machine_.dependencies(transaction_, target);
            if (dependency) {
                guards.clear();
                machine_.gate("installed");
                machine_.markBoot(true);
                state("staged", dependency);
                result("staged", dependency);
                return dependency;
            }
            // No active file changes before the entire backup and inventory
            // exist.
            removeTree(transaction_ / "backup");
            fs::create_directories(transaction_ / "backup");
            for (auto &[path, guard] : guards)
                guard.backup(transaction_ / "backup" / relativePath(path));
            for (auto &[unused, node] : entries) {
                if (node.get<bool>("existed"))
                    node.put(
                        "backupHash",
                        sha256(transaction_ / "backup" /
                               relativePath(node.get<std::string>("path"))));
            }
            const auto activeManifest =
                root_ / "setup" / "runtime-manifest.json";
            if (fs::exists(activeManifest))
                writeText(transaction_ / "old-manifest.json",
                          readText(activeManifest));
            machine_.snapshot(transaction_);
            Tree snapshot;
            snapshot.add_child("files", entries);
            save(transaction_ / "inventory.json", snapshot);
            state("applying");
            for (const auto &file : target.files) {
                auto found = std::find_if(guards.begin(), guards.end(),
                                          [&](const auto &entry) {
                                              return entry.first == file.path;
                                          });
                if (found == guards.end()) {
                    const auto path = root_ / relativePath(file.path);
                    validatePath(path.parent_path(), true);
                    fs::create_directories(path.parent_path());
                    guards.emplace_back(
                        file.path,
                        FileGuard(path, true, transaction_ / "retired"));
                    found = std::prev(guards.end());
                }
                found->second.replace(transaction_ / "payload" /
                                      relativePath(file.path));
                if (sha256(found->second.get()) != file.digest)
                    fail(ERROR_CRC);
                point(file.path);
            }
            for (auto &[path, guard] : guards) {
                if (std::none_of(
                        target.files.begin(), target.files.end(),
                        [&](const auto &file) { return file.path == path; }))
                    guard.remove();
            }
            state("verifying");
            guards.clear();
            verify(root_, target.files);
            machine_.registerVersion(target);
            writeText(activeManifest,
                      readText(transaction_ / "target-manifest.json"));
            writeText(transaction_ / "commit-ready",
                      "FCITX5-UPGRADE-COMMIT-1\n");
            point("commit-ready");
            machine_.gate("installed");
            state("committed");
            result("committed", ERROR_SUCCESS);
        } catch (const std::system_error &error) {
            guards.clear();
            if (fs::exists(transaction_ / "commit-ready"))
                throw;
            if (stateAt(transaction_) == "validating") {
                machine_.gate("installed");
                machine_.markBoot(true);
                state("staged", DWORD(error.code().value()));
                result("staged", DWORD(error.code().value()));
                return DWORD(error.code().value());
            }
            return rollback(DWORD(error.code().value()));
        }
        return cleanup();
    } catch (const std::system_error &error) {
        if (hasTransaction(root_)) {
            const auto current = stateAt(transaction_);
            state(current, DWORD(error.code().value()));
            result(current, DWORD(error.code().value()));
            if (current == "staged")
                machine_.markBoot(true);
        }
        throw;
    }
}

DWORD Engine::rollback(DWORD error) {
    machine_.gate("maintenance");
    try {
        state("rolling-back", error);
        const auto entries =
            json(transaction_ / "inventory.json").get_child("files");
        std::vector<FileGuard> guards;
        for (const auto &[key, node] : entries) {
            const auto path =
                root_ / relativePath(node.get<std::string>("path"));
            if (node.get<bool>("existed")) {
                if (sha256(transaction_ / "backup" /
                           relativePath(node.get<std::string>("path"))) !=
                    node.get<std::string>("backupHash"))
                    fail(ERROR_CRC);
                guards.emplace_back(path, !fs::exists(path),
                                    transaction_ / "retired");
            } else if (fs::exists(path)) {
                guards.emplace_back(path, false, transaction_ / "retired");
            }
        }
        size_t index = 0;
        for (const auto &[key, node] : entries) {
            const auto relative = relativePath(node.get<std::string>("path"));
            if (node.get<bool>("existed")) {
                guards[index++].replace(transaction_ / "backup" / relative,
                                        true);
            } else if (fs::exists(root_ / relative)) {
                guards[index++].remove();
            }
        }
        guards.clear();
        machine_.restore(transaction_);
        const auto activeManifest = root_ / "setup" / "runtime-manifest.json";
        if (fs::exists(transaction_ / "old-manifest.json"))
            writeText(activeManifest,
                      readText(transaction_ / "old-manifest.json"));
        else
            fs::remove(activeManifest);
        machine_.gate("installed");
        state("rolled-back", error);
        result("rolled-back", error);
        cleanup();
        return error;
    } catch (const std::system_error &failure) {
        machine_.gate("pending");
        state("recovery-required", DWORD(failure.code().value()));
        result("recovery-required", DWORD(failure.code().value()));
        return DWORD(failure.code().value());
    }
}

DWORD Engine::cleanup() {
    reapCompleted(root_);
    if (!hasTransaction(root_)) {
        const auto saved = root_ / "setup" / "upgrade-result.json";
        if (fs::exists(saved)) {
            const auto id = json(saved).get<std::string>("transactionId", "");
            if (!id.empty()) {
                machine_.task(false, id);
                machine_.markBoot(false);
            }
        }
        return ERROR_SUCCESS;
    }
    const auto current = stateAt(transaction_);
    if (current != "committed" && current != "rolled-back")
        fail(ERROR_BUSY);
    const auto id = idAt(transaction_);
    result(current, json(transaction_ / "journal.json").get<DWORD>("error", 0));
    // Keep the terminal journal and task until potentially mapped old files
    // have gone. A failed cleanup must remain recognizable on the next boot.
    for (auto directory : {"payload", "backup", "dependencies", "retired"})
        removeTree(transaction_ / directory);
    if (current == "committed") {
        state("committed");
        result("committed", ERROR_SUCCESS);
    }
    machine_.task(false, id);
    machine_.markBoot(false);
    const auto garbage = transaction_.parent_path() / ("completed-" + id);
    if (!MoveFileExW(transaction_.c_str(), garbage.c_str(),
                     MOVEFILE_WRITE_THROUGH))
        fail();
    removeTree(garbage);
    return ERROR_SUCCESS;
}
std::string Engine::query() {
    if (hasTransaction(root_)) {
        Tree result;
        result.put("state", stateAt(transaction_));
        if (fs::exists(transaction_ / "journal.json"))
            result.put(
                "error",
                json(transaction_ / "journal.json").get<DWORD>("error", 0));
        if (fs::exists(transaction_ / "target-manifest.json"))
            result.put("targetVersion",
                       manifest(transaction_ / "target-manifest.json").version);
        std::ostringstream stream;
        boost::property_tree::write_json(stream, result, false);
        return stream.str();
    }
    const auto path = root_ / "setup" / "upgrade-result.json";
    return fs::exists(path) ? readText(path) : "{\"state\":\"none\"}\n";
}

std::vector<fs::path> ownedFiles(const fs::path &root) {
    std::vector<fs::path> result;
    const auto path = root / "setup" / "runtime-manifest.json";
    if (fs::exists(path)) {
        for (const auto &file : manifest(path).files)
            result.push_back(relativePath(file.path));
    }
    if (hasMaintenance(root)) {
        for (const auto name :
             {"setup/runtime-manifest.json", "setup/upgrade-result.json",
              "setup/maintenance-host/Fcitx5Upgrade.exe",
              "setup/maintenance.lock"})
            result.push_back(relativePath(name));
    }
    return result;
}
DWORD removeOwnedFiles(const fs::path &root,
                       const std::vector<fs::path> &files) {
    DWORD result = ERROR_SUCCESS;
    std::set<fs::path> directories;
    for (const auto &relative : files) {
        const auto path = root / relative;
        validatePath(path, true);
        if (fs::exists(path) && !DeleteFileW(path.c_str())) {
            const auto error = GetLastError();
            if ((error == ERROR_SHARING_VIOLATION ||
                 error == ERROR_ACCESS_DENIED) &&
                MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
                result = ERROR_SUCCESS_REBOOT_REQUIRED;
            else
                fail(error);
        }
        for (auto parent = path.parent_path();
             parent != root && !parent.empty(); parent = parent.parent_path())
            directories.insert(parent);
    }
    RemoveDirectoryW((root / "setup" / "upgrades").c_str());
    for (auto it = directories.rbegin(); it != directories.rend(); ++it)
        RemoveDirectoryW(it->c_str());
    RemoveDirectoryW(root.c_str());
    return result;
}
} // namespace fcitx::setup::upgrade
