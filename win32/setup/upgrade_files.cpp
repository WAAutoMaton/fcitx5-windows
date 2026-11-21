#include "upgrade_files.h"
#include "../ipc/atomicfile.h"
#include <aclapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bcrypt.h>
#include <cstdio>
#include <cwctype>
#include <memory>
#include <ntsecapi.h>
#include <optional>
#include <sddl.h>
#include <system_error>
#include <utility>

namespace fcitx::setup::upgrade {
namespace {
constexpr DWORD kReparseTagWof = 0x80000017UL;
constexpr DWORD kWriteRights = FILE_WRITE_DATA | FILE_APPEND_DATA |
                               FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | DELETE |
                               FILE_DELETE_CHILD | WRITE_DAC | WRITE_OWNER |
                               GENERIC_WRITE | GENERIC_ALL;

void setDetail(std::string *detail, std::string value) {
    if (detail)
        *detail = std::move(value);
}

std::string hex32(DWORD value) {
    char buffer[16]{};
    snprintf(buffer, sizeof(buffer), "0x%08lX", value);
    return buffer;
}

PSID trustedInstallerSid() {
    static const auto sid = [] {
        constexpr wchar_t sidText[] =
            L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464";
        PSID value = nullptr;
        if (!ConvertStringSidToSidW(sidText, &value))
            fail();
        return std::unique_ptr<void, decltype(&LocalFree)>(value, LocalFree);
    }();
    return sid.get();
}

std::vector<BYTE> createDomainWellKnownSid(WELL_KNOWN_SID_TYPE type,
                                           PSID domainSid) {
    if (!domainSid || !IsValidSid(domainSid))
        return {};
    DWORD size = SECURITY_MAX_SID_SIZE;
    std::vector<BYTE> buffer(size);
    if (!CreateWellKnownSid(type, domainSid, buffer.data(), &size))
        return {};
    buffer.resize(size);
    return buffer;
}

bool readPolicyDword(const wchar_t *name, DWORD &value, DWORD missingDefault) {
    DWORD size = sizeof(value);
    const auto status = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", name,
        RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr, &value, &size);
    if (status == ERROR_SUCCESS)
        return true;
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
        value = missingDefault;
        return true;
    }
    return false;
}

struct OwnedPrincipalPolicy {
    std::vector<BYTE> builtinAdmin;
    std::vector<BYTE> domainAdmins;
    std::vector<BYTE> enterpriseAdmins;
    std::vector<BYTE> currentAdminUser;
    detail::PrincipalPolicy policy;

    const detail::PrincipalPolicy &view() const { return policy; }
};

