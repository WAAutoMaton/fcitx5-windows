#include <Windows.h>

extern "C" __declspec(dllexport) int upgradeFixture() { return 10; }

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
