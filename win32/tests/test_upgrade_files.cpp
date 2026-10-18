#include "../setup/upgrade_files.h"
#include <cassert>
#include <iostream>
#include <sddl.h>

namespace upgrade = fcitx::setup::upgrade;
namespace fs = std::filesystem;

template <typename F> void rejected(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::system_error &) {
        failed = true;
    }
    assert(failed);
}

int wmain(int argc, wchar_t **argv) try {
    if (argc == 5 && std::wstring_view(argv[1]) == L"--child") {
        const auto module = LoadLibraryW(fs::path(argv[2]).c_str());
        if (!module)
            return 1;
        const auto ready = reinterpret_cast<HANDLE>(std::stoull(argv[3]));
        const auto stop = reinterpret_cast<HANDLE>(std::stoull(argv[4]));
        SetEvent(ready);
        WaitForSingleObject(stop, 30000);
        FreeLibrary(module);
        return 0;
    }
    assert(argc == 2);
    assert(!upgrade::detail::trustedWritePrincipal(nullptr));
    PSID trustedInstallerSid = nullptr;
    assert(ConvertStringSidToSidW(
        L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464",
        &trustedInstallerSid));
    assert(upgrade::detail::trustedWritePrincipal(trustedInstallerSid));
    LocalFree(trustedInstallerSid);

    BYTE systemSidBuffer[SECURITY_MAX_SID_SIZE]{};
    DWORD systemSidSize = sizeof(systemSidBuffer);
    assert(CreateWellKnownSid(WinLocalSystemSid, nullptr, systemSidBuffer,
                              &systemSidSize));
    assert(upgrade::detail::trustedWritePrincipal(systemSidBuffer));

    BYTE administratorsSidBuffer[SECURITY_MAX_SID_SIZE]{};
    DWORD administratorsSidSize = sizeof(administratorsSidBuffer);
    assert(CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr,
                              administratorsSidBuffer, &administratorsSidSize));
    assert(upgrade::detail::trustedWritePrincipal(administratorsSidBuffer));

    BYTE everyoneSidBuffer[SECURITY_MAX_SID_SIZE]{};
    DWORD everyoneSidSize = sizeof(everyoneSidBuffer);
    assert(CreateWellKnownSid(WinWorldSid, nullptr, everyoneSidBuffer,
                              &everyoneSidSize));
    assert(!upgrade::detail::trustedWritePrincipal(everyoneSidBuffer));

    const auto root =
        fs::temp_directory_path() /
        ("fcitx5-upgrade-files-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(root);
    const auto dll = root / "loaded.dll";
    fs::copy_file(argv[1], dll);
    upgrade::writeText(root / "hash.txt", "abc");
    assert(upgrade::sha256(root / "hash.txt") ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const auto original = upgrade::sha256(dll);
    auto module = LoadLibraryW(dll.c_str());
    assert(module);
    rejected([&] { upgrade::FileGuard guard(dll); });
    assert(upgrade::sha256(dll) == original);
    FreeLibrary(module);
    {
        upgrade::Handle file(CreateFileW(dll.c_str(), GENERIC_READ,
                                         FILE_SHARE_READ, nullptr,
                                         OPEN_EXISTING, 0, nullptr));
        assert(file);
        upgrade::Handle image(CreateFileMappingW(
            file.get(), nullptr, PAGE_READONLY | SEC_IMAGE, 0, 0, nullptr));
        assert(image);
        file = upgrade::Handle();
        rejected([&] { upgrade::FileGuard guard(dll); });
    }
    {
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
        upgrade::Handle ready(CreateEventW(&attributes, TRUE, FALSE, nullptr));
        upgrade::Handle stop(CreateEventW(&attributes, TRUE, FALSE, nullptr));
        const auto executable = fs::absolute(argv[0]);
        auto command =
            L"\"" + executable.wstring() + L"\" --child \"" + dll.wstring() +
            L"\" " + std::to_wstring(reinterpret_cast<uintptr_t>(ready.get())) +
            L" " + std::to_wstring(reinterpret_cast<uintptr_t>(stop.get()));
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION info{};
        assert(CreateProcessW(executable.c_str(), command.data(), nullptr,
                              nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                              &startup, &info));
        upgrade::Handle child(info.hProcess), thread(info.hThread);
        assert(WaitForSingleObject(ready.get(), 10000) == WAIT_OBJECT_0);
        rejected([&] { upgrade::FileGuard guard(dll); });
        rejected([&] { upgrade::FileGuard guard(executable); });
        SetEvent(stop.get());
        assert(WaitForSingleObject(child.get(), 10000) == WAIT_OBJECT_0);
    }
    {
        upgrade::Handle file(CreateFileW(dll.c_str(), GENERIC_READ,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         nullptr, OPEN_EXISTING, 0, nullptr));
        assert(file);
        upgrade::Handle mapping(CreateFileMappingW(
            file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
        assert(mapping);
        const auto view = MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0);
        assert(view);
        file = upgrade::Handle();
        const auto previous = upgrade::readText(dll);
        upgrade::writeText(root / "mapped-new.txt", "immutable replacement");
        {
            upgrade::FileGuard guard(dll, false, root / "retired");
            guard.replace(root / "mapped-new.txt");
            assert(memcmp(view, previous.data(), previous.size()) == 0);
        }
        assert(upgrade::readText(dll) == "immutable replacement");
        assert(memcmp(view, previous.data(), previous.size()) == 0);
        UnmapViewOfFile(view);
    }
    {
        upgrade::FileGuard guard(dll);
        guard.replace(argv[1]);
    }
    {
        upgrade::FileGuard guard(dll);
        // A load racing the preflight must fail while the handle is retained.
        assert(!LoadLibraryW(dll.c_str()));
        guard.backup(root / "backup.dll");
        assert(upgrade::sha256(root / "backup.dll") == original);
        rejected([&] { upgrade::FileGuard second(dll); });
        upgrade::writeText(root / "new.txt", "new content\n");
        guard.replace(root / "new.txt");
        assert(upgrade::sha256(guard.get()) ==
               upgrade::sha256(root / "new.txt"));
    }
    assert(upgrade::readText(dll) == "new content\n");
    {
        upgrade::FileGuard guard(dll);
        guard.replace(root / "backup.dll");
    }
    assert(upgrade::sha256(dll) == original);
    module = LoadLibraryW(dll.c_str());
    assert(module);
    FreeLibrary(module);
    fs::create_hard_link(dll, root / "linked.dll");
    rejected([&] { upgrade::FileGuard guard(dll); });
    fs::remove(root / "linked.dll");
    for (auto path :
         {"../bin/a", "bin/../a", "/bin/a", "C:/a", "bin/a:b", "bin/a\\b",
          "bin/CON.txt", "bin/a.", "bin/a ", "bin/*", "bin//a"}) {
        rejected([&] { upgrade::relativePath(path); });
    }
    assert(upgrade::runtimePath(upgrade::relativePath("bin/Fcitx5.exe")));
    assert(!upgrade::runtimePath(
        upgrade::relativePath("setup/maintenance-host/a.exe")));
    {
        fs::create_directories(root / "setup");
        upgrade::InstallationLock lock(root);
        rejected([&] { upgrade::InstallationLock second(root); });
    }
    fs::remove_all(root);
    std::cout << "PASS: loaded image rejection, concurrent LoadLibrary guard, "
                 "immutable read-only mappings, backup/replace/restore, hard "
                 "links and paths\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