OwnedPrincipalPolicy querySystemPrincipalPolicy() {
    OwnedPrincipalPolicy owned;
    DWORD enableLua = 1;
    if (readPolicyDword(L"EnableLUA", enableLua, 1))
        owned.policy.uacDisabled = (enableLua == 0);
    DWORD filterAdmin = 1;
    if (readPolicyDword(L"FilterAdministratorToken", filterAdmin, 0))
        owned.policy.allowUnfilteredBuiltinAdmin =
            owned.policy.uacDisabled || (filterAdmin == 0);
    else
        owned.policy.allowUnfilteredBuiltinAdmin = owned.policy.uacDisabled;

    LSA_OBJECT_ATTRIBUTES attributes{};
    LSA_HANDLE lsa = nullptr;
    if (LsaOpenPolicy(nullptr, &attributes, POLICY_VIEW_LOCAL_INFORMATION,
                      &lsa) == 0 &&
        lsa) {
        PVOID accountInfo = nullptr;
        if (LsaQueryInformationPolicy(lsa, PolicyAccountDomainInformation,
                                      &accountInfo) == 0 &&
            accountInfo) {
            const auto *domain =
                static_cast<const POLICY_ACCOUNT_DOMAIN_INFO *>(accountInfo);
            owned.builtinAdmin = createDomainWellKnownSid(
                WinAccountAdministratorSid, domain->DomainSid);
            if (!owned.builtinAdmin.empty())
                owned.policy.builtinAdminSid = owned.builtinAdmin.data();
            LsaFreeMemory(accountInfo);
        }
        PVOID primaryInfo = nullptr;
        if (LsaQueryInformationPolicy(lsa, PolicyPrimaryDomainInformation,
                                      &primaryInfo) == 0 &&
            primaryInfo) {
            const auto *domain =
                static_cast<const POLICY_PRIMARY_DOMAIN_INFO *>(primaryInfo);
            owned.domainAdmins = createDomainWellKnownSid(
                WinAccountDomainAdminsSid, domain->Sid);
            if (!owned.domainAdmins.empty())
                owned.policy.domainAdminsSid = owned.domainAdmins.data();
            owned.enterpriseAdmins = createDomainWellKnownSid(
                WinAccountEnterpriseAdminsSid, domain->Sid);
            if (!owned.enterpriseAdmins.empty())
                owned.policy.enterpriseAdminsSid =
                    owned.enterpriseAdmins.data();
            LsaFreeMemory(primaryInfo);
        }
        LsaClose(lsa);
    }

    if (owned.policy.uacDisabled) {
        BYTE adminSid[SECURITY_MAX_SID_SIZE]{};
        DWORD adminSize = sizeof(adminSid);
        BOOL isAdmin = FALSE;
        if (CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, adminSid,
                               &adminSize) &&
            CheckTokenMembership(nullptr, adminSid, &isAdmin) && isAdmin) {
            HANDLE token = nullptr;
            if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
                Handle tokenLifetime(token);
                DWORD bytes = 0;
                GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
                if (bytes > 0 && bytes <= 4096) {
                    std::vector<BYTE> userInfo(bytes);
                    if (GetTokenInformation(token, TokenUser, userInfo.data(),
                                            bytes, &bytes)) {
                        const auto *user = reinterpret_cast<const TOKEN_USER *>(
                            userInfo.data());
                        if (user->User.Sid && IsValidSid(user->User.Sid)) {
                            const auto length = GetLengthSid(user->User.Sid);
                            owned.currentAdminUser.resize(length);
                            if (CopySid(length, owned.currentAdminUser.data(),
                                        user->User.Sid))
                                owned.policy.currentAdminUserSid =
                                    owned.currentAdminUser.data();
                            else
                                owned.currentAdminUser.clear();
                        }
                    }
                }
            }
        }
    }
    return owned;
}

bool matchSid(PSID candidate, PSID target) {
    return candidate && target && IsValidSid(candidate) && IsValidSid(target) &&
           EqualSid(candidate, target) != FALSE;
}

