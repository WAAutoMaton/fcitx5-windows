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
| Core/input methods | MSYS2 clang64 Clang, Ninja, pkgconf, ECM, dlfcn, libuv, gettext, Boost headers/iostreams and zstd; the pinned libime/chinese-addons data supplies Pinyin, Shuangpin and table methods |
| TSF | Standalone LLVM Clang/clang++/llvm-rc, Windows SDK, ATL and ImageMagick (`magick`) |
| Settings | Visual Studio MSVC x64 tools, MSBuild, NuGet and Windows SDK 10.0.22000.0+ |
| Settings runtime | x64 Windows App SDK 1.8 Framework/DDLM >= `8000.994.2142.0` and Visual C++ runtime for the current user |
| UI localization | Boost 1.92 portable headers, gettext tools; the SHA256-pinned Boost.Locale 1.92 source archive is downloaded by CMake |

The default tool locations are `C:/msys64` and `C:/Program Files/LLVM`.
`cmake`, `git` and `magick` must be available in `PATH`. The unified script uses
MSYS2's Ninja and initializes the Visual Studio x64 development environment
itself, so a normal PowerShell terminal is sufficient.

Core uses the MSYS2 Windows GNU runtime; TSF uses the Windows SDK/ATL toolchain.
The standalone `win32` project uses static MSVC CRT (`/MT`, `/MTd` for Debug)
throughout the TIP, i18n/Boost.Locale libraries, Setup Helper and test hosts.
Settings builds its own i18n library with its existing dynamic CRT configuration.
TSF deployment checks normal and delayed PE imports using `llvm-readobj` and
rejects dynamic MSVC/UCRT dependencies even when `-SkipTests` is supplied.
Boost/iostreams and zstd must match the clang64 target/runtime. These environments
are separate and cannot be substituted for each other. Scripts do not install
or upgrade tools or system runtimes.

Each toolchain compiles Boost.Locale's message/encoding components separately.
Native builds copy only the portable `boost` headers into their own include
directory; they do not include GNU toolchain headers or link MSYS2 Boost DLLs.
ICU and Boost.Thread binaries are not needed for this message-only integration.
Custom headers can be supplied with `-DNATIVE_BOOST_HEADERS=<include directory>`
or `build-settings.ps1 -BoostHeaders <include directory>`; version mismatches fail
configuration. An offline Boost.Locale checkout can be supplied with
`-BoostSource <source directory>` to the build scripts.

## Unified Build Options

Run from the repository root:

```powershell
./scripts/build-and-deploy.ps1
```

The script initializes pinned submodules, applies compatibility patches and
builds Core/libime/chinese-addons, TSF and WinUI Settings. The resulting runtime
contains Pinyin, Shuangpin and the available chinese-addons table entries
(Wubi, Wubi Pinyin, Cangjie, Dianbaoma, Erbi, Bingchan, Wanfeng and Ziranma).
It stages the complete
runtime under `build/all/prefix`, runs the default TSF CTest suite, then
copies the runtime to `dist/pinyin`. Repeating the command uses incremental builds.
It does not register/unregister TSF, stop applications or start services.

| Option | Default / Behavior |
| --- | --- |
| `-Prefix` | `dist/pinyin`; target runtime tree, must be inside the repository |
| `-BuildRoot` | `build/all`; Core/libime/addons/TSF build and staging parent inside the repository; must not overlap `-Prefix` |
| `-MSYS2Root` | `C:/msys64` |
| `-LLVMRoot` | `C:/Program Files/LLVM`; standalone LLVM with `clang`, `clang++`, `llvm-rc` |
| `-DependencyPrefix` | Optional matching clang64 Boost/zstd prefix; uses `build/deps/clang64` if present |
| `-BoostSource` | Optional local Boost.Locale 1.92 source directory for offline builds; shared source, separate build caches |
| `-DataMode` | `Prebuilt`; `Source` generates data locally and takes more time/memory |
| `-DataArchive` | Optional local prebuilt archive, checked against the locked SHA256 |
| `-Jobs` | `6`; parallel jobs for CMake builds |
| `-SkipTests` | Skip default CTest tests |
| `-ModernApps` | Explicitly declare immersive/UIElement TSF categories for system acceptance; no registration or signing |
| `-WhatIf` | Preview without builds, submodule initialization or file copies |

Default paths are resolved relative to the repository; explicit relative paths
are resolved from the current working directory. For an isolated deployment:

```powershell
./scripts/build-and-deploy.ps1 -Prefix ./dist/next
./scripts/build-and-deploy.ps1 -WhatIf
```

