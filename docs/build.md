# Build, Deployment and Validation

For the quick-start AMD64 Release workflow, see
[Build and Deploy in the README](../README.md#build-and-deploy). This guide
covers prerequisites, build options, plugin builds, installer packaging, CI,
validation and development registration.

## Installer Packages

The CI/Nightly workflow publishes four x64 installers for Windows 10 version
1809 (build 17763) or later: `base` (`fcitx5-chinese-addons`) and `full` (all
bundled plugins under `plugins/`: `fcitx5-chinese-addons`, `fcitx5-rime`,
`fcitx5-table-extra`, `fcitx5-wenzhounese` and `fcitx5-zhuyin`), each with or
without bundled Visual C++ and Windows App Runtime installers. See
[Install in the README](../README.md#install) and
[installer implementation](windows-installer-design.md).

## Prerequisites

Install the following before running the build scripts (scripts assume these
tools and runtimes are already installed):

| Component | Requirements |
| --- | --- |
| Shared tools | CMake 3.27+, Git and PowerShell |
| Core / input methods | MSYS2 `clang64` Clang, Ninja, pkgconf, ECM, dlfcn, libuv, gettext, Boost headers/iostreams and zstd; pinned libime/`fcitx5-chinese-addons` data supplies Pinyin, Shuangpin and base table methods |
| Full plugin edition | Git LFS; matching `clang64` `mingw-w64-clang-x86_64-librime` and `mingw-w64-clang-x86_64-librime-data` for Rime; `clang64` `glib2` and `db` for Zhuyin |
| TSF | Standalone LLVM (`clang`, `clang++`, `llvm-rc`; LLVM 22+ required with MSVC 14.52+, CI pins `22.1.8`), Windows SDK, x64/x86 MSVC tools and ATL, ImageMagick (`magick`) |
| Settings | Visual Studio MSVC x64 tools, MSBuild, NuGet and Windows SDK 10.0.22000.0+ |
| Settings runtime | x64 Windows App SDK 1.8 Framework/DDLM >= `8000.994.2142.0` and Visual C++ runtime for the current user |
| UI localization | Boost 1.92 portable headers, gettext tools; CMake downloads the SHA256-pinned Boost.Locale 1.92 source archive |

Default tool locations are `C:/msys64` and `C:/Program Files/LLVM`, with
`cmake`, `git` and `magick` in `PATH`. The unified script uses MSYS2's Ninja
and initializes the Visual Studio development environment automatically, so a
normal PowerShell terminal is sufficient.

Toolchain and CRT separation:

- **Core** uses the MSYS2 Windows GNU runtime; Boost/iostreams and zstd must
  match `clang64`.
- **TSF (`win32`)** uses standalone LLVM + Windows SDK/ATL with static MSVC CRT
  (`/MT`, `/MTd` for Debug) across the TIP, i18n/Boost.Locale libraries, Setup
  Helper, Upgrade Helper and test hosts. Deployment audits both normal and
  delayed PE imports via `llvm-readobj` and rejects dynamic MSVC/UCRT
  dependencies (even with `-SkipTests`).
- **Settings (`win32/settings`)** builds its own MSVC i18n library with dynamic
  CRT.
- **Boost.Locale**: Each toolchain compiles the message/encoding components
  separately without ICU or Boost.Thread. Native builds copy only portable
  `boost` headers into their own build directory. Supply custom headers via
  `-DNATIVE_BOOST_HEADERS=<include dir>` or
  `build-settings.ps1 -BoostHeaders <include dir>`, or an offline Boost.Locale
  checkout via `-BoostSource <source dir>`.

## Unified Build Options

Run from the repository root:

```powershell
./scripts/build-and-deploy.ps1
```

The script initializes pinned submodules, applies Windows patches, builds AMD64
Release Core/libime/`fcitx5-chinese-addons`, x64/x86 TSF DLLs, setup/upgrade
helpers and WinUI Settings, stages the runtime under `build/all/prefix`, runs
the default TSF CTest suites and copies the runtime to `dist/pinyin`. Re-running
performs incremental builds.

| Option | Default / Behavior |
| --- | --- |
| `-Prefix` | `dist/pinyin`; target runtime tree inside the repository |
| `-BuildRoot` | `build/all`; build and staging parent inside the repository (must not overlap `-Prefix`) |
| `-MSYS2Root` | `C:/msys64` |
| `-LLVMRoot` | `C:/Program Files/LLVM`; standalone LLVM with `clang`, `clang++`, `llvm-rc` |
| `-DependencyPrefix` | Optional matching `clang64` Boost/zstd prefix; auto-uses `build/deps/clang64` if present |
| `-BoostSource` | Optional local Boost.Locale 1.92 source directory for offline builds |
| `-DataMode` | `Prebuilt`; `Source` generates data locally (requires more time/memory; not validated end to end locally) |
| `-DataArchive` | Optional local prebuilt archive verified against the locked SHA256 |
| `-Jobs` | `6`; parallel CMake build jobs |
| `-SkipTests` | Skip default CTest execution |
| `-ModernApps` | Declare `UIELEMENTENABLED` and `IMMERSIVESUPPORT` TSF categories |
| `-TsfArchitectures` | `x64,x86`; independent frontends sharing x64 Core/Settings (`x64`-only is development-only) |
| `-WhatIf` | Preview actions without modifying files or submodules |

Default paths resolve relative to the repository; explicit relative paths
resolve from the current directory:

```powershell
./scripts/build-and-deploy.ps1 -Prefix ./dist/next
./scripts/build-and-deploy.ps1 -WhatIf
```

### Independent TSF and Settings Builds

TSF builds into `BuildRoot/tsf` (x64) and `BuildRoot/tsf-x86` (x86) using
`cmake/tsf-x64.cmake` and `cmake/tsf-x86.cmake`. Standalone frontend builds
initialize the matching VS environment and reject mismatched cache architectures:

```powershell
./scripts/build-tsf.ps1 -Architecture x86 -BuildRoot ./build/tsf-x86 -ModernApps
./scripts/build-tsf.ps1 -Architecture x64 -BuildRoot ./build/tsf-x64 -ModernApps
```

Deployment audits PE machine types and normal/delayed CRT imports before copying
`tsf/fcitx5-x86_64.dll` and `tsf/x86/fcitx5-x86.dll` into the shared runtime
tree. Both frontends locate Core, Settings, translations and install state from
the same root.

The unified build places Settings' MSVC i18n build and MSBuild artifacts under
`BuildRoot/settings` before staging. Standalone `build-settings.ps1` defaults to
`win32/build/settings` and also accepts `-BuildRoot`.

### Translations and Prebuilt Data

Windows UI translations live in `po/zh_CN.po`, `po/zh_TW.po` and `po/ja.po` and
compile to `share/locale/<lang>/LC_MESSAGES/fcitx5-windows.mo` (English uses
source strings). Run `scripts/update-translations.ps1` when adding messages (or
`-ExtractOnly` for POT only). The runtime includes `licenses/Boost-1.92-LICENSE.txt`.

Precompiled dictionary/model data is cached in `build/pinyin-data`. Before
downloading, the script checks local `libime*.pkg.tar.zst` files in `build/deps`
and `build/pinyin-data` against the locked SHA256 in `cmake/pinyin-lock.json`,
and falls back across `prebuiltData.fallbackUrls` if the primary archive URL
fails or fails checksum verification. CI caches the locked archives under
`build/pinyin-data` and `build/mozc-data`.

For individual Core, TSF and Settings builds, see
[input-method integration](pinyin.md#build-and-deploy-core).

## Independent Input-Method Plugins

Bundled plugin submodules live under `plugins/`: `fcitx5-chinese-addons`,
`fcitx5-mozc`, `fcitx5-rime`, `fcitx5-table-extra`, `fcitx5-wenzhounese` and
`fcitx5-zhuyin`. `fcitx5-chinese-addons` is built by `build-pinyin.ps1` as part
of `base`. To build and deploy every additional plugin against the same build's
SDK:

```powershell
./scripts/build-plugins.ps1 -CorePrefix ./build/all/prefix -Prefix ./dist/pinyin
```

`build-plugins.ps1` discovers direct plugin directories under `plugins/`, runs
`git lfs pull`, builds each CMake project with
`cmake/windows-addon-toolchain.cmake`, audits private DLL imports, copies needed
shared runtime DLLs and installs into `Prefix/plugins/<name>`. It also supports
`-BuildRoot`, `-MSYS2Root`, `-DependencyPrefix`, `-MozcData`, `-Jobs` and
`-WhatIf`. Apply pinned Windows patches (`scripts/apply-pinyin-patches.ps1`)
before standalone plugin builds.

Plugin specifics:

- **`fcitx5-mozc`**: requires Python 3 and initialized nested submodules
  (`mozc`, `abseil-cpp`, `protobuf`). Downloads `mozc.data` into
  `build/mozc-data` (or uses `-MozcData`), checks its dataset magic header
  (plus SHA256 when configured), generates `mozc_data.inc` via `embed_file.py`
  and builds the self-contained in-process `libmozc.dll` with bundled
  dependency licenses.

- **`fcitx5-table-extra`**: resource-only package reusing the base `table`
  engine. The SDK's native `libime_tabledict.exe` compiles all 25 tables
  (including Wubi98, Zhengma, Boshiamy, Cangjie5 and Jyutping Table) into
  `plugins/fcitx5-table-extra`.
- **`fcitx5-rime`**: requires matching `clang64` librime and schema packages;
  copies `share/rime-data` and `opencc` data into `plugins/fcitx5-rime/share/rime-data`.
- **`fcitx5-zhuyin`**: requires `clang64` GLib (`glib2`) and Berkeley DB (`db`).
  `cmake/zhuyin-deps` builds libzhuyin and three native data generators from
  SHA256-pinned libpinyin 2.10.3 sources (`patches/libpinyin-windows.patch`),
  compiles the pinned model archive into `plugins/fcitx5-zhuyin/share/fcitx5/zhuyin`
  and copies required GLib/Berkeley DB DLLs and dependency licenses.

See [Windows plugins](windows-plugins.md) for layout, ABI rules, SDK details and
the isolated `addon_probe` tool. `scripts/test-build-plugins.ps1` verifies
discovery, layout and dependency copying using fixture projects.

## Status Menus

IPC v16 carries the focused context's Fcitx `StatusArea` / `Action/Menu` tree.
Desktop popups and system `ITfMenu` share frozen views; AppContainer input pipes
reject menu queries and actions.

To run isolated status-menu integration tests, build Core and TSF/probes with
`-DCMAKE_CXX_FLAGS=-DFCITX5_UI_TEST` in separate test trees, stage Pinyin and
Rime (with matching librime DLLs, `share/rime-data` and `opencc`), and run:

```powershell
./scripts/test-status-menu.ps1 -Prefix ./build/menu/prefix -TsfBuild ./build/menu/tsf -PeerTsfBuild ./build/menu/tsf-x86 -Rime
```

The runner redirects Windows Fcitx user paths to an isolated
`FCITX5_TEST_USER_ROOT` (ignored by production builds), deploys `luna_pinyin`
and `luna_pinyin_simp` in that temporary directory, verifies menu states,
preedit cancellation, traditional/simplified commits, F4 candidate menus and
real Pinyin actions. Passing `-PeerTsfBuild` adds cross-architecture concurrent
Pipe checks.

Unit tests: enable `-DENABLE_WINDOWS_STATUS_MENU_TESTS=ON` in the root project
to build `test_windowsstatusmenu`; default TSF CTest includes `test_statusmenu`
and `test_langbar`.

## Developer Logging

Open **Global Settings > Developer options** to toggle detailed logging (applies
immediately in Core/Settings; TSF syncs on the next Core reply). All global
preferences—UI language, detailed logging, shortcuts and candidate layout—are
stored in `conf/windows-ui.conf` v4 under one revision (v1–v3 migrate to v4 on
the next save).

Log files live in `%LOCALAPPDATA%/Fcitx5/logs`:

- **Engine log (`fcitx5-YYYYMMDD.log`)**: always persisted as plain text.
  Threshold is `Info` by default and switches immediately to `Debug` when
  detailed logging is enabled (`key_trace` stays disabled). Preserves upstream
  addon message text.
- **Detailed structured logs (`core-YYYYMMDD.log`, `tsf-PID-YYYYMMDD.log`,
  `settings-PID-YYYYMMDD.log`)**: JSON Lines recorded only when detailed logging
  is enabled. Contains diagnostic metadata, HRESULTs, counts, lengths and
  geometry (TSF key results include scan codes, modifiers and consumption state,
  excluding translated characters/Unicode). Excludes input text, candidates,
  payloads, window titles and user paths.

Both formats rotate at 8 MiB and UTC day changes, with 7-day retention and a
shared 512 MiB directory budget (detailed logs also enforce a 64 MiB per-process
budget). Core runs cleanup at startup and hourly, and writes trigger cleanup;
open files stay protected until later maintenance.

When detailed logging is enabled, Settings records window/process exit events
(`settings_exit`), operation stages correlated by `operation_id`, and exception
metadata (`settings_exception`, XAML unhandled exceptions, `std::terminate`)
without exception messages or config values. Fatal C++/WinRT callback paths
attempt a 250 ms queue drain before preserving fail-fast termination. If a crash
leaves no final event (e.g., access violation or external termination), check
Windows Application Error/WER records.

Verification commands:

- Default CTest: `test_diagnostic_log`, `test_protocol`, `test_i18n`,
  `test_candidate`, `test_candidate_tsf`.
- Engine/IPC/TSF logging (requires `FCITX5_UI_TEST` Core/TSF builds):
  `./scripts/test-developer-logging.ps1 -Prefix <isolated-prefix> -TsfBuild <build>`
- Developer options UI (requires `-UiTest` Settings build):
  `./scripts/test-ui-localization.ps1 -SettingsBuild <build> -TsfBuild <build> -TestLogging`
- Settings exit/exception logging (requires `-UiTest` Settings build):
  `./scripts/test-settings-exit-logging.ps1 -SettingsBuild <build>/Release`
- Settings tab-switch & close stability (requires `-UiTest` Settings build and
  `ui_fixture`):
  `./scripts/test-settings-stability.ps1 -SettingsBuild <build> -TsfBuild <tsf-build>`

These runners set `FCITX5_DIAGNOSTIC_TEST_DIRECTORY` (ignored by production
builds). See [developer logging design](windows-developer-logging-design.md).

## Settings Shortcut Recording

**Global Settings > Shortcuts** configures input-method cycling (`Ctrl+Space`)
and direct selection of entries 1–5 (`Ctrl+Alt+1…5`). Engine key-list options
also provide a record button alongside multiline manual editing. See
[shortcut behavior](windows-settings-design.md#快捷键录制).

Build isolated Settings with `scripts/build-settings.ps1 -UiTest -BuildRoot <build>`
and TSF's `ui_fixture` target, then run on an interactive desktop:

```powershell
./scripts/test-settings-keycapture.ps1 -SettingsExe <build>/Release/Fcitx5Settings.exe -FixtureExe <tsf-build>/tests/ui_fixture.exe
./scripts/test-settings-shortcuts.ps1 -SettingsExe <build>/Release/Fcitx5Settings.exe -FixtureExe <tsf-build>/tests/ui_fixture.exe
```

`test-settings-keycapture.ps1` sends synthetic keyboard input to the recorder;
`test-settings-shortcuts.ps1` validates global shortcut defaults, validation,
persistence, v2→v4 migration and candidate layout switching. `test_keycapture`
runs in default CTest without WinUI.

## Modern App Acceptance

See [windows-modern-apps-design.md](windows-modern-apps-design.md) for the
runtime architecture. CI/Nightly enables `-ModernApps` (`UIELEMENTENABLED` and
`IMMERSIVESUPPORT`); local builds enable these categories only when `-ModernApps`
is passed. Changing categories requires installing the updated package or
re-registering the DLL.

The installer smoke test checks both modern categories in both registry indexes,
COM DLL path/threading model and the enabled language profile; missing
declarations block Nightly publication. Code signing and interactive testing in
signed/installed modern apps remain pending.

- **AppContainer transport diagnostics** (pass an existing AppContainer profile
  or package family name):

  ```powershell
  ./build/modern-apps/tsf/tests/appcontainer_probe.exe Microsoft.WindowsCalculator_8wekyb3d8bbwe
  ```

- **Restricted engine & TSF integration** (requires `FCITX5_UI_TEST` Core and
  TSF builds with staged data):

  ```powershell
  ./scripts/test-modern-apps.ps1 -Prefix ./build/modern-apps/prefix -TsfBuild ./build/modern-apps/tsf -AppContainerProfile Microsoft.WindowsCalculator_8wekyb3d8bbwe
  ```

- **Unsigned UWP acceptance fixture** (TextBox, RichEditBox and PasswordBox):

  ```powershell
  cmake -S win32 -B build/modern-apps/tsf -DENABLE_UWP_FIXTURE=ON
  cmake --build build/modern-apps/tsf --target uwp_fixture_package
  ```

## CI Build and Artifact Flow

CI builds each x64 component once in Release and runs validation and packaging
in parallel jobs (`.github/workflows/ci.yml`):

| Job | Responsibility |
| --- | --- |
| `build-x64` | Install pinned LLVM `22.1.8` and MSYS2 deps; build `base` with `-ModernApps -SkipTests`; copy and build all additional plugins for `full`; verify Wenzhounese, `table-extra` (Wubi98/Cangjie5/Jyutping Table) and Zhuyin commits plus Zhuyin model save/reload (`zhuyin_model_probe`); upload base/full runtimes, portable TSF tests/probes and standalone Settings |
| `test-x64` | Download `base` runtime and portable tests; run `test-build-and-deploy.ps1`, `test-build-plugins.ps1`, `test-installer-registration.ps1`, icon MD5 check, `test-pinyin.ps1` (including CTest), `test-inputmethods.ps1`, `test-engine-localization.ps1` and `test-dictionaries.ps1` |
| `test-x86` | Install pinned LLVM `22.1.8`; build/test independent x86 TSF; run x86 Pinyin/TSF probes with concurrent x86/x64 Pipe clients (`-PeerTsfBuild`) and dual-arch deployment checks (`test-deploy-tsf.ps1`) |
| `package-installer-x64` | 2×2 matrix (`base`/`full` × `runtime`/`no-runtime`); run `test-build-installer.ps1`, `test-installer-wizard.ps1` and `test-installer-restart.ps1`; build installer; verify `table-extra` and Zhuyin manifests in `full` / absence in `base` |
| `installer-smoke-x64` | Run silent machine-only install, modern TSF registration checks, uninstall and cleanup for both `base` and `full` no-runtime installers |
| `release` | After all jobs succeed on `master` (unless commit message contains `!release`), replace the `latest` prerelease with the four installers and their SHA256 files |

`test-x64`, `test-x86` and `package-installer-x64` start in parallel as soon as
`build-x64` finishes. The portable test bundle exports a relative-path manifest
via `ctest --show-only=json-v1` and restores CTest entries on the test runner
without reconfiguring CMake. Installer artifacts use compression level 0.

## Build Installer Packages

Build an isolated staging tree, then package with Inno Setup 7 (use `-IsccPath`
to select `ISCC.exe` explicitly):

```powershell
./scripts/build-and-deploy.ps1 -Prefix ./dist/installer-stage -BuildRoot ./build/installer
./scripts/build-installer.ps1 -Prefix ./dist/installer-stage -Output ./dist/installer -SkipRuntimes
./scripts/build-installer.ps1 -Prefix ./dist/installer-stage -Dependencies ./build/installer-deps -Output ./dist/installer
```

Bundled-runtime builds require `vc_redist.x64.exe` and
`WindowsAppRuntimeInstall-x64.exe` in `-Dependencies`, matching the SHA256
values in `cmake/installer-lock.json`. `-PackageVersion` defaults to `0.1.0.0`;
`-PackageName` overrides the output basename. Each build outputs `.exe`,
`.exe.sha256`, `.manifest.txt` and per-file runtime manifests under `metadata/`.
All paths must stay inside the repository.

Packaging writes `setup/managed-install` into the staging tree, which makes
service startup require installer-managed state; always use a dedicated staging
prefix rather than an active development deployment.

Deferred reboot upgrades are enabled by default (`-RebootUpgrade:$false`
disables them). See [reboot upgrade design](windows-reboot-upgrade-design.md).

Local packaging and registration regressions (no system installation):

```powershell
./scripts/test-build-installer.ps1
./scripts/test-installer-registration.ps1
./scripts/test-reboot-upgrade-package.ps1 -TsfBuild ./build/installer/tsf
./scripts/test-installer-wizard.ps1 -IsccPath <Inno-Setup-7/ISCC.exe>
./scripts/test-installer-restart.ps1 -IsccPath <Inno-Setup-7/ISCC.exe>
```

CI's `installer-smoke-x64` runs `scripts/test-installer.ps1 -ModernApps` with
`/VERYSILENT /MACHINEONLY=1` on a fresh runner. Testing bundled runtime
installers and interactive user initialization requires a clean Windows VM or
self-hosted runner.

## Deployment and Updates

Keep the deployed runtime tree intact:

```text
dist/pinyin/
  bin/Fcitx5.exe
  tsf/fcitx5-x86_64.dll
  tsf/x86/fcitx5-x86.dll
  settings/Fcitx5Settings.exe
  lib/
  share/
  plugins/          # full edition plugins
```

TSF locates Core at the adjacent `bin/Fcitx5.exe`, and Core locates Settings at
`settings/Fcitx5Settings.exe`; Core must stay in `bin/` for resource path
resolution. Rebuild and deploy all components together after an IPC protocol
change.

Before updating an existing deployment, choose **Stop Services** in the input
indicator and close applications loading its TSF DLL (or deploy to a new
`-Prefix`). Locked target files cause the build script to exit; file copying is
not atomic.

If Settings fails to open, choose **Restart Services** if stopped, then verify
the executable path and Windows App SDK/Visual C++ runtimes. Check Core and
Settings separately with `ipc_probe --ready` and
`settings_probe --open-settings` (see
[settings diagnostics](pinyin.md#windows-settings)).

## Validation

With no existing Core or Settings running, run integration checks against the
deployed tree:

```powershell
./scripts/test-pinyin.ps1 -TsfBuild ./build/all/tsf
./scripts/test-dictionaries.ps1 -TsfBuild ./build/all/tsf
./scripts/test-service.ps1 -TsfBuild ./build/all/tsf -WithSettings
./scripts/test-settings-ui.ps1 -TsfBuild ./build/all/tsf
./scripts/test-inputmethods.ps1 -Prefix ./dist/pinyin -TsfBuild ./build/all/tsf
./scripts/test-engine-localization.ps1 -Prefix ./dist/pinyin -TsfBuild ./build/all/tsf
```

Additional targeted integration scripts:

- **Cross-architecture x86/x64 probes**:
  `./scripts/test-pinyin.ps1 -Architecture x86 -TsfBuild ./build/all/tsf-x86 -PeerTsfBuild ./build/all/tsf`
- **Punctuation & Settings UI**:
  `./scripts/test-punctuation.ps1` (requires `FCITX5_UI_TEST` Core/TSF) and
  `./scripts/test-settings-punctuation.ps1`
- **Plugin status UI**:
  `./scripts/test-settings-plugins.ps1 -SettingsExe <UiTest Settings> -FixtureExe <ui_fixture.exe>`
- **Script regressions** (no registration or process termination):
  `./scripts/test-build-and-deploy.ps1`, `./scripts/test-build-plugins.ps1`,
  `./win32/tests/test_scripts.ps1`

UI tests require an interactive desktop and installed Settings runtimes.
See [probe coverage](pinyin.md#build-and-test-tsf) for default CTest details.

### Default CTest Highlights and Old-Runtime Coexistence

- `test_candidate` and `test_candidate_tsf` exercise production DirectWrite /
  Direct2D rendering, software DC fallback and popup lifecycle without Core or
  registration (see
  [candidate rendering verification](windows-candidate-rendering.md#verification)).
- Runtime import checks (`test_tsf_runtime_imports`, `test_tsf_runtime_audit`,
  `test_tsf_runtime_host`) audit normal and delayed PE imports via LLVM and
  verify loading/activating/unloading the TIP DLL three times.
- Enabling `-DENABLE_PINYIN_INTEGRATION_TESTS=ON` registers three Core probes in
  CTest (requires a running deployed Core).
- Configuring a fresh TSF build with
  `-DTSF_PRELOAD_RUNTIME="C:/path/to/jbr/bin/MSVCP140.dll"` adds
  `test_tsf_old_runtime_host` to verify coexistence when an older `MSVCP140.dll`
  is already loaded in the host process (keep Release and Debug build trees
  separate).

## Registration and DLL Management

Installer-managed installations should be removed through the Start menu or
Windows Installed Apps. The helpers below manage development deployments only.

`install.ps1` and `uninstall.ps1` default to `dist/pinyin/tsf/fcitx5-x86_64.dll`
(use `-Prefix` or `-DllPath` for another build; `-DllPath` selects
`System32`/`SysWOW64` `regsvr32` from the PE machine):

```powershell
./win32/scripts/install.ps1 -WhatIf
./win32/scripts/install.ps1
./win32/scripts/uninstall.ps1 -WhatIf
```

Real registration/unregistration requests elevation when needed and waits for
completion. Re-register after changing profile, category or icon metadata, and
reopen applications to load the rebuilt DLL.

To inspect or release a locked TSF DLL, preview first:

```powershell
./win32/scripts/release-tsf.ps1 -Force -WhatIf
./win32/scripts/release-tsf.ps1 -Prefix "$PWD/dist/another" -Force -WhatIf
```

`release-tsf.ps1` unregisters TSF by default (`-SkipUnregister` skips
unregistration and elevation). `-Force` terminates processes verified to load
the exact DLL path (which can include editors, browsers and Explorer—save work
first; unverified owners are skipped). Stop Core/Settings from the indicator
menu beforehand. Supply `-HandlePath` if Sysinternals Handle is not in `PATH`;
otherwise the script falls back to `tasklist /m` and accessible module lists.
`-WhatIf` makes no system changes and needs no elevation.
