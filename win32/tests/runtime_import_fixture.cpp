#include <mutex>

extern "C" __declspec(dllexport) void runtime_import_fixture() {
    static std::mutex mutex;
    std::lock_guard lock(mutex);
}