bool extractAllowAce(const ACE_HEADER *header, ACCESS_MASK &mask, PSID &sid,
                     bool &compound) {
    compound = false;
    const auto *bytes = reinterpret_cast<const BYTE *>(header);
    size_t sidOffset = 0;
    switch (header->AceType) {
    case ACCESS_ALLOWED_ACE_TYPE:
    case ACCESS_ALLOWED_CALLBACK_ACE_TYPE: {
        if (header->AceSize < sizeof(ACCESS_ALLOWED_ACE))
            return false;
        const auto *ace = reinterpret_cast<const ACCESS_ALLOWED_ACE *>(header);
        mask = ace->Mask;
        sidOffset = offsetof(ACCESS_ALLOWED_ACE, SidStart);
        break;
    }
    case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
    case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE: {
        if (header->AceSize < offsetof(ACCESS_ALLOWED_OBJECT_ACE, ObjectType))
            return false;
        const auto *ace =
            reinterpret_cast<const ACCESS_ALLOWED_OBJECT_ACE *>(header);
        mask = ace->Mask;
        sidOffset = offsetof(ACCESS_ALLOWED_OBJECT_ACE, ObjectType);
        if (ace->Flags & ACE_OBJECT_TYPE_PRESENT)
            sidOffset += sizeof(GUID);
        if (ace->Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
            sidOffset += sizeof(GUID);
        break;
    }
    case ACCESS_ALLOWED_COMPOUND_ACE_TYPE: {
        if (header->AceSize < sizeof(ACCESS_ALLOWED_ACE))
            return false;
        const auto *ace = reinterpret_cast<const ACCESS_ALLOWED_ACE *>(header);
        mask = ace->Mask;
        sid = nullptr;
        compound = true;
        return true;
    }
    default:
        return false;
    }
    if ((mask & kWriteRights) == 0) {
        sid = nullptr;
        return true;
    }
    if (sidOffset + offsetof(SID, SubAuthority) > header->AceSize)
        return false;
    const auto *candidate = reinterpret_cast<const SID *>(bytes + sidOffset);
    if (sidOffset + GetSidLengthRequired(candidate->SubAuthorityCount) >
        header->AceSize)
        return false;
    auto *mutableSid =
        const_cast<void *>(reinterpret_cast<const void *>(candidate));
    if (!IsValidSid(mutableSid))
        return false;
    sid = mutableSid;
    return true;
}

bool isIgnoredNonAllowAce(BYTE type) {
    switch (type) {
    case ACCESS_DENIED_ACE_TYPE:
    case ACCESS_DENIED_OBJECT_ACE_TYPE:
    case ACCESS_DENIED_CALLBACK_ACE_TYPE:
    case ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE:
    case SYSTEM_AUDIT_ACE_TYPE:
    case SYSTEM_ALARM_ACE_TYPE:
    case SYSTEM_AUDIT_OBJECT_ACE_TYPE:
    case SYSTEM_ALARM_OBJECT_ACE_TYPE:
    case SYSTEM_MANDATORY_LABEL_ACE_TYPE:
    case SYSTEM_AUDIT_CALLBACK_ACE_TYPE:
    case SYSTEM_ALARM_CALLBACK_ACE_TYPE:
    case SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE:
    case SYSTEM_ALARM_CALLBACK_OBJECT_ACE_TYPE:
    case SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE:
    case SYSTEM_SCOPED_POLICY_ID_ACE_TYPE:
    case SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE:
    case SYSTEM_ACCESS_FILTER_ACE_TYPE:
        return true;
    default:
        return false;
    }
}

std::optional<DWORD> reparseTagForPath(const fs::path &path) {
    WIN32_FIND_DATAW data{};
    const auto search = FindFirstFileW(path.c_str(), &data);
    if (search == INVALID_HANDLE_VALUE)
        return std::nullopt;
    FindClose(search);
    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return 0;
    return data.dwReserved0;
}

fs::path finalPathForHandle(HANDLE file) {
    std::wstring buffer(32768, L'\0');
    const auto length = GetFinalPathNameByHandleW(
        file, buffer.data(), DWORD(buffer.size()), FILE_NAME_NORMALIZED);
    if (!length || length >= buffer.size())
        fail();
    buffer.resize(length);
    if (buffer.starts_with(L"\\\\?\\UNC\\"))
        fail(ERROR_ACCESS_DENIED);
    if (buffer.starts_with(L"\\\\?\\"))
        buffer.erase(0, 4);
    return fs::path(buffer);
}

void verifyLocalNtfs(const fs::path &path, std::string *detail) {
    const auto drive = path.root_name().wstring();
    if (!path.is_absolute() || drive.size() != 2 || drive.back() != L':') {
        setDetail(detail, "not-drive-path");
        fail(ERROR_ACCESS_DENIED);
    }
    wchar_t filesystem[32]{};
    if (GetDriveTypeW(path.root_path().c_str()) != DRIVE_FIXED ||
        !GetVolumeInformationW(path.root_path().c_str(), nullptr, 0, nullptr,
                               nullptr, nullptr, filesystem, 32) ||
        std::wstring_view(filesystem) != L"NTFS") {
        setDetail(detail, "not-local-ntfs");
        fail(ERROR_NOT_SUPPORTED);
    }
}
} // namespace