The unified build places Settings' independent MSVC translation-library build,
MSBuild output and object files under `BuildRoot/settings`, then stages them.
Standalone `build-settings.ps1` defaults to `win32/build/settings` and also accepts
`-BuildRoot`. NuGet restore metadata stays in its existing project cache.

Windows translations are authored in `po/zh_CN.po` and `po/zh_TW.po` and compiled
to `share/locale/<language>/LC_MESSAGES/fcitx5-windows.mo`; English falls back to
source strings. Run `scripts/update-translations.ps1` when adding messages, or
use `-ExtractOnly` to update just the POT. Normal builds do not rewrite PO/POT.
The isolated runtime also includes `licenses/Boost-1.92-LICENSE.txt`.

Precompiled data is shared in `build/pinyin-data`. Before downloading, the script
also checks local `libime*.pkg.tar.zst` archives in `build/deps` and
`build/pinyin-data`, including `build/deps/libime-data.pkg.tar.zst`. Only an
archive matching the locked SHA256 is reused. A valid `-DataArchive` fills the
same standard cache; extracted or deployed data alone does not establish its
version. Source-data generation has not been validated end to end locally.

For individual Core, TSF and Settings builds, see the
[input-method integration guide](pinyin.md#build-and-deploy-core). A plain root
CMake build does not build/deploy libime, chinese-addons, TSF, Settings or input
method data.

## Developer Logging

In Settings, open **Global Settings > Developer options** and enable detailed
logging, then apply. Core applies the change immediately; TSF learns the setting
from its next Core reply. Core, TSF and Settings must be deployed together using
IPC v10. `windows-ui.conf` now uses version 2; version 1 loads with logging off
and migrates on the next successful save. Language and logging share a revision.

Logs are JSON Lines under `%LOCALAPPDATA%/Fcitx5/logs`, split by role and TSF or
Settings PID. The files rotate at 8 MiB and UTC date changes, expire after 7
days, and are bounded to 64 MiB per process and 512 MiB per directory. Active
files in other processes are protected from deletion by Windows sharing rules;
capacity cleanup retries on subsequent maintenance. Disabling retains history.

Settings records window open/close, close-button/service-stop requests, dispatcher
return, runtime shutdown and a final `settings_exit` with a reason and exit code.
Page rendering and user operations have begin/stage/end events correlated by
`operation_id`. `settings_exception` includes the operation, exception kind,
numeric HRESULT (when available) and whether the exception is fatal. XAML
unhandled exceptions and `std::terminate` are also recorded. Exception messages,
configuration values and input text are excluded. These events require detailed
logging to be enabled; disabling it also disables exit diagnostics.

Fatal C++/WinRT callback errors attempt to drain the log queue for 250 ms before
preserving the existing fail-fast behavior. These are best-effort diagnostics:
native access violations, stack/heap corruption, external termination, and a
blocked/unavailable logger can leave no final event. A missing `settings_exit`
is a reason to inspect Windows Application Error/WER records, not proof of a
crash. `settings_window_closed` followed by `settings_exit` distinguishes a
completed shutdown from a failure during teardown.

Build isolated Settings with `scripts/build-settings.ps1 -UiTest -BuildRoot <build>`
and run `scripts/test-settings-exit-logging.ps1 -SettingsBuild <build>/Release`
to check normal exits, recoverable HRESULTs, real async fail-fast and
`std::terminate`, privacy and logging-off behavior. This test uses dedicated
child processes and log directories without connecting to Core or stopping any
running Settings instance. Its fatal cases intentionally terminate test children.

Events contain diagnostic metadata, HRESULT/error codes, counts, lengths and
geometry. TSF key results include scan codes, modifiers and consumption state,
but not translated characters/Unicode. Input text, candidate contents, raw
payloads, titles and user paths are excluded. Upstream Fcitx messages are
represented only by severity and byte
count; no addon Debug categories are enabled, and `key_trace` stays disabled.
The background worker pins the DLL until its thread-pool callback returns.

Run `test_diagnostic_log`, `test_protocol`, `test_i18n`, `test_candidate` and
`test_candidate_tsf` with CTest. For integration, compile Core, TSF and probes with
`-DCMAKE_CXX_FLAGS=-DFCITX5_UI_TEST`, and Settings with `build-settings.ps1 -UiTest`.
Use `scripts/test-developer-logging.ps1 -Prefix <isolated-prefix> -TsfBuild <build>`
for real engine/IPC and unregistered TSF checks, and
`scripts/test-ui-localization.ps1 -SettingsBuild <build> -TsfBuild <build> -TestLogging`
for the developer page. These runners set `FCITX5_DIAGNOSTIC_TEST_DIRECTORY` to
an isolated directory; production builds ignore that variable. They do not
register TSF or validate real application dispatch, physical DPI or installers.

See [developer logging design and event scope](windows-developer-logging-design.md)
for the shared logger, privacy boundaries and Settings exit diagnostics.

## Settings Shortcut Recording

Engine key-list options have a record button alongside the editable text list.
Recording changes the draft; use **Apply** to save it through Core. The UI keeps
manual input for unsupported keys and layouts. See
[shortcut behavior](windows-settings-design.md#快捷键录制).

Build Settings with `scripts/build-settings.ps1 -UiTest -BuildRoot <build>` and
the TSF `ui_fixture` target, then run on an interactive desktop:

```powershell
./scripts/test-settings-keycapture.ps1 -SettingsExe <build>/Release/Fcitx5Settings.exe -FixtureExe <tsf-build>/tests/ui_fixture.exe
```

The runner uses isolated test endpoints and configuration and sends synthetic
keyboard input to the recorder. It does not register TSF or change the user's
engine settings. `test_keycapture` covers key conversion without WinUI; neither
test establishes support for every keyboard layout or IME interaction.

## Modern App Acceptance

The runtime implementation is described in
[windows-modern-apps-design.md](windows-modern-apps-design.md). A modern acceptance
build uses `build-and-deploy.ps1 -ModernApps` with an isolated prefix/build root.
CI/Nightly explicitly enables this option for the shared runtime and both
installer variants, so installation declares `UIELEMENTENABLED` and
`IMMERSIVESUPPORT`. Local builds keep the categories disabled unless the option
is supplied. Changing a build option or replacing the DLL does not update
existing category registration; install the updated package or explicitly
re-register the updated DLL. Re-registration, signing, package installation and
login startup remain explicit operations.

The installer smoke test requires both modern categories in both registry
indexes, the installed COM DLL path/threading model, and an enabled language
profile referencing the installed icon. Missing declarations block Nightly
publication even if the unregistered probes pass. These checks establish
registration only: CI does not currently sign the TSF DLL or validate Windows
dispatch into a real modern application. A signed deployment and interactive
system acceptance are still required to establish modern-app compatibility.

For transport diagnostics, build `appcontainer_probe` and pass an existing
AppContainer profile/package family name:

```powershell
./build/modern-apps/tsf/tests/appcontainer_probe.exe Microsoft.WindowsCalculator_8wekyb3d8bbwe
```

The probe launches real x64 restricted tokens using the existing profile without
creating or altering a profile. It compares global and `LOCAL\` names, desktop
and restricted roundtrips, peer identity, message mode, deadlines and cancellation.
Only isolated diagnostic pipes are created; no Core or TIP registration is used.
Local-name results are diagnostic; the global authenticated path must pass.

For engine/TSF integration while a production Core is running, compile both Core
and the independent TSF/probe project with `-DCMAKE_CXX_FLAGS=-DFCITX5_UI_TEST`.
Stage the real engines/data in the test prefix, then run:

```powershell
./scripts/test-modern-apps.ps1 -Prefix ./build/modern-apps/prefix -TsfBuild ./build/modern-apps/tsf -AppContainerProfile Microsoft.WindowsCalculator_8wekyb3d8bbwe
```

The runner checks the isolated-build markers before starting Core, uses temporary
configuration/data directories, and releases only the process it created. It
tests restricted Pinyin input, actual TSF edit sessions, host-rendered candidate
elements and queued selection/finalize/abort, password scope, denied management
operations, reconnect, and the desktop IPC/settings/TSF probes. This is an
unregistered fixture, not Windows system dispatch into an installed UWP app.

In the initialized Windows SDK/ATL environment, create the unsigned UWP fixture:

```powershell
cmake -S win32 -B build/modern-apps/tsf -DENABLE_UWP_FIXTURE=ON
cmake --build build/modern-apps/tsf --target uwp_fixture_package
```

The `.appx` under `tests/uwp-fixture` contains native UWP TextBox, RichEditBox and
PasswordBox controls with no broad capabilities or full-trust manifest entry.
It is not signed, installed or activated by the build. After explicit package/TIP
installation, verify Win+Space selection, keyboard and host candidate actions,
focus, suspension/resume and Core stop/restart in this fixture and system Settings.
Signed, protected installation acceptance remains separate from unsigned tests.

## CI Build and Artifact Flow

The workflow builds each x64 component once in Release. Build and validation
run in separate jobs:

| Job | Responsibility |
| --- | --- |
| `build-x64` | Run the unified build with `-ModernApps -SkipTests`; upload the complete runtime tree, portable TSF tests/probes and standalone Settings artifact |
| `test-x64` | Download the x64 runtime and tests; run build-script and mocked registration-check regressions, the existing icon checksum, Pinyin/TSF/settings probes, input-method list/Wubi regressions, relocated engine translations, default CTest and dictionary integration checks |
| `package-installer-x64` | Download the same runtime, run packaging-script regressions and build installers with and without bundled runtimes; no component recompilation |
| `installer-smoke-x64` | Download the no-runtime x64 installer, run a silent machine-only install, require modern TSF registration, uninstall and check registration cleanup, and upload the installer log |
| `release` | Wait for the x64 build, tests, packaging and installer smoke test to succeed, then publish the installers and SHA256 files |

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
the setup helper, `setup/Fcitx5Upgrade.exe` and the complete Core/TSF/Settings staging tree.

`-PackageVersion` defaults to `0.1.0.0`; `-PackageName` overrides the output
basename. Each variant produces an EXE, `.exe.sha256` and `.manifest.txt` with
version, commit, runtime-bundling mode and compiler version. All input/output
paths must be inside the repository.

Packaging copies license files and writes `setup/managed-install` into the
staging tree. Use a dedicated packaging prefix: that marker makes service startup
require installer-managed state, so do not package an active development tree.
`-WhatIf` validates required input files without compiling or changing the tree.
Packaging does not register TSF, start services or execute runtime installers.
Packaging also generates a per-file runtime manifest and an Inno file list under
the output's `metadata` directory. The runtime manifest is installed as
`setup/runtime-manifest.json`; maintenance binaries and transaction state are
excluded from that payload.

Deferred reboot upgrades are enabled by default; use `-RebootUpgrade:$false`
to disable this branch for a package.
It stages the entire new runtime while the installed version remains usable,
returns `3010`, and schedules protected machine work for a subsequent full
Windows restart. CI/Nightly uses the same enabled default. Real reboot/VM
acceptance remains outstanding. See [reboot upgrade design](windows-reboot-upgrade-design.md) for
recovery, uninstall coordination and remaining system validation.
The script regression can run without system installation:

```powershell
./scripts/test-build-installer.ps1
./scripts/test-reboot-upgrade-package.ps1 -TsfBuild ./build/installer/tsf
```

The second script compiles both default variants and an explicitly disabled package using fixture binaries;
it never executes an installer or changes system registration.

The GitHub Actions `installer-smoke-x64` job executes the no-runtime package on
a fresh Windows runner with `/VERYSILENT /MACHINEONLY=1`, verifies the installed
files, COM/profile/category registration and managed install state, then invokes
the installed
`Fcitx5SetupHelper.exe uninstall --silent` entry point and checks that both
uninstall registrations and the TSF COM/TIP registration are removed. CI passes
`-ModernApps` to `scripts/test-installer.ps1` to require the modern categories;
omit it only when testing a build that deliberately disables them. The assertion
regression can run locally without installing anything or changing the registry:

```powershell
./scripts/test-installer-registration.ps1
```

The bundled-runtime package is still only packaged in CI; installing system
runtimes and exercising the original-user
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
./scripts/test-inputmethods.ps1 -Prefix ./dist/pinyin -TsfBuild ./build/all/tsf
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
Default CTest also checks the TSF runtime imports and verifies that the audit
rejects fixtures with normal and delayed MSVCP140 imports. The portable runtime
host loads the actual DLL, creates/activates the TIP, exercises pipe mutexes,
translated COM text and translation worker exit, and deactivates/unloads it three
times without registering the TIP or dispatching its Core-start message. LLVM must be installed for the
import checks; it is found in PATH or `C:/Program Files/LLVM`.
Enabling `ENABLE_PINYIN_INTEGRATION_TESTS` adds three Core probes and requires a
running deployed Core.

To test coexistence with an older host runtime, configure a fresh TSF build with
`-DTSF_PRELOAD_RUNTIME="C:/path/to/jbr/bin/MSVCP140.dll"`. This adds
`test_tsf_old_runtime_host`, which verifies the requested DLL was preloaded before
COM/TIP initialization and exercises the same lifecycle. The runtime and its
matching dependencies must already exist; no runtime is downloaded or installed.
This test covers activation and pipe locking, not full Core input or real IDE
keyboard dispatch. Keep fresh Release and Debug build directories separate.

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
