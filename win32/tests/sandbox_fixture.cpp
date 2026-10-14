#include <windows.h>

namespace {
ULONG64 processFlags = 1;
LONG processStatus = 0;
} // namespace

extern "C" __declspec(dllexport) void WINAPI SetSandboxState(ULONG64 flags,
                                                             LONG status) {
    processFlags = flags;
    processStatus = status;
}
extern "C" __declspec(dllexport) LONG WINAPI
SbieApi_QueryProcess(HANDLE, WCHAR *box, WCHAR *, WCHAR *, ULONG *) {
    if (processStatus)
        return processStatus;
    if (box)
        lstrcpyW(box, L"FixtureBox");
    return 0;
}
extern "C" __declspec(dllexport) ULONG64 WINAPI
SbieApi_QueryProcessInfo(HANDLE, ULONG) {
    return processFlags;
}
