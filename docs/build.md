# Build, Deployment and Validation

The recommended AMD64 Release workflow is shown in the
[README](../README.md#build-and-deploy). This guide covers prerequisites,
custom build paths, installer packaging, validation and input-method registration.

## Installer Packages

The CI/Nightly workflow packages Windows 11 x64 installers with and without
bundled Visual C++/Windows App Runtime installers. See
[Install in the README](../README.md#install) for package selection and removal,
and [installer implementation](windows-installer-design.md) for current behavior
and unverified system-installation scenarios. The build/deploy and registration
scripts in this guide remain separate development tools.

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
runtime under `build/all/prefix`, runs the thirteen default TSF CTest tests, then
copies the runtime to `dist/pinyin`. Repeating the command uses incremental builds.
It does not register/unregister TSF, stop applications or start services.

| Option | Default / Behavior |
| --- | --- |
| `-Prefix` | `dist/pinyin`; target runtime tree, must be inside the repository |
| `-BuildRoot` | `build/all`; Core/libime/addons/TSF build and staging parent inside the repository; must not overlap `-Prefix` |
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

Settings uses the independent MSBuild output/cache under `win32/build/settings`,
then copies its runtime files into the staging prefix. `-BuildRoot` does not
relocate that MSBuild cache.

Precompiled data is shared in `build/pinyin-data`. Before downloading, the script
also checks local `libime*.pkg.tar.zst` archives in `build/deps` and
`build/pinyin-data`, including `build/deps/libime-data.pkg.tar.zst`. Only an
archive matching the locked SHA256 is reused. A valid `-DataArchive` fills the
same standard cache; extracted or deployed data alone does not establish its
version. Source-data generation has not been validated end to end locally.

For individual Core, TSF and Settings builds, see the
[Pinyin integration guide](pinyin.md#build-and-deploy-core). A plain root CMake
build does not build/deploy libime, chinese-addons, TSF, Settings or Pinyin data.

## CI Build and Artifact Flow

The workflow builds each x64 component once in Release. Build and validation
run in separate jobs:

| Job | Responsibility |
| --- | --- |
| `build-core-arm64` | Validate ARM64 Core compilation; no ARM64 TSF or Pinyin integration validation |
| `build-x64` | Run the unified build with `-SkipTests`; upload the complete runtime tree, portable TSF tests/probes and standalone Settings artifact |
| `test-x64` | Download the x64 runtime and tests; run build-script regressions, the existing icon checksum, Pinyin/TSF/settings probes, default CTest and dictionary integration checks |
| `package-installer-x64` | Download the same runtime, run packaging-script regressions and build installers with and without bundled runtimes; no component recompilation |
| `installer-smoke-x64` | Download the no-runtime x64 installer, run a silent machine-only install and uninstall, and upload the installer log |
| `release` | Wait for both architectures, x64 tests, packaging and installer smoke test to succeed, then publish the installers and SHA256 files |

`test-x64` and `package-installer-x64` both depend only on `build-x64`, so they
can start in parallel as soon as that job finishes. Packaging can succeed even
if tests fail, but Nightly publication remains blocked. The standalone Debug
TSF build is no longer part of this workflow.

Nightly publication uses GitHub CLI with job-scoped `contents: write` permission.
Release jobs run one at a time, replace the previous `latest` prerelease and its
assets, and move the tag to the workflow commit. Only the two x64 installers
(with and without bundled runtimes) and their SHA256 files are published, with
generated release notes. Missing asset patterns stop publication before the
previous release is changed. Core developer archives are no longer packaged
or uploaded by CI.

The portable test bundle includes test executables, probes, the actual TSF DLL
and generated icon. A relative-path JSON manifest comes from
`ctest --show-only=json-v1`; the test runner restores CTest registration with its
downloaded bundle's absolute commands and working directories, without
reconfiguring CMake or depending on the original runner's checkout path.

All multi-file artifacts use the artifact action's default ZIP archive mode.
Installer artifacts use compression level zero. Release downloads only the
named installer artifact, without merging the internal runtime and test bundles.

## Build Installer Packages

Prepare a complete isolated runtime tree with the unified build, then package
it with Inno Setup 7 (`-IsccPath` can select the compiler explicitly):

```powershell
./scripts/build-and-deploy.ps1 -Prefix ./dist/installer-stage -BuildRoot ./build/installer
./scripts/build-installer.ps1 -Prefix ./dist/installer-stage -Output ./dist/installer -SkipRuntimes
./scripts/build-installer.ps1 -Prefix ./dist/installer-stage -Dependencies ./build/installer-deps -Output ./dist/installer
```

The bundled variant requires `vc_redist.x64.exe` and
`WindowsAppRuntimeInstall-x64.exe` in `-Dependencies`. Prepare and verify these
against `cmake/installer-lock.json`; CI downloads and checks the locked SHA256
values. The packaging script itself checks presence, not dependency hashes.
`-SkipRuntimes` requires neither installer. Both modes require Inno Setup 7,
the setup helper and the complete Core/TSF/Settings staging tree.

`-PackageVersion` defaults to `0.1.0.0`; `-PackageName` overrides the output
basename. Each variant produces an EXE, `.exe.sha256` and `.manifest.txt` with
version, commit, runtime-bundling mode and compiler version. All input/output
paths must be inside the repository.

Packaging copies license files and writes `setup/managed-install` into the
staging tree. Use a dedicated packaging prefix: that marker makes service startup
require installer-managed state, so do not package an active development tree.
`-WhatIf` validates required input files without compiling or changing the tree.
Packaging does not register TSF, start services or execute runtime installers.
The script regression can run without system installation:

```powershell
./scripts/test-build-installer.ps1
```

The GitHub Actions `installer-smoke-x64` job executes the no-runtime package on
a fresh Windows runner with `/VERYSILENT /MACHINEONLY=1`, verifies the installed
files, machine registration and managed install state, then invokes the installed
`Fcitx5SetupHelper.exe uninstall --silent` entry point and checks that both
uninstall registrations are removed. The bundled-runtime package is still only
packaged in CI; installing system runtimes and exercising the original-user
configuration path require a dedicated clean Windows VM or self-hosted runner.

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

Default CTest includes `test_candidate` and `test_candidate_tsf`, which exercise the production
DirectWrite/Direct2D draw function and popup lifecycle without Core or TIP
registration. See [candidate rendering validation](windows-candidate-rendering.md#verification)
for preview images and the remaining physical-monitor/application checks.
Enabling `ENABLE_PINYIN_INTEGRATION_TESTS` adds three Core probes to the thirteen
default tests, for sixteen total; it requires a running deployed Core.

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

For installer-managed copies, use the Start menu or Windows installed-apps
uninstall entry so user configuration, maintenance and file removal are handled
together. The helpers below only register/unregister development deployments.

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

Real unregistration requires an elevated PowerShell. The helper unregisters
TSF by default; `-SkipUnregister` skips that step and its elevation check.
`-Force` also terminates
processes verified to load the exact DLL path, potentially including editors,
browsers and Explorer; save work before using it. Owners whose paths cannot be
verified are not terminated. It does not stop Core/Settings as services; use the
input indicator menu first. Closing applications or choosing an isolated build
directory is sufficient for ordinary rebuilds.

Use `-HandlePath` if Sysinternals Handle is not in `PATH`; otherwise the helper
falls back to `tasklist /m` and accessible module lists. `-WhatIf` requires no
elevation, does not invoke Handle or accept its EULA, and makes no registration
or process changes.