namespace detail {
bool allowedReparseTag(DWORD tag) { return tag == kReparseTagWof; }

bool trustedWritePrincipal(PSID sid, const PrincipalPolicy &policy) {
    if (!sid || !IsValidSid(sid))
        return false;
    if (IsWellKnownSid(sid, WinLocalSystemSid) ||
        IsWellKnownSid(sid, WinBuiltinAdministratorsSid) ||
        EqualSid(sid, trustedInstallerSid()))
        return true;
    if (matchSid(sid, policy.domainAdminsSid) ||
        matchSid(sid, policy.enterpriseAdminsSid))
        return true;
    if (policy.allowUnfilteredBuiltinAdmin &&
        matchSid(sid, policy.builtinAdminSid))
        return true;
    if (policy.uacDisabled && matchSid(sid, policy.currentAdminUserSid))
        return true;
    return false;
}

bool trustedWritePrincipal(PSID sid) {
    return trustedWritePrincipal(sid, PrincipalPolicy{});
}

bool trustedOwnerPrincipal(PSID sid, const PrincipalPolicy &policy) {
    return trustedWritePrincipal(sid, policy);
}

std::string sidCategory(PSID sid, const PrincipalPolicy &policy) {
    if (!sid || !IsValidSid(sid))
        return "invalid";
    if (IsWellKnownSid(sid, WinLocalSystemSid))
        return "system";
    if (IsWellKnownSid(sid, WinBuiltinAdministratorsSid))
        return "administrators";
    if (EqualSid(sid, trustedInstallerSid()))
        return "trusted-installer";
    if (matchSid(sid, policy.domainAdminsSid))
        return "domain-admins";
    if (matchSid(sid, policy.enterpriseAdminsSid))
        return "enterprise-admins";
    if (matchSid(sid, policy.builtinAdminSid))
        return "builtin-admin";
    if (matchSid(sid, policy.currentAdminUserSid))
        return "current-admin-user";
    if (IsWellKnownSid(sid, WinCreatorOwnerRightsSid))
        return "owner-rights";
    if (IsWellKnownSid(sid, WinCreatorOwnerSid))
        return "creator-owner";
    if (IsWellKnownSid(sid, WinBuiltinUsersSid))
        return "users";
    if (IsWellKnownSid(sid, WinAuthenticatedUserSid))
        return "authenticated-users";
    if (IsWellKnownSid(sid, WinWorldSid))
        return "everyone";
    if (IsWellKnownSid(sid, WinInteractiveSid))
        return "interactive";
    if (IsWellKnownSid(sid, WinLocalServiceSid))
        return "local-service";
    if (IsWellKnownSid(sid, WinNetworkServiceSid))
        return "network-service";
    if (IsWellKnownSid(sid, WinBuiltinAnyPackageSid))
        return "app-packages";
    return "other-account";
}

bool validateSecurityDescriptor(PSECURITY_DESCRIPTOR descriptor,
                                const PrincipalPolicy &policy,
                                std::string *detail) {
    if (!descriptor) {
        setDetail(detail, "missing-descriptor");
        return false;
    }
    PSID owner = nullptr;
    BOOL ownerDefaulted = FALSE;
    if (!GetSecurityDescriptorOwner(descriptor, &owner, &ownerDefaulted) ||
        !owner || !IsValidSid(owner)) {
        setDetail(detail, "missing-owner");
        return false;
    }
    PACL acl = nullptr;
    BOOL daclPresent = FALSE, daclDefaulted = FALSE;
    if (!GetSecurityDescriptorDacl(descriptor, &daclPresent, &acl,
                                   &daclDefaulted) ||
        !daclPresent || !acl) {
        setDetail(detail, "missing-dacl");
        return false;
    }
    if (!trustedOwnerPrincipal(owner, policy)) {
        setDetail(detail,
                  "untrusted-owner sid-kind=" + sidCategory(owner, policy));
        return false;
    }
    for (DWORD index = 0; index < acl->AceCount; ++index) {
        void *value = nullptr;
        if (!GetAce(acl, index, &value)) {
            setDetail(detail, "ace-read-failed");
            return false;
        }
        const auto *header = static_cast<const ACE_HEADER *>(value);
        if (header->AceFlags & INHERIT_ONLY_ACE)
            continue;
        if (isIgnoredNonAllowAce(header->AceType))
            continue;
        ACCESS_MASK mask = 0;
        PSID sid = nullptr;
        bool compound = false;
        if (!extractAllowAce(header, mask, sid, compound)) {
            setDetail(detail, "unsupported-ace ace-type=" +
                                  std::to_string(unsigned(header->AceType)));
            return false;
        }
        if ((mask & kWriteRights) == 0)
            continue;
        if (compound) {
            setDetail(detail, "untrusted-write-ace ace-type=" +
                                  std::to_string(unsigned(header->AceType)) +
                                  " mask=" + hex32(mask) +
                                  " sid-kind=compound");
            return false;
        }
        // OWNER RIGHTS applies strictly to the already-validated trusted owner.
        if (IsWellKnownSid(sid, WinCreatorOwnerRightsSid))
            continue;
        if (!trustedWritePrincipal(sid, policy)) {
            setDetail(detail, "untrusted-write-ace ace-type=" +
                                  std::to_string(unsigned(header->AceType)) +
                                  " mask=" + hex32(mask) +
                                  " sid-kind=" + sidCategory(sid, policy));
            return false;
        }
    }
    return true;
}
} // namespace detail

