#pragma once

#include "upgrade_files.h"
#include <functional>
#include <memory>
#include <optional>

namespace fcitx::setup::upgrade {

struct File {
    std::string path;
    uint64_t size = 0;
    std::string digest;
};
struct Manifest {
    std::string version;
    std::string package;
    uint16_t protocolVersion = 0;
    std::vector<File> files;
    std::vector<File> dependencies;
};
Manifest manifest(const fs::path &path);
void verify(const fs::path &root, const std::vector<File> &files);
bool hasTransaction(const fs::path &root);
bool hasMaintenance(const fs::path &root);
std::string transactionState(const fs::path &root);
bool busyState(std::string_view state);

// OS mutations live behind this boundary; engine tests use an isolated fake.
class Machine {
  public:
    virtual ~Machine() = default;
    virtual void validate() = 0;
    virtual void gate(std::string_view state) = 0;
    virtual bool sameBoot() = 0;
    virtual void markBoot(bool present) = 0;
    virtual void task(bool enable, std::string_view id) = 0;
    virtual void controls(bool install, const fs::path &transaction) = 0;
    virtual void snapshot(const fs::path &transaction) = 0;
    virtual void restore(const fs::path &transaction) = 0;
    virtual void registerVersion(const Manifest &value) = 0;
    virtual void verifyVersion(const Manifest &value) = 0;
    virtual DWORD dependencies(const fs::path &transaction,
                               const Manifest &value) = 0;
};

class Engine {
  public:
    Engine(fs::path root, Machine &machine,
           DWORD owner = GetCurrentProcessId());
    DWORD begin(const fs::path &manifestPath);
    DWORD seal();
    DWORD cancel();
    DWORD complete(bool recover = false);
    DWORD cleanup();
    std::string query();
    // Runs after durable writes. Tests throw here to simulate process loss.
    std::function<void(std::string_view)> checkpoint;

  private:
    void state(std::string_view value, DWORD error = 0);
    void point(std::string_view value);
    DWORD rollback(DWORD error);
    void result(std::string_view value, DWORD error);
    fs::path root_;
    fs::path transaction_;
    Machine &machine_;
    DWORD owner_;
};

std::unique_ptr<Machine> nativeMachine(const fs::path &root);
bool upgradeCommand(std::wstring_view command);
DWORD runUpgrade(const fs::path &root, std::wstring_view command,
                 const fs::path &source, std::string_view transaction,
                 bool preview, bool silent);
std::vector<fs::path> ownedFiles(const fs::path &root);
DWORD removeOwnedFiles(const fs::path &root,
                       const std::vector<fs::path> &files);

} // namespace fcitx::setup::upgrade
