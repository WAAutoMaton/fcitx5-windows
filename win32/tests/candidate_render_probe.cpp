#include "../ipc/pipeidentity.h"
#include "../tsf/candidatewindow.h"
#include <cstdio>
#include <string>
#include <userenv.h>
#include <vector>

namespace fcitx {
struct CandidateWindowTestAccess {
    static HWND window(const CandidateWindow &view) { return view.window_; }
    static bool painted(const CandidateWindow &view) {
        return view.target_ != nullptr || view.dcTarget_ != nullptr;
    }
    static void useDc(CandidateWindow &view) { view.useDcTarget_ = true; }
};
} // namespace fcitx

namespace {
bool candidatePixels(HWND window) {
    RECT client{};
    if (!GetClientRect(window, &client))
        return false;
    const auto dc = GetDC(window);
    if (!dc)
        return false;
    const auto color = GetPixel(dc, client.right - 8, client.bottom / 2);
    ReleaseDC(window, dc);
    return color == GetSysColor(COLOR_HIGHLIGHT);
}

HRESULT render(const std::wstring &mode, bool appContainer) {
    const auto environment = fcitx::win32::ipc::clientEnvironment();
    if (!environment.tokenKnown || environment.appContainer != appContainer)
        return E_ACCESSDENIED;
    const auto window =
        CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC",
                        L"Candidate render probe", WS_POPUP, 100, 100, 240, 160,
                        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window)
        return HRESULT_FROM_WIN32(GetLastError());
    ShowWindow(window, SW_SHOWNOACTIVATE);
    HRESULT result = E_FAIL;
    if (mode == L"candidate" || mode == L"candidate-dc") {
        fcitx::CandidateWindow view;
        if (mode == L"candidate-dc")
            fcitx::CandidateWindowTestAccess::useDc(view);
        fcitx::PipeClient::KeyReply reply;
        reply.selected = 0;
        reply.candidates = {{"Candidate", "1", "render probe"}};
        view.show(reply, window, {100, 100, 101, 120});
        result = view.visible() &&
                         fcitx::CandidateWindowTestAccess::painted(view) &&
                         candidatePixels(
                             fcitx::CandidateWindowTestAccess::window(view))
                     ? S_OK
                     : E_FAIL;
    } else {
        CComPtr<ID2D1Factory> factory;
        result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory);
        if (SUCCEEDED(result)) {
            CComPtr<ID2D1RenderTarget> target;
            HDC dc = nullptr;
            if (mode == L"dc") {
                const auto properties = D2D1::RenderTargetProperties(
                    D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                      D2D1_ALPHA_MODE_IGNORE));
                CComPtr<ID2D1DCRenderTarget> dcTarget;
                result = factory->CreateDCRenderTarget(&properties, &dcTarget);
                if (SUCCEEDED(result)) {
                    dc = GetDC(window);
                    RECT client{};
                    GetClientRect(window, &client);
                    result = dcTarget->BindDC(dc, &client);
                    target = dcTarget;
                }
            } else {
                const auto properties = D2D1::RenderTargetProperties(
                    mode == L"hwnd-software" ? D2D1_RENDER_TARGET_TYPE_SOFTWARE
                                             : D2D1_RENDER_TARGET_TYPE_DEFAULT);
                CComPtr<ID2D1HwndRenderTarget> hwndTarget;
                result = factory->CreateHwndRenderTarget(
                    properties,
                    D2D1::HwndRenderTargetProperties(
                        window, D2D1::SizeU(240, 160),
                        D2D1_PRESENT_OPTIONS_IMMEDIATELY),
                    &hwndTarget);
                target = hwndTarget;
            }
            if (SUCCEEDED(result)) {
                target->BeginDraw();
                target->Clear(D2D1::ColorF(D2D1::ColorF::Red));
                result = target->EndDraw();
            }
            if (dc)
                ReleaseDC(window, dc);
        }
    }
    DestroyWindow(window);
    return result;
}

DWORD launch(const wchar_t *executable, const wchar_t *mode, PSID appSid) {
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<BYTE> storage(size);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &size))
        return HRESULT_FROM_WIN32(GetLastError());
    SECURITY_CAPABILITIES capabilities{};
    capabilities.AppContainerSid = appSid;
    if (appSid && !UpdateProcThreadAttribute(
                      list, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                      &capabilities, sizeof(capabilities), nullptr, nullptr)) {
        const auto error = GetLastError();
        DeleteProcThreadAttributeList(list);
        return HRESULT_FROM_WIN32(error);
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = list;
    PROCESS_INFORMATION process{};
    auto command = L"\"" + std::wstring(executable) + L"\" --child " + mode +
                   (appSid ? L" app" : L" desktop");
    const auto launched =
        CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE,
                       EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr,
                       nullptr, &startup.StartupInfo, &process);
    const auto error = GetLastError();
    DeleteProcThreadAttributeList(list);
    if (!launched)
        return HRESULT_FROM_WIN32(error);
    DWORD result = E_FAIL;
    if (WaitForSingleObject(process.hProcess, 15000) == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &result);
    } else {
        TerminateProcess(process.hProcess, ERROR_TIMEOUT);
        WaitForSingleObject(process.hProcess, 5000);
        result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return result;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc == 4 && std::wstring(argv[1]) == L"--child") {
        const auto initialized =
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(initialized))
            return initialized;
        const auto result = render(argv[2], std::wstring(argv[3]) == L"app");
        CoUninitialize();
        return result;
    }
    if (argc != 2) {
        std::puts("Pass an existing AppContainer profile/package family name. "
                  "No profile is created.");
        return 1;
    }
    PSID appSid = nullptr;
    if (FAILED(DeriveAppContainerSidFromAppContainerName(argv[1], &appSid)))
        return 1;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    bool passed = true;
    for (bool restricted : {false, true}) {
        for (const auto mode : {L"hwnd-default", L"hwnd-software", L"dc",
                                L"candidate", L"candidate-dc"}) {
            const auto result =
                launch(executable, mode, restricted ? appSid : nullptr);
            std::wprintf(L"appContainer=%u mode=%ls hr=0x%08lX\n", restricted,
                         mode, result);
            if (std::wstring(mode) == L"candidate" ||
                std::wstring(mode) == L"candidate-dc" ||
                std::wstring(mode) == L"dc")
                passed = passed && SUCCEEDED(static_cast<HRESULT>(result));
        }
    }
    FreeSid(appSid);
    return passed ? 0 : 1;
}