namespace {
void checkFile(HANDLE file, std::string *detail = nullptr) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file, &info))
        fail();
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        setDetail(detail, "unexpected-directory");
        fail(ERROR_ACCESS_DENIED);
    }
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        FILE_ATTRIBUTE_TAG_INFO tagInfo{};
        if (!GetFileInformationByHandleEx(file, FileAttributeTagInfo, &tagInfo,
                                          sizeof(tagInfo)) ||
            !detail::allowedReparseTag(tagInfo.ReparseTag)) {
            setDetail(detail, "reparse-point tag=" + hex32(tagInfo.ReparseTag));
            fail(ERROR_ACCESS_DENIED);
        }
    }
    if (info.nNumberOfLinks != 1) {
        setDetail(detail,
                  "hardlink-count=" + std::to_string(info.nNumberOfLinks));
        fail(ERROR_ACCESS_DENIED);
    }
}

void rewind(HANDLE file) {
    if (!SetFilePointerEx(file, {}, nullptr, FILE_BEGIN))
        fail();
}

void copySecurity(HANDLE source, HANDLE destination) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL acl = nullptr;
    auto error =
        GetSecurityInfo(source, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                        nullptr, nullptr, &acl, nullptr, &descriptor);
    if (error)
        fail(error);
    error = SetSecurityInfo(destination, SE_FILE_OBJECT,
                            DACL_SECURITY_INFORMATION |
                                PROTECTED_DACL_SECURITY_INFORMATION,
                            nullptr, nullptr, acl, nullptr);
    LocalFree(descriptor);
    if (error)
        fail(error);
    if (!FlushFileBuffers(destination))
        fail();
}
void renameFile(HANDLE file, const fs::path &destination) {
    const auto name = destination.wstring();
    const auto bytes = name.size() * sizeof(wchar_t);
    std::vector<BYTE> buffer(offsetof(FILE_RENAME_INFO, FileName) + bytes +
                             sizeof(wchar_t));
    auto *info = reinterpret_cast<FILE_RENAME_INFO *>(buffer.data());
    info->ReplaceIfExists = FALSE;
    info->RootDirectory = nullptr;
    info->FileNameLength = DWORD(bytes);
    memcpy(info->FileName, name.data(), bytes);
    if (!SetFileInformationByHandle(file, FileRenameInfo, info,
                                    DWORD(buffer.size())))
        fail();
    if (!FlushFileBuffers(file))
        fail();
}
fs::path temporaryFile(const fs::path &directory, const wchar_t *kind) {
    static std::atomic_uint64_t sequence = 0;
    validatePath(directory, true);
    fs::create_directories(directory);
    return directory / (L".fcitx-" + std::wstring(kind) + L"-" +
                        std::to_wstring(GetCurrentProcessId()) + L"-" +
                        std::to_wstring(++sequence) + L".tmp");
}
} // namespace

Handle::~Handle() {
    if (*this)
        CloseHandle(value_);
}
Handle::Handle(Handle &&other) noexcept
    : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
Handle &Handle::operator=(Handle &&other) noexcept {
    if (this != &other) {
        if (*this)
            CloseHandle(value_);
        value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
    }
    return *this;
}
[[noreturn]] void fail(DWORD error) {
    throw std::system_error(static_cast<int>(error), std::system_category());
}

std::string utf8(std::wstring_view value) {
    if (value.empty())
        return {};
    const auto size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            int(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size)
        fail(ERROR_INVALID_DATA);
    std::string result(size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                             int(value.size()), result.data(), size, nullptr,
                             nullptr))
        fail();
    return result;
}

std::wstring wide(std::string_view value) {
    if (value.empty())
        return {};
    const auto size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            int(value.size()), nullptr, 0);
    if (!size)
        fail(ERROR_INVALID_DATA);
    std::wstring result(size, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                             int(value.size()), result.data(), size))
        fail();
    return result;
}

fs::path relativePath(std::string_view value) {
    if (value.empty() || value.size() > 1024 ||
        value.find("//") != std::string_view::npos || value.ends_with('/') ||
        value.find_first_of("\\:\0", 0, 3) != std::string_view::npos)
        fail(ERROR_INVALID_NAME);
    fs::path result(wide(value));
    if (result.is_absolute() || result.has_root_path())
        fail(ERROR_INVALID_NAME);
    for (const auto &part : result) {
        const auto name = part.wstring();
        if (name.empty() || name == L"." || name == L".." ||
            name.back() == L'.' || name.back() == L' ' ||
            name.find_first_of(L"<>\"|?*") != std::wstring::npos ||
            std::any_of(name.begin(), name.end(),
                        [](wchar_t c) { return c < 32; }))
            fail(ERROR_INVALID_NAME);
        auto base = name.substr(0, name.find(L'.'));
        std::transform(base.begin(), base.end(), base.begin(), towupper);
        if (base == L"CON" || base == L"PRN" || base == L"AUX" ||
            base == L"NUL" || base == L"CONIN$" || base == L"CONOUT$" ||
            (base.size() == 4 &&
             (base.starts_with(L"COM") || base.starts_with(L"LPT")) &&
             ((base.back() >= L'0' && base.back() <= L'9') ||
              base.back() == L'\u00b9' || base.back() == L'\u00b2' ||
              base.back() == L'\u00b3')))
            fail(ERROR_INVALID_NAME);
    }
    if (utf8(result.generic_wstring()) != value)
        fail(ERROR_INVALID_NAME);
    return result;
}

