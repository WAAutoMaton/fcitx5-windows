#include "../setup/upgrade_files.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <sddl.h>

namespace upgrade = fcitx::setup::upgrade;
namespace fs = std::filesystem;

namespace {
bool checkSddl(const wchar_t *sddl,
               const upgrade::detail::PrincipalPolicy &policy,
               std::string *detail = nullptr) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    assert(ConvertStringSecurityDescriptorToSecurityDescriptorW(
        sddl, SDDL_REVISION_1, &descriptor, nullptr));
    const bool ok =
        upgrade::detail::validateSecurityDescriptor(descriptor, policy, detail);
    LocalFree(descriptor);
    return ok;
}

bool checkObjectAce(DWORD flags, ACCESS_MASK mask, PSID aceSid,
                    const upgrade::detail::PrincipalPolicy &policy,
                    std::string *detail = nullptr) {
    BYTE adminSid[SECURITY_MAX_SID_SIZE]{};
    DWORD adminSize = sizeof(adminSid);
    assert(CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, adminSid,
                              &adminSize));
    const DWORD sidLength = GetLengthSid(aceSid);
    size_t sidOffset = offsetof(ACCESS_ALLOWED_OBJECT_ACE, ObjectType);
    if (flags & ACE_OBJECT_TYPE_PRESENT)
        sidOffset += sizeof(GUID);
    if (flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
        sidOffset += sizeof(GUID);
    const WORD aceSize = static_cast<WORD>((sidOffset + sidLength + 3) & ~3u);
    std::vector<BYTE> aclBuffer(sizeof(ACL) + aceSize, 0);
    auto *acl = reinterpret_cast<PACL>(aclBuffer.data());
    assert(InitializeAcl(acl, DWORD(aclBuffer.size()), ACL_REVISION_DS));
    std::vector<BYTE> aceBytes(aceSize, 0);
    auto *ace = reinterpret_cast<ACCESS_ALLOWED_OBJECT_ACE *>(aceBytes.data());
    ace->Header.AceType = ACCESS_ALLOWED_OBJECT_ACE_TYPE;
    ace->Header.AceFlags = 0;
    ace->Header.AceSize = aceSize;
    ace->Mask = mask;
    ace->Flags = flags;
    memcpy(aceBytes.data() + sidOffset, aceSid, sidLength);
    assert(AddAce(acl, ACL_REVISION_DS, MAXDWORD, aceBytes.data(), aceSize));

    SECURITY_DESCRIPTOR sd{};
    assert(InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION));
    assert(SetSecurityDescriptorOwner(&sd, adminSid, FALSE));
    assert(SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE));
    return upgrade::detail::validateSecurityDescriptor(&sd, policy, detail);
}
} // namespace

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

    BYTE usersSidBuffer[SECURITY_MAX_SID_SIZE]{};
    DWORD usersSidSize = sizeof(usersSidBuffer);
    assert(CreateWellKnownSid(WinBuiltinUsersSid, nullptr, usersSidBuffer,
                              &usersSidSize));

    PSID builtinAdminSid = nullptr, domainAdminsSid = nullptr,
         enterpriseAdminsSid = nullptr, personalAdminSid = nullptr;
    assert(ConvertStringSidToSidW(L"S-1-5-21-11111-22222-33333-500",
                                  &builtinAdminSid));
    assert(ConvertStringSidToSidW(L"S-1-5-21-44444-55555-66666-512",
                                  &domainAdminsSid));
    assert(ConvertStringSidToSidW(L"S-1-5-21-44444-55555-66666-519",
                                  &enterpriseAdminsSid));
    assert(ConvertStringSidToSidW(L"S-1-5-21-11111-22222-33333-1001",
                                  &personalAdminSid));

    upgrade::detail::PrincipalPolicy strictPolicy;
    strictPolicy.builtinAdminSid = builtinAdminSid;
    strictPolicy.domainAdminsSid = domainAdminsSid;
    strictPolicy.enterpriseAdminsSid = enterpriseAdminsSid;
    strictPolicy.currentAdminUserSid = personalAdminSid;

    assert(upgrade::detail::trustedOwnerPrincipal(administratorsSidBuffer,
                                                  strictPolicy));
    assert(
        upgrade::detail::trustedOwnerPrincipal(systemSidBuffer, strictPolicy));
    assert(
        upgrade::detail::trustedOwnerPrincipal(domainAdminsSid, strictPolicy));
    assert(upgrade::detail::trustedOwnerPrincipal(enterpriseAdminsSid,
                                                  strictPolicy));
    assert(
        !upgrade::detail::trustedOwnerPrincipal(builtinAdminSid, strictPolicy));
    assert(!upgrade::detail::trustedOwnerPrincipal(personalAdminSid,
                                                   strictPolicy));

    auto unfilteredAdminPolicy = strictPolicy;
    unfilteredAdminPolicy.allowUnfilteredBuiltinAdmin = true;
    assert(upgrade::detail::trustedOwnerPrincipal(builtinAdminSid,
                                                  unfilteredAdminPolicy));
    assert(!upgrade::detail::trustedOwnerPrincipal(personalAdminSid,
                                                   unfilteredAdminPolicy));

    auto uacDisabledPolicy = unfilteredAdminPolicy;
    uacDisabledPolicy.uacDisabled = true;
    assert(upgrade::detail::trustedOwnerPrincipal(personalAdminSid,
                                                  uacDisabledPolicy));
    assert(!upgrade::detail::trustedOwnerPrincipal(everyoneSidBuffer,
                                                   uacDisabledPolicy));
    assert(!upgrade::detail::trustedOwnerPrincipal(usersSidBuffer,
                                                   uacDisabledPolicy));

    // TrustedInstaller as owner + standard Program Files ACLs + OWNER RIGHTS +
    // read-only conditional ACE must pass.
    std::string detail;
    assert(checkSddl(
        L"O:S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464"
        L"D:(A;;FA;;;SY)(A;;FA;;;BA)(A;;FRFX;;;BU)(A;;FA;;;OW)"
        L"(A;OICIIO;FA;;;CO)",
        strictPolicy, &detail));
    assert(
        checkSddl(L"O:BAD:(A;;FA;;;BA)(XA;;FRFX;;;BU;(Exists WIN://SYSAPPID))",
                  strictPolicy, &detail));
    assert(checkSddl(L"O:BAD:(XA;;FA;;;BA;(Exists WIN://SYSAPPID))",
                     strictPolicy, &detail));
    // Writable conditional ACE for Users must be rejected with diagnostics.
    detail.clear();
    assert(
        !checkSddl(L"O:BAD:(A;;FA;;;BA)(XA;;FW;;;BU;(Exists WIN://SYSAPPID))",
                   strictPolicy, &detail));
    assert(detail.find("untrusted-write-ace") != std::string::npos);
    assert(detail.find("sid-kind=users") != std::string::npos);
    // Untrusted owner (split-token personal admin SID) must be rejected when
    // UAC is enabled, and allowed only when UAC is disabled.
    detail.clear();
    assert(
        !checkSddl(L"O:S-1-5-21-11111-22222-33333-1001"
                   L"D:(A;;FA;;;BA)(A;;FA;;;S-1-5-21-11111-22222-33333-1001)",
                   strictPolicy, &detail));
    assert(detail.find("untrusted-owner") != std::string::npos);
    assert(checkSddl(L"O:S-1-5-21-11111-22222-33333-1001"
                     L"D:(A;;FA;;;BA)(A;;FA;;;S-1-5-21-11111-22222-33333-1001)",
                     uacDisabledPolicy, &detail));
    // Built-in admin (RID 500) allowed when unfiltered, rejected when filtered.
    assert(checkSddl(L"O:S-1-5-21-11111-22222-33333-500"
                     L"D:(A;;FA;;;BA)(A;;FA;;;S-1-5-21-11111-22222-33333-500)",
                     unfilteredAdminPolicy, &detail));
    assert(!checkSddl(L"O:S-1-5-21-11111-22222-33333-500"
                      L"D:(A;;FA;;;BA)(A;;FA;;;S-1-5-21-11111-22222-33333-500)",
                      strictPolicy, &detail));
    // Missing DACL (NULL DACL) must be rejected.
    detail.clear();
    assert(!checkSddl(L"O:BAD:NO_ACCESS_CONTROL", strictPolicy, &detail));
    assert(detail == "missing-dacl");
    // Object ACE: read-only for Users passes; writable for Users fails;
    // writable for Administrators passes.
    assert(checkObjectAce(ACE_OBJECT_TYPE_PRESENT, FILE_GENERIC_READ,
                          usersSidBuffer, strictPolicy, &detail));
    detail.clear();
    assert(!checkObjectAce(
        ACE_OBJECT_TYPE_PRESENT | ACE_INHERITED_OBJECT_TYPE_PRESENT,
        FILE_WRITE_DATA, usersSidBuffer, strictPolicy, &detail));
    assert(detail.find("untrusted-write-ace") != std::string::npos);
    assert(detail.find("sid-kind=users") != std::string::npos);
    assert(checkObjectAce(
        ACE_OBJECT_TYPE_PRESENT | ACE_INHERITED_OBJECT_TYPE_PRESENT,
        FILE_WRITE_DATA, administratorsSidBuffer, strictPolicy, &detail));

    // Reparse tag allowlist: only WOF transparent compression is accepted.
    assert(upgrade::detail::allowedReparseTag(0x80000017UL));
    assert(!upgrade::detail::allowedReparseTag(IO_REPARSE_TAG_SYMLINK));
    assert(!upgrade::detail::allowedReparseTag(IO_REPARSE_TAG_MOUNT_POINT));
    assert(!upgrade::detail::allowedReparseTag(0x9000001AUL));
    assert(!upgrade::detail::allowedReparseTag(0));

    LocalFree(builtinAdminSid);
    LocalFree(domainAdminsSid);
    LocalFree(enterpriseAdminsSid);
    LocalFree(personalAdminSid);

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
    assert(upgrade::runtimePath(upgrade::relativePath(
        "plugins/fcitx5-wenzhounese/lib/fcitx5/libwenzhounese.dll")));
    assert(upgrade::runtimePath(upgrade::relativePath(
        "plugins/future-plugin/share/libime/example.dict")));
    assert(!upgrade::runtimePath(
        upgrade::relativePath("plugins-other/example.dll")));
    assert(!upgrade::runtimePath(
        upgrade::relativePath("setup/maintenance-host/a.exe")));
    {
        fs::create_directories(root / "setup");
        upgrade::InstallationLock lock(root);
        rejected([&] { upgrade::InstallationLock second(root); });
    }
    // External temp inputs tolerate ancestor directory junctions while
    // installation-tree readText still rejects them and external reads still
    // reject hard links.
    {
        const auto realTemp = root / "real-temp";
        const auto junction = root / "temp-junction";
        fs::create_directories(realTemp);
        upgrade::writeText(realTemp / "manifest.json", "{\"ok\":true}\n");
        std::wstring cmd = L"cmd.exe /c mklink /J \"" + junction.wstring() +
                           L"\" \"" + realTemp.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION info{};
        assert(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                              &info));
        upgrade::Handle child(info.hProcess), thread(info.hThread);
        assert(WaitForSingleObject(child.get(), 10000) == WAIT_OBJECT_0);
        DWORD exitCode = 1;
        assert(GetExitCodeProcess(child.get(), &exitCode) && exitCode == 0);
        std::string pathDetail;
        rejected([&] {
            upgrade::validatePath(junction / "manifest.json", false,
                                  &pathDetail);
        });
        assert(pathDetail.find("reparse-point") != std::string::npos);
        rejected([&] { upgrade::readText(junction / "manifest.json"); });
        assert(upgrade::readExternalText(junction / "manifest.json") ==
               "{\"ok\":true}\n");
        fs::create_hard_link(realTemp / "manifest.json",
                             realTemp / "manifest-link.json");
        std::string linkDetail;
        rejected([&] {
            upgrade::readExternalText(junction / "manifest-link.json",
                                      1024 * 1024, &linkDetail);
        });
        assert(linkDetail.find("hardlink-count=2") != std::string::npos);
        fs::remove(realTemp / "manifest-link.json");
        assert(RemoveDirectoryW(junction.c_str()));
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
