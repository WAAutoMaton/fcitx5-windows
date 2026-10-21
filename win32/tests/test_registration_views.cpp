#include "../dll/register.h"
#include "../dll/util.h"
#include <cassert>
#include <map>

namespace {
struct Entry {
    REGSAM view;
    std::wstring path;
};
std::map<HKEY, Entry> handles;
std::map<REGSAM, std::wstring> servers;
uintptr_t nextHandle = 10;
LSTATUS failure = ERROR_SUCCESS;

LSTATUS WINAPI createKey(HKEY parent, LPCWSTR path, DWORD, LPWSTR, DWORD,
                         REGSAM access, LPSECURITY_ATTRIBUTES, PHKEY result,
                         LPDWORD) {
    if (failure)
        return failure;
    const auto view = access & (KEY_WOW64_32KEY | KEY_WOW64_64KEY);
    assert(view == KEY_WOW64_32KEY || view == KEY_WOW64_64KEY);
    if (parent != HKEY_LOCAL_MACHINE)
        assert(handles.at(parent).view == view);
    *result = reinterpret_cast<HKEY>(++nextHandle);
    handles[*result] = {view, path};
    return ERROR_SUCCESS;
}
LSTATUS WINAPI openKey(HKEY parent, LPCWSTR path, DWORD options, REGSAM access,
                       PHKEY result) {
    return createKey(parent, path, options, nullptr, 0, access, nullptr, result,
                     nullptr);
}
LSTATUS WINAPI setValue(HKEY key, LPCWSTR name, DWORD, DWORD type,
                        const BYTE *bytes, DWORD size) {
    assert(type == REG_SZ && size >= sizeof(wchar_t));
    if (handles.at(key).path == L"InprocServer32" && !name) {
        const auto value = reinterpret_cast<const wchar_t *>(bytes);
        assert(value[size / sizeof(wchar_t) - 1] == 0);
        servers[handles.at(key).view] = value;
    }
    return ERROR_SUCCESS;
}
LSTATUS WINAPI deleteTree(HKEY key, LPCSTR path) {
    assert(path == fcitx::guidToString(fcitx::FCITX_CLSID));
    servers.erase(handles.at(key).view);
    return ERROR_SUCCESS;
}
LSTATUS WINAPI closeKey(HKEY key) {
    assert(handles.erase(key) == 1);
    return ERROR_SUCCESS;
}
} // namespace

#define RegCreateKeyExW createKey
#define RegOpenKeyExW openKey
#define RegSetValueExW setValue
#define RegDeleteTreeA deleteTree
#define RegCloseKey closeKey
#include "../dll/register.cpp"

int main() {
    const auto root = HKEY_LOCAL_MACHINE;
    const std::wstring x64 = L"C:/Fcitx5/tsf/fcitx5-x86_64.dll";
    const std::wstring x86 = L"C:/Fcitx5/tsf/x86/fcitx5-x86.dll";
    assert(fcitx::RegisterServerAt(root, x64, KEY_WOW64_64KEY) == S_OK);
    assert(fcitx::RegisterServerAt(root, x86, KEY_WOW64_32KEY) == S_OK);
    assert(servers.at(KEY_WOW64_32KEY) == x86);
    assert(servers.at(KEY_WOW64_64KEY) == x64);
    assert(fcitx::UnregisterServerAt(root, KEY_WOW64_32KEY) == S_OK);
    assert(!servers.contains(KEY_WOW64_32KEY));
    assert(servers.at(KEY_WOW64_64KEY) == x64);
    assert(fcitx::RegisterServerAt(root, x86, KEY_WOW64_32KEY) == S_OK);
    assert(fcitx::UnregisterServerAt(root, KEY_WOW64_64KEY) == S_OK);
    assert(servers.at(KEY_WOW64_32KEY) == x86);
    assert(fcitx::RegisterServerAt(root, x64, 0) == E_INVALIDARG);
    assert(fcitx::UnregisterServerAt(nullptr) == E_INVALIDARG);
    failure = ERROR_ACCESS_DENIED;
    assert(fcitx::RegisterServerAt(root, x64) == HRESULT_FROM_WIN32(failure));
    assert(fcitx::UnregisterServerAt(root) == HRESULT_FROM_WIN32(failure));
    assert(handles.empty());
}