bool runtimePath(const fs::path &path) {
    if (path.empty())
        return false;
    const auto first = path.begin()->wstring();
    return first == L"bin" || first == L"lib" || first == L"share" ||
           first == L"tsf" || first == L"settings" || first == L"licenses" ||
           first == L"plugins" || path == L"setup/Fcitx5SetupHelper.exe" ||
           path == L"setup/managed-install";
}

void validatePath(const fs::path &path, bool allowMissing,
                  std::string *detail) {
    const auto drive = path.root_name().wstring();
    if (!path.is_absolute() || drive.size() != 2 || drive.back() != L':') {
        setDetail(detail, "invalid-absolute-path");
        fail(ERROR_INVALID_NAME);
    }
    for (const auto &part : path.relative_path()) {
        if (part == L".." || part == L".") {
            setDetail(detail, "dot-path-segment");
            fail(ERROR_INVALID_NAME);
        }
    }
    auto current = path.root_path();
    for (const auto &part : path.relative_path()) {
        if (part == L".." || part == L".") {
            setDetail(detail, "dot-path-segment");
            fail(ERROR_INVALID_NAME);
        }
        current /= part;
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto error = GetLastError();
            if (allowMissing && (error == ERROR_FILE_NOT_FOUND ||
                                 error == ERROR_PATH_NOT_FOUND))
                return;
            setDetail(detail, "attributes-failed");
            fail(error);
        }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            const auto tag = reparseTagForPath(current);
            if (!tag || !detail::allowedReparseTag(*tag)) {
                setDetail(detail,
                          "reparse-point tag=" + hex32(tag.value_or(0)));
                fail(ERROR_ACCESS_DENIED);
            }
        }
    }
}

void validateSecurity(const fs::path &path, std::string *detail) {
    validatePath(path, false, detail);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL acl = nullptr;
    PSID owner = nullptr;
    const auto error = GetNamedSecurityInfoW(
        path.c_str(), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr,
        &acl, nullptr, &descriptor);
    if (error) {
        setDetail(detail, "security-query-failed");
        fail(error);
    }
    struct Local {
        PSECURITY_DESCRIPTOR value;
        ~Local() { LocalFree(value); }
    } lifetime{descriptor};
    const auto policy = querySystemPrincipalPolicy();
    if (!detail::validateSecurityDescriptor(descriptor, policy.view(), detail))
        fail(ERROR_ACCESS_DENIED);
}

void protectDirectory(const fs::path &path) {
    validatePath(path, true);
    fs::create_directories(path);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)"
            L"(A;OICI;FRFX;;;BU)",
            SDDL_REVISION_1, &descriptor, nullptr))
        fail();
    struct Local {
        PSECURITY_DESCRIPTOR value;
        ~Local() { LocalFree(value); }
    } lifetime{descriptor};
    PSID owner = nullptr, group = nullptr;
    PACL dacl = nullptr;
    BOOL defaulted = FALSE, present = FALSE;
    if (!GetSecurityDescriptorOwner(descriptor, &owner, &defaulted) ||
        !GetSecurityDescriptorGroup(descriptor, &group, &defaulted) ||
        !GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) ||
        !present || !owner || !group || !dacl)
        fail(ERROR_INVALID_SECURITY_DESCR);
    auto target = path.wstring();
    const auto error = SetNamedSecurityInfoW(
        target.data(), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        owner, group, dacl, nullptr);
    if (error)
        fail(error);
    validateSecurity(path);
}

