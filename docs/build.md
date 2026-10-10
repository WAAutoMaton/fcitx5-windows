# Build, Deployment and Validation

The recommended AMD64 Release workflow is shown in the
[README](../README.md#build-and-deploy). This guide covers prerequisites,
custom build paths, validation and input-method registration.

## Prerequisites

Install the following before running the build scripts:

| Component | Requirements |
| --- | --- |
| Shared tools | CMake 3.27+, Git and PowerShell |
| Core/Pinyin | MSYS2 clang64 Clang, Ninja, pkgconf, ECM, dlfcn, libuv, gettext, Boost headers/iostreams and zstd |
| TSF | Standalone LLVM Clang/clang++/llvm-rc, Windows SDK, ATL and ImageMagick (`magick`) |
| Settings | Visual Studio MSVC x64 tools, MSBuild, NuGet and Windows SDK 10.0.22000.0+ |
| Settings runtime | x64 Windows App SDK 1.8 Framework/DDLM >= `8000.994.2142.0` and Visual C++ runtime for the current user |

The default tool locations are `C:/msys64` and `C:/Program Files/LLVM`.
`cmake`, `git` and `magick` must be available in `PATH`. The unified script uses
MSYS2's Ninja and initializes the Visual Studio x64 development environment
itself, so a normal PowerShell terminal is sufficient.

Core uses the MSYS2 Windows GNU runtime; TSF uses the Windows SDK/ATL toolchain.
Boost/iostreams and zstd must match the clang64 target/runtime. These environments
are separate and cannot be substituted for each other. Scripts do not install
or upgrade tools or system runtimes.

## Unified Build Options

Run from the repository root:

```powershell
./scripts/build-and-deploy.ps1
```

The script initializes pinned submodules, applies compatibility patches and
builds Core/libime/chinese-addons, TSF and WinUI Settings. It stages the complete
runtime under `build/all/prefix`, runs the eight default TSF CTest tests, then
copies the runtime to `dist/pinyin`. Repeating the command uses incremental builds.
It does not register/unregister TSF, stop applications or start services.

| Option | Default / Behavior |
| --- | --- |
| `-Prefix` | `dist/pinyin`; target runtime tree, must be inside the repository |
| `-BuildRoot` | `build/all`; build/staging directory inside the repository, separate from `-Prefix` |
| `-MSYS2Root` | `C:/msys64` |
| `-LLVMRoot` | `C:/Program Files/LLVM`; standalone LLVM with `clang`, `clang++`, `llvm-rc` |
| `-DependencyPrefix` | Optional matching clang64 Boost/zstd prefix; uses `build/deps/clang64` if present |
| `-DataMode` | `Prebuilt`; `Source` generates data locally and takes more time/memory |
| `-DataArchive` | Optional local prebuilt archive, checked against the locked SHA256 |
| `-Jobs` | `6`; parallel jobs for CMake builds |
| `-SkipTests` | Skip default CTest tests |
| `-WhatIf` | Preview without builds, submodule initialization or file copies |

Default paths are resolved relative to the repository; explicit relative paths
are resolved from the current working directory. For an isolated deployment:

```powershell
./scripts/build-and-deploy.ps1 -Prefix ./dist/next
./scripts/build-and-deploy.ps1 -WhatIf
```

Precompiled data is shared in `build/pinyin-data`. Before downloading, the script
also checks local `libime*.pkg.tar.zst` archives in `build/deps` and
`build/pinyin-data`, including `build/deps/libime-data.pkg.tar.zst`. Only an
archive matching the locked SHA256 is reused. A valid `-DataArchive` fills the
same standard cache; extracted or deployed data alone does not establish its
version. Source-data generation has not been validated end to end locally.

For individual Core, TSF and Settings builds, see the
[Pinyin integration guide](pinyin.md#build-and-deploy-core). A plain root CMake
build does not build/deploy libime, chinese-addons, TSF, Settings or Pinyin data.

## Deployment and Updates

Keep the complete runtime tree together:

```text
dist/pinyin/
  bin/Fcitx5.exe
  tsf/fcitx5-x86_64.dll
  settings/Fcitx5Settings.exe
  lib/
  share/
```

TSF locates Core at the fixed adjacent `bin/Fcitx5.exe` path; Core locates
Settings at `settings/Fcitx5Settings.exe`. A DLL registered from a development
build directory cannot infer this runtime layout. Core must remain in `bin`
for resource path calculation. Rebuild and deploy all three components together
after an IPC protocol upgrade.

Before updating an existing deployment, use `关闭服务` in the input indicator
and close applications loading its DLL. Locked or read-only target/staging files
cause the build script to exit. Choose another `-Prefix` to build while the
current deployment is running. The final file copy is not atomic; a process
opening a target file during copying can still interrupt deployment.

If Settings cannot open, choose `重启服务` if services were manually stopped,
then check the executable path and Windows App SDK/Visual C++ runtime. A
successful `ipc_probe --ready` confirms the Core connection but does not prove
Settings can start. See [settings diagnostics](pinyin.md#windows-settings).

## Validation

The unified build runs default TSF CTest only. It does not automatically run
Core integration probes or interactive WinUI checks. With no existing Core or
Settings process, use the unified build's TSF directory:

```powershell
./scripts/test-pinyin.ps1 -TsfBuild ./build/all/tsf
./scripts/test-dictionaries.ps1 -TsfBuild ./build/all/tsf
./scripts/test-service.ps1 -TsfBuild ./build/all/tsf -WithSettings
./scripts/test-settings-ui.ps1 -TsfBuild ./build/all/tsf
```

Supply `-Prefix` if the deployment is not `dist/pinyin`. These scripts start/stop
their own service processes and do not register the input method. The UI checks
require an interactive desktop and installed Settings runtimes. See
[probe coverage and limits](pinyin.md#build-and-test-tsf) for the distinction
between actual TSF contexts, simulated callbacks and real OS dispatch.

The following script regressions do not perform real registration or terminate
applications:

```powershell
./scripts/test-build-and-deploy.ps1
./win32/tests/test_scripts.ps1
```

The first checks build/deploy paths, preview behavior and locked/read-only file
rejection without building. The second checks registration/release helpers
with mocked system operations and requires the default deployed DLL to exist.

## Registration and DLL Management

Install/uninstall helpers default to `dist/pinyin/tsf/fcitx5-x86_64.dll`, resolved
relative to the repository regardless of the current directory. Use `-Prefix`
for another runtime tree or `-DllPath` for an explicit DLL; these are mutually
exclusive. Preview before making system changes:

```powershell
./win32/scripts/install.ps1 -WhatIf
./win32/scripts/install.ps1
./win32/scripts/uninstall.ps1 -WhatIf
```

Real registration/unregistration requests administrator privileges when needed,
waits for completion and reports errors. It does not copy files, install system
runtimes, stop services or unload DLLs from running applications. Re-register
after changing profile/category/icon metadata and reopen applications to load
the updated DLL.

The input mode item is a TSF Language Bar item; visibility depends on Windows
language-bar settings. Registered right-click dispatch still needs validation.
The menu's service commands use graceful shutdown and report timeouts for hung
processes. Restart discards unfinished preedit and settings drafts; stopping
services pauses automatic startup for the current login until a restart.

For diagnosing or releasing a loaded DLL, preview the release helper:

```powershell
./win32/scripts/release-tsf.ps1 -Force -WhatIf
./win32/scripts/release-tsf.ps1 -Prefix "$PWD/dist/another" -Force -WhatIf
```

Without `-WhatIf`, this helper requires an elevated PowerShell and unregisters
TSF by default. `-SkipUnregister` skips that step. `-Force` also terminates
processes verified to load the exact DLL path, potentially including editors,
browsers and Explorer; save work before using it. Owners whose paths cannot be
verified are not terminated. It does not stop Core/Settings as services; use the
input indicator menu first. Closing applications or choosing an isolated build
directory is sufficient for ordinary rebuilds.

Use `-HandlePath` if Sysinternals Handle is not in `PATH`; otherwise the helper
falls back to `tasklist /m` and accessible module lists. `-WhatIf` requires no
elevation, does not invoke Handle or accept its EULA, and makes no registration
or process changes.
