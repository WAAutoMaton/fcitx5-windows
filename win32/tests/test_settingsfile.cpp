#include "../ipc/atomicfile.h"
#include <cassert>
#include <fstream>
#include <iterator>

int main() {
    using fcitx::win32::ipc::replaceFile;
    const auto directory =
        std::filesystem::temp_directory_path() /
        (L"fcitx-settings-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto path = directory / "settings.conf";
    assert(replaceFile(path, "old settings"));
    assert(replaceFile(path, "new settings"));
    auto contents = [&]() {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    assert(contents() == "new settings");
    assert(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY));
    assert(!replaceFile(path, "lost settings"));
    assert(contents() == "new settings");
    assert(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL));
    assert(std::distance(std::filesystem::directory_iterator(directory),
                         std::filesystem::directory_iterator{}) == 1);
    assert(std::filesystem::remove(path));
    assert(std::filesystem::remove(directory));
}