void protectFile(HANDLE file) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FRFX;;;BU)",
            SDDL_REVISION_1, &descriptor, nullptr))
        fail();
    struct Local {
        PSECURITY_DESCRIPTOR value;
        ~Local() { LocalFree(value); }
    } lifetime{descriptor};
    PSID owner = nullptr, group = nullptr;
    PACL dacl = nullptr;
    BOOL defaulted = FALSE, present = FALSE;
    if (!GetSecurityDescriptorOwner(descriptor, &owner, &defaulted) ||
        !GetSecurityDescriptorGroup(descriptor, &group, &defaulted) ||
        !GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) ||
        !present || !owner || !group || !dacl)
        fail(ERROR_INVALID_SECURITY_DESCR);
    const auto error = SetSecurityInfo(
        file, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        owner, group, dacl, nullptr);
    if (error)
        fail(error);
    if (!FlushFileBuffers(file))
        fail();
}

std::string sha256(HANDLE file) {
    rewind(file);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                    nullptr, 0) < 0)
        fail(ERROR_GEN_FAILURE);
    BCRYPT_HASH_HANDLE hash = nullptr;
    const auto created =
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (created < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        fail(ERROR_GEN_FAILURE);
    }
    DWORD error = ERROR_SUCCESS;
    std::array<unsigned char, 65536> buffer{};
    DWORD count = 0;
    for (;;) {
        if (!ReadFile(file, buffer.data(), DWORD(buffer.size()), &count,
                      nullptr)) {
            error = GetLastError();
            break;
        }
        if (!count)
            break;
        if (BCryptHashData(hash, buffer.data(), count, 0) < 0) {
            error = ERROR_GEN_FAILURE;
            break;
        }
    }
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(hash, digest.data(), DWORD(digest.size()), 0) < 0)
        error = ERROR_GEN_FAILURE;
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (error)
        fail(error);
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : digest) {
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    return result;
}

std::string sha256(const fs::path &path) {
    validatePath(path);
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING,
                            FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file)
        fail();
    checkFile(file.get());
    return sha256(file.get());
}

void copyContents(HANDLE source, HANDLE destination) {
    rewind(source);
    rewind(destination);
    std::array<char, 65536> buffer{};
    for (;;) {
        DWORD count = 0, written = 0;
        if (!ReadFile(source, buffer.data(), DWORD(buffer.size()), &count,
                      nullptr))
            fail();
        if (!count)
            break;
        if (!WriteFile(destination, buffer.data(), count, &written, nullptr) ||
            written != count)
            fail(ERROR_WRITE_FAULT);
    }
    if (!SetEndOfFile(destination) || !FlushFileBuffers(destination))
        fail();
}

namespace {
std::string readHandleText(HANDLE file, size_t limit) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        uint64_t(size.QuadPart) > limit)
        fail(ERROR_INVALID_DATA);
    std::string result(size_t(size.QuadPart), '\0');
    DWORD count = 0;
    if (!ReadFile(file, result.data(), DWORD(result.size()), &count, nullptr) ||
        count != result.size())
        fail(ERROR_READ_FAULT);
    return result;
}
} // namespace

Handle openVerifiedExternalFile(const fs::path &path, bool requireSecureParent,
                                std::string *detail) {
    const auto drive = path.root_name().wstring();
    if (!path.is_absolute() || drive.size() != 2 || drive.back() != L':') {
        setDetail(detail, "invalid-absolute-path");
        fail(ERROR_INVALID_NAME);
    }
    for (const auto &part : path.relative_path()) {
        if (part == L".." || part == L".") {
            setDetail(detail, "dot-path-segment");
            fail(ERROR_INVALID_NAME);
        }
    }
    const DWORD share = requireSecureParent
                            ? FILE_SHARE_READ
                            : (FILE_SHARE_READ | FILE_SHARE_DELETE);
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, share, nullptr,
                            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
                            nullptr));
    if (!file) {
        setDetail(detail, "open-external-failed");
        fail();
    }
    checkFile(file.get(), detail);
    const auto canonical = finalPathForHandle(file.get());
    if (requireSecureParent) {
        verifyLocalNtfs(canonical, detail);
        const auto policy = querySystemPrincipalPolicy();
        {
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            const auto error = GetSecurityInfo(
                file.get(), SE_FILE_OBJECT,
                OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, nullptr,
                nullptr, nullptr, nullptr, &descriptor);
            if (error) {
                setDetail(detail, "external-file-security-query-failed");
                fail(error);
            }
            struct Local {
                PSECURITY_DESCRIPTOR value;
                ~Local() { LocalFree(value); }
            } lifetime{descriptor};
            if (!detail::validateSecurityDescriptor(descriptor, policy.view(),
                                                    detail))
                fail(ERROR_ACCESS_DENIED);
        }
        const auto parent = canonical.parent_path();
        const auto parentAttrs = GetFileAttributesW(parent.c_str());
        if (parentAttrs == INVALID_FILE_ATTRIBUTES) {
            setDetail(detail, "external-parent-attributes-failed");
            fail();
        }
        if (parentAttrs & FILE_ATTRIBUTE_REPARSE_POINT) {
            const auto tag = reparseTagForPath(parent);
            if (!tag || !detail::allowedReparseTag(*tag)) {
                setDetail(detail, "external-parent-reparse-point tag=" +
                                      hex32(tag.value_or(0)));
                fail(ERROR_ACCESS_DENIED);
            }
        }
        {
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            const auto error = GetNamedSecurityInfoW(
                parent.c_str(), SE_FILE_OBJECT,
                OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, nullptr,
                nullptr, nullptr, nullptr, &descriptor);
            if (error) {
                setDetail(detail, "external-parent-security-query-failed");
                fail(error);
            }
            struct Local {
                PSECURITY_DESCRIPTOR value;
                ~Local() { LocalFree(value); }
            } lifetime{descriptor};
            std::string parentDetail;
            if (!detail::validateSecurityDescriptor(descriptor, policy.view(),
                                                    &parentDetail)) {
                setDetail(detail, "external-parent:" + parentDetail);
                fail(ERROR_ACCESS_DENIED);
            }
        }
    }
    return file;
}

