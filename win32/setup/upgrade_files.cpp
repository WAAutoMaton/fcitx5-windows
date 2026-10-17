#include "upgrade_files.h"
#include "../ipc/atomicfile.h"
#include <aclapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bcrypt.h>
#include <cwctype>
#include <sddl.h>
#include <system_error>
#include <utility>

namespace fcitx::setup::upgrade {
namespace {
void checkFile(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file, &info))
        fail();
    if ((info.dwFileAttributes &
         (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        info.nNumberOfLinks != 1)
        fail(ERROR_ACCESS_DENIED);
}

void rewind(HANDLE file) {
    if (!SetFilePointerEx(file, {}, nullptr, FILE_BEGIN))
        fail();
}

bool trustedSid(PSID sid) {
    return IsWellKnownSid(sid, WinLocalSystemSid) ||
           IsWellKnownSid(sid, WinBuiltinAdministratorsSid);
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
           path == L"setup/Fcitx5SetupHelper.exe" ||
           path == L"setup/managed-install";
}

void validatePath(const fs::path &path, bool allowMissing) {
    const auto drive = path.root_name().wstring();
    if (!path.is_absolute() || drive.size() != 2 || drive.back() != L':')
        fail(ERROR_INVALID_NAME);
    for (const auto &part : path.relative_path()) {
        if (part == L".." || part == L".")
            fail(ERROR_INVALID_NAME);
    }
    auto current = path.root_path();
    for (const auto &part : path.relative_path()) {
        if (part == L".." || part == L".")
            fail(ERROR_INVALID_NAME);
        current /= part;
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto error = GetLastError();
            if (allowMissing && (error == ERROR_FILE_NOT_FOUND ||
                                 error == ERROR_PATH_NOT_FOUND))
                return;
            fail(error);
        }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
            fail(ERROR_ACCESS_DENIED);
    }
}

void validateSecurity(const fs::path &path) {
    validatePath(path);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL acl = nullptr;
    PSID owner = nullptr;
    const auto error = GetNamedSecurityInfoW(
        path.c_str(), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr,
        &acl, nullptr, &descriptor);
    if (error)
        fail(error);
    struct Local {
        PSECURITY_DESCRIPTOR value;
        ~Local() { LocalFree(value); }
    } lifetime{descriptor};
    if (!acl || !owner || !trustedSid(owner))
        fail(ERROR_ACCESS_DENIED);
    constexpr DWORD writes = FILE_WRITE_DATA | FILE_APPEND_DATA |
                             FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | DELETE |
                             FILE_DELETE_CHILD | WRITE_DAC | WRITE_OWNER |
                             GENERIC_WRITE | GENERIC_ALL;
    for (DWORD index = 0; index < acl->AceCount; ++index) {
        void *value = nullptr;
        if (!GetAce(acl, index, &value))
            fail();
        const auto *ace = static_cast<ACCESS_ALLOWED_ACE *>(value);
        if (ace->Header.AceFlags & INHERIT_ONLY_ACE)
            continue;
        if (ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
            (ace->Mask & writes) &&
            !trustedSid(const_cast<DWORD *>(&ace->SidStart)))
            fail(ERROR_ACCESS_DENIED);
        if (ace->Header.AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE ||
            ace->Header.AceType == ACCESS_ALLOWED_CALLBACK_ACE_TYPE)
            fail(ERROR_ACCESS_DENIED);
    }
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
    const auto success = SetFileSecurityW(
        path.c_str(),
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
            PROTECTED_DACL_SECURITY_INFORMATION,
        descriptor);
    const auto error = GetLastError();
    LocalFree(descriptor);
    if (!success)
        fail(error);
    validateSecurity(path);
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

std::string readText(const fs::path &path, size_t limit) {
    validatePath(path);
    Handle file(CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file)
        fail();
    checkFile(file.get());
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0 ||
        uint64_t(size.QuadPart) > limit)
        fail(ERROR_INVALID_DATA);
    std::string result(size_t(size.QuadPart), '\0');
    DWORD count = 0;
    if (!ReadFile(file.get(), result.data(), DWORD(result.size()), &count,
                  nullptr) ||
        count != result.size())
        fail(ERROR_READ_FAULT);
    return result;
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
