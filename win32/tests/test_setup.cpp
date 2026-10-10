#include "../dll/util.h"
#include "../ipc/atomicfile.h"
#include "../ipc/installation.h"
#include "../setup/userprofile.h"
#include <cassert>
#include <iostream>

namespace fs = std::filesystem;
namespace ipc = fcitx::win32::ipc;
namespace setup = fcitx::setup;

int main() {
    const auto root =
        fs::temp_directory_path() /
        (L"fcitx5-installation-test-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(root / "setup");
    ipc::InstallationGate development(root);
    assert(development.allowsStart());
    assert(ipc::replaceFile(root / "setup" / "managed-install",
                            ipc::kManagedInstall));
    ipc::InstallationGate installed(root);
    assert(!installed.allowsStart());
    assert(ipc::replaceFile(root / "setup" / "install-state", "installed\n"));
    assert(installed.allowsStart());
    assert(ipc::replaceFile(root / "setup" / "install-state", "maintenance\n"));
    assert(!installed.allowsStart());
    assert(ipc::replaceFile(root / "setup" / "install-state", "pending\n"));
    assert(!installed.allowsStart());
    assert(
        ipc::replaceFile(root / "setup" / "install-state", "installed\nextra"));
    assert(!installed.allowsStart());
    fs::remove(root / "setup" / "managed-install");
    fs::remove(root / "setup" / "install-state");
    assert(!installed.allowsStart());
    fs::remove_all(root);

    setup::InputProfile fcitx;
    fcitx.type = 1;
    fcitx.language = TEXTSERVICE_LANGID_HANS;
    fcitx.clsid = fcitx::FCITX_CLSID;
    fcitx.profile = fcitx::PROFILE_GUID;
    fcitx.flags = setup::kDefaultProfile;
    assert(setup::isFcitx(fcitx));
    assert(setup::profileId(fcitx) ==
           L"0x0804:{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}"
           L"{9A92B895-29B9-4F19-9627-9F626C9490F2};");
    setup::InputProfile english;
    english.type = 2;
    english.language = 0x0409;
    wcscpy_s(english.id, L"00000409");
    assert(setup::profileId(english) == L"0x0409:0x00000409");
    setup::InputProfile other = english;
    other.language = 0x0407;
    wcscpy_s(other.id, L"00000407");
    const std::vector profiles{fcitx, other, english};
    assert(setup::defaultProfile(profiles) == &profiles[0]);
    assert(setup::restoreProfile(profiles, english) == &profiles[2]);
    const std::vector missing{fcitx, other};
    assert(setup::restoreProfile(missing, english) == &missing[1]);
    other.flags = setup::kDisabledProfile;
    const std::vector disabled{fcitx, other};
    assert(!setup::restoreProfile(disabled, english));
    english.flags = setup::kDefaultProfile;
    fcitx.flags = 0;
    const std::vector changed{fcitx, english};
    assert(!setup::isFcitx(*setup::defaultProfile(changed)));
    wcscpy_s(english.id, L"bad-id");
    assert(setup::profileId(english).empty());
    std::cout << "Installation gate and input-profile restoration passed.\n";
}