std::string readText(const fs::path &path, size_t limit) {
    validatePath(path);
    Handle file(CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file)
        fail();
    checkFile(file.get());
    return readHandleText(file.get(), limit);
}

std::string readExternalText(const fs::path &path, size_t limit,
                             std::string *detail) {
    auto file = openVerifiedExternalFile(path, false, detail);
    return readHandleText(file.get(), limit);
}

void writeText(const fs::path &path, std::string_view value) {
    validatePath(path.parent_path());
    if (fs::exists(path))
        validatePath(path);
    if (!win32::ipc::replaceFile(path, value))
        fail(ERROR_WRITE_FAULT);
}

FileGuard::FileGuard(const fs::path &path, bool create, fs::path retirement)
    : path_(path), retirement_(retirement.empty() ? path.parent_path()
                                                  : std::move(retirement)) {
    validatePath(path, create);
    handle_ = Handle(CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE | WRITE_DAC, 0,
        nullptr, create ? CREATE_NEW : OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!handle_)
        fail();
    checkFile(handle_.get());
}
FileGuard::~FileGuard() {
    handle_ = Handle();
    for (auto &old : retired_) {
        old.file = Handle();
        // Read-only resource mappings can outlive a swap. Keep their old bytes
        // intact and let transaction cleanup retry deletion when necessary.
        DeleteFileW(old.path.c_str());
    }
}
void FileGuard::backup(const fs::path &destination) {
    validatePath(destination, true);
    fs::create_directories(destination.parent_path());
    FileGuard output(destination, true);
    copyContents(handle_.get(), output.get());
    copySecurity(handle_.get(), output.get());
}
void FileGuard::retire() {
    const auto path = temporaryFile(retirement_, L"old");
    retired_.push_back({Handle(), path});
    try {
        renameFile(handle_.get(), path);
    } catch (...) {
        retired_.pop_back();
        throw;
    }
    retired_.back().file = std::move(handle_);
}
void FileGuard::replace(const fs::path &source, bool restoreSecurity) {
    validatePath(source);
    Handle input(CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!input)
        fail();
    checkFile(input.get());
    const auto temporary = temporaryFile(retirement_, L"new");
    Handle output(CreateFileW(
        temporary.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE | WRITE_DAC, 0,
        nullptr, CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!output)
        fail();
    try {
        copyContents(input.get(), output.get());
        copySecurity(restoreSecurity ? input.get() : handle_.get(),
                     output.get());
        retire();
        renameFile(output.get(), path_);
        handle_ = std::move(output);
    } catch (...) {
        output = Handle();
        DeleteFileW(temporary.c_str());
        throw;
    }
}
void FileGuard::remove() { retire(); }

InstallationLock::InstallationLock(const fs::path &root) {
    const auto path = root / "setup" / "maintenance.lock";
    validatePath(path, true);
    file_ = Handle(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                               nullptr, OPEN_ALWAYS,
                               FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file_)
        fail();
    checkFile(file_.get());
}
} // namespace fcitx::setup::upgrade
