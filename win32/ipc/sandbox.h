#pragma once

#include <system_error>
#include <windows.h>

namespace fcitx::win32::ipc {

enum class SandboxState { Host, Boxed, Unknown };

class SandboxieApi {
  public:
    using QueryProcess = LONG(WINAPI *)(HANDLE, WCHAR *, WCHAR *, WCHAR *,
                                        ULONG *);
    using QueryInfo = ULONG64(WINAPI *)(HANDLE, ULONG);

    SandboxieApi() {
        const auto module = GetModuleHandleW(L"SbieDll.dll");
        present_ = module != nullptr;
        if (module) {
            query_ = reinterpret_cast<QueryProcess>(
                GetProcAddress(module, "SbieApi_QueryProcess"));
            info_ = reinterpret_cast<QueryInfo>(
                GetProcAddress(module, "SbieApi_QueryProcessInfo"));
        }
    }
    SandboxieApi(bool present, QueryProcess query, QueryInfo info)
        : present_(present), query_(query), info_(info) {}

    SandboxState process(DWORD pid) const {
        if (!present_)
            return SandboxState::Host;
        if (!pid || !query_)
            return SandboxState::Unknown;
        WCHAR box[34]{};
        const auto id = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(pid));
        const auto status = query_(id, box, nullptr, nullptr, nullptr);
        // Sandboxie's driver returns STATUS_INVALID_CID for an unboxed PID.
        if (status == static_cast<LONG>(0xc000000b))
            return SandboxState::Host;
        if (status != 0 || !box[0])
            return SandboxState::Unknown;
        if (info_) {
            const auto flags = info_(id, 0);
            if ((flags & 0x80000000ULL) && !(flags & 1))
                return SandboxState::Host; // SBIE_FLAG_HOST_INJECT_PROCESS
            if (!(flags & 1))
                return SandboxState::Unknown;
        }
        return SandboxState::Boxed;
    }
    bool hostOnly() const { return !isHostProcess(); }
    bool isHostProcess(DWORD pid = GetCurrentProcessId()) const {
        return process(pid) == SandboxState::Host;
    }
    bool isSandboxedProcess(DWORD pid = GetCurrentProcessId()) const {
        return process(pid) == SandboxState::Boxed;
    }
    bool isUnknownProcess(DWORD pid = GetCurrentProcessId()) const {
        return process(pid) == SandboxState::Unknown;
    }
    bool acceptsServer(DWORD pid) const {
        return !hostOnly() || process(pid) == SandboxState::Host;
    }

  private:
    bool present_ = false;
    QueryProcess query_ = nullptr;
    QueryInfo info_ = nullptr;
};

} // namespace fcitx::win32::ipc
