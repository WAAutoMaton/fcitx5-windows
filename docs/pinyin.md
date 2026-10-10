# Windows Pinyin Integration

This guide describes the AMD64 Pinyin integration and its current validation
scope as of 2026-10-10. For a quick start, see [Run in the README](../README.md#run).
For build options, deployment and registration details, see the
[build guide](build.md).

## Versions and Architecture

The pinned combination is Fcitx5 5.1.22, chinese-addons 5.1.15 and libime
1.1.17, including libime's pinned KenLM submodule. Although chinese-addons
declares libime 1.1.14 as its minimum, its selected revision uses newer
`HistoryBigram::WordWithCode` and Pinyin context APIs. The tested combination
is recorded in `cmake/pinyin-lock.json`.

Core and TSF are independent CMake projects. The root project builds Core and
its host; it does not include `win32`, libime or chinese-addons. Core runs in a
separate MSYS2 clang64 process. The TSF DLL is compiled with LLVM's Windows
SDK/ATL toolchain and loaded into the application's process. No C++ object,
allocator or engine ABI crosses the pipe. Pipe service threads dispatch requests
to Core's event loop through `EventDispatcher`; document changes use TSF edit
sessions on the TSF owning thread.

| Source | Responsibility |
| --- | --- |
| [src/main.cpp](../src/main.cpp) | Host initialization, in-memory input group and Pinyin warmup |
| [src/windowsfrontend.cpp](../src/windowsfrontend.cpp) | Windows InputContext and Core pipe service |
| [win32/ipc/protocol.h](../win32/ipc/protocol.h) | Shared framing and snapshot format |
| [win32/tsf/pipeclient.cpp](../win32/tsf/pipeclient.cpp) | TSF pipe client and bounded I/O |
| [win32/tsf/EditSession.cpp](../win32/tsf/EditSession.cpp) | Composition and document edits |
| [win32/tsf/langbaritem.cpp](../win32/tsf/langbaritem.cpp) | Input mode button, notifications and lifetime |
| [win32/dll/register.cpp](../win32/dll/register.cpp) | Profile, branding icon and capability registration |

### Input Modes and Indicator

`windowskeyboard` is a static direct-input engine with no XKB dependency. It
provides the first group entry required by Core; it never commits ASCII itself.
The Windows group uses `keyboard-us`, `pinyin` and, when installed, `shuangpin`.
Ctrl+Space switches direct/Chinese modes using the selected Pinyin scheme.
TSF subscribes to the thread manager's `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE`:
system keyboard open/close changes select Pinyin/direct input in Core. Focus
changes and reconnections preserve that system mode. An exact Ctrl+Space
preserved key and the ordinary key callback use the same mode synchronization;
ordinary Space is never registered as a preserved key. Existing user groups are
not saved or overwritten. Both upstream keyboard support and ASCII fallback are
disabled by default and by the Pinyin build script.
Core prewarms Pinyin before accepting pipe clients so initial model paging does
not consume the first key's IPC timeout. Runtime DLL search includes its own bin.

TSF publishes one Language Bar button with `GUID_LBI_INPUTMODE` while the TIP
is activated and removes it when deactivated. Chinese mode shows "中" and
direct-input mode shows "A"; closing the keyboard compartment changes the icon
rather than removing the item. Clicking it posts a message to the TSF owning
thread, updates the same keyboard compartment, and uses the existing Core
`SetMode` request. The transparent monochrome glyphs are drawn with GDI at the
system small-icon size and declare `TF_LBI_STYLE_TEXTCOLORICON` for system theme
coloring. No extra runtime icon files or Core protocol changes are required.
This is a Windows input-indicator item, not a `Shell_NotifyIcon` tray icon;
its display is controlled by Windows and the user's language-bar settings.
After removal, a retained language-bar object is hidden and detached from its
message window, so late clicks cannot affect another activation. It holds an
independent DLL reference until released.

The icon reflects the system keyboard compartment, not Core connection health.
It can show a mode while Core is unavailable. TSF activation starts Core in the
background, and Core must remain running for Pinyin input. Registration itself
does not start services.

The desktop TIP registers `GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT` so Windows can
recognize its input-indicator support without declaring unverified Windows Store
compatibility. The branding icon is embedded in the TSF DLL as `IDI_FCITX5`, the
only group icon, and the profile refers to the DLL path with icon index 0.
`RegisterProfile` receives character counts for the description and DLL path.
The generated `penguin.ico` remains a build input; it is no longer the profile's
runtime icon file. Its existing generation and checksum are unchanged.

After updating an existing deployment, explicitly re-register the rebuilt DLL
to update the categories and profile icon metadata; replacing the binary alone
does not update these settings. Restart the test application so it loads the
rebuilt DLL. These registration changes are not performed by the test scripts.

## Compatibility Patches

Initialize the pinned submodules, then apply the patches kept in the main repo:

```powershell
git submodule update --init --recursive
./scripts/apply-pinyin-patches.ps1
```

The script verifies HEAD revisions, skips already-applied patches and refuses
conflicting changes. It does not reset submodules or create commits.

- `fcitx5-windows-paths.patch`: Windows built-in resource paths use the installed
  `share`, `share/fcitx5` and `share/locale` directories. Core 5.1.22's Windows
  StandardPaths does not use the earlier resource environment overrides.
- `libime-windows.patch`: imported executable paths include `.exe`; model path
  lists use `;` on Windows, preserving drive letters.
- `chinese-addons-windows.patch`: dictionary loading accepts filesystem paths,
  allowing Windows' wide native paths to reach `std::ifstream`; the addon also
  exposes extra dictionary loading states through a read-only subconfiguration.

The submodule working trees are intentionally patched. Record Windows changes
in the main-repository patch files; do not create local submodule commits or
discard this patched dirty state. Keep the pinned revisions unless the task
explicitly requires an upgrade, and keep the lock file and submodule pointers
consistent if versions are intentionally changed.

## Build and Deploy Core

For a single command that also builds/deploys TSF and Settings, use
`./scripts/build-and-deploy.ps1`. It defaults to `Prebuilt`, stages the complete
runtime under `build/all/prefix`, runs default TSF CTest, then copies to
`dist/pinyin`. It initializes the VS x64 environment itself. See the
[build options and update procedure](build.md#unified-build-options).

Prerequisites: CMake 3.27+, MSYS2 clang64 Clang/Ninja/pkgconf/ECM/dlfcn/libuv/
gettext, zstd, and Boost headers/iostreams. No script installs or upgrades system
packages. Run the commands from the repository root. The build script currently
targets AMD64 using clang64 and does not offer an ARM64 Pinyin build option.

```powershell
./scripts/build-pinyin.ps1 -DataMode Prebuilt
```

This applies the compatibility patches and builds Core, libime and its tools,
then chinese-addons without Qt configuration tools, OpenCC or cloud Pinyin.
Only `usr/share/libime` and `usr/lib/libime` are extracted from the SHA256-pinned
Arch Linux libime 1.1.17 package; no Linux executable/library is loaded. The
dictionary and KenLM data formats have been verified with the Windows build.
This is the locally validated deployment path.

The default `Source` mode generates data locally instead:

```powershell
./scripts/build-pinyin.ps1 -DataMode Source
```

It downloads source datasets from the upstream server with SHA256 validation.
Language-model generation can take significant time and memory. Source-data
generation has not been validated end to end locally.

| Build Option | Behavior |
| --- | --- |
| `-MSYS2Root` | MSYS2 location, default `C:/msys64`; the script uses its clang64 toolchain |
| `-DependencyPrefix` | Extra dependency prefix; Boost/iostreams and zstd must use the same target/runtime |
| `-Prefix` | Install location, default `dist/pinyin`; must remain inside this repository |
| `-BuildRoot` | Build directory parent, default `build`; the unified script uses `build/all` |
| `-DataArchive` | Local archive for `Prebuilt` mode, verified against the locked SHA256; avoids the data download, not all environment prerequisites |
| `-Jobs` | Build parallelism, default 6 |

The build directories are `build/pinyin-core`, `build/pinyin-libime` and
`build/pinyin-addons`, with prebuilt data cached in `build/pinyin-data`.
`-BuildRoot` changes the parent of those three build directories; prebuilt data
remains cached in `build/pinyin-data`. Changing `-Prefix` alone changes
deployment, not these build directory names. A plain
root CMake build alone does not produce a complete Pinyin installation.

Before downloading precompiled data, the script checks the standard cached
archive and local `libime*.pkg.tar.zst` files in `build/deps` and
`build/pinyin-data`. A matching SHA256 is required, regardless of filename.
`-DataArchive` is validated explicitly and populates the same standard cache.
Subsequent builds share this data cache even when their `-BuildRoot` differs.

The default prefix is `dist/pinyin`. The script recursively resolves PE imports
and copies the necessary MSYS2 runtime DLLs into `bin`, without copying Windows
system DLLs. Start the deployed `dist/pinyin/bin/Fcitx5.exe`, not the build-tree
executable, for input validation. Keep `bin`, `lib` and `share` together when
moving the prefix. The executable must remain in `bin` for the Windows resource
path calculation. User dictionaries remain in Fcitx5's user data directories;
an isolated installation prefix does not isolate those user dictionaries.

## Build and Test TSF

Use an initialized Visual Studio x64 development environment with LLVM clang,
Windows SDK/ATL, Ninja and ImageMagick, not the MSYS2 clang compiler. Run these
commands from the repository root after deploying Core:

```powershell
cmake -S win32 -B win32/build/pinyin-tsf -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build win32/build/pinyin-tsf
./scripts/test-pinyin.ps1
```

The TSF DLL requires RC compilation for its embedded branding icon.
With the GNU-style Clang driver, use LLVM's `llvm-rc`; if an old CMake cache
selects the SDK's `rc.exe`, reconfigure with
`-DCMAKE_RC_COMPILER="C:/Program Files/LLVM/bin/llvm-rc.exe"`.

The test script starts only the isolated Core, runs both probes and CTest, then
stops the process it started. It refuses to run alongside an existing Core.
Neither the probes nor the CTest tests register/unregister an input method or
call `DllRegisterServer`. The separate install/uninstall/release scripts do
modify registration and are not integration test steps.

For CTest alone, without starting Core:

```powershell
ctest --test-dir win32/build/pinyin-tsf --output-on-failure
```

The default nine tests are `test_dll`, `test_protocol`, `test_settingsfile`, `test_input`,
`test_candidate`, `test_transport`, `test_service`, `test_langbar` and `test_register`. Root/upstream tests are
disabled by the default build configuration; these nine tests are not the full
upstream test suite.

- `ipc_probe`: real `nihao` candidates and Chinese commits, number/space
  selection, backspace, cancellation, Ctrl+C pass-through, Ctrl+Space switching,
  and separate connections/context ownership.
- `tsf_probe`: actual DLL loading/factory/interfaces, actual Windows TSF document
  contexts backed by an in-memory ITextStoreACP, composition, Chinese commit,
  synchronous-to-asynchronous edit fallback, cancellation, late edits and focus
  isolation. Because this TIP is not registered, a test-only adapter substitutes
  key-sink subscription, and the probe explicitly supplies key/focus callbacks.
  This does not verify Windows' registered TIP activation or OS key dispatch.
  A test-only language-bar manager captures the input mode item instead of
  publishing an unregistered service to the desktop. The probe checks add/remove,
  Chinese/English state and notifications, explicitly invoked button clicks,
  activation rollback and detached-item lifetime/late-click isolation; it does
  not verify taskbar rendering or actual OS language-bar click dispatch.
  Mode regressions use a real thread-manager keyboard compartment, including
  open/close notifications, cancellation, focus changes and a simulated failure
  to register the exact Ctrl+Space preserved key. Preserved-key callbacks are
  explicitly invoked; the adapter does not prove real OS hotkey dispatch.
- Default CTest: helper functions, protocol framing/snapshots, UTF-8/UTF-16/key policy,
  real pipe timeout/cancellation followed by a successful read, and language-bar
  COM identity, sink cookies, state notifications and nonblank/distinct monochrome
  icon pixels.
- `test_register`: actual DLL branding resource and Shell icon extraction,
  plus profile/category calls captured by fake COM managers to verify character
  lengths, DLL path, icon index, `SYSTRAYSUPPORT` and failure handling. It never
  invokes system registration and does not prove taskbar rendering.

Optional `ENABLE_PINYIN_INTEGRATION_TESTS=ON` registers all three probes with CTest;
a deployed Core must already be running, bringing the total to ten. Release
builds and the default eight tests passed for this change; earlier Debug and
clean Release verification covered the previous six-test set. Real Pinyin and
settings probes passed with the deployed Core.

`scripts/test-pinyin.ps1` accepts `-Prefix` and `-TsfBuild` for other deployment
and TSF build directories. Logs are written to `build/pinyin-test/core.stdout.log`
and `core.stderr.log`. It refuses to run alongside any existing `Fcitx5` process
and only stops the Core it starts. Read those logs if Core exits or the readiness
check fails.

If a registered DLL is locked during a rebuild, close applications using it or
build TSF in a new directory and pass that directory with `-TsfBuild`. Repeating
unregistration alone does not unload it from running applications. See the
[release helper instructions](build.md#registration-and-dll-management) before using
`release-tsf.ps1`: it defaults to `dist/pinyin/tsf/fcitx5-x86_64.dll`, supports
`-Prefix`/`-DllPath`, and `-Force` terminates owners with verified exact DLL paths;
these can include editors, browsers or Explorer. Preview with `-Force -WhatIf`
without elevation. Install/uninstall helpers share the same path options and
default, and request elevation only for real registration operations. None of
these helpers stops Core/Settings as services; use the input indicator menu first.

## Protocol and Behavior

Protocol v5 uses a pipe name scoped to the user's SID and Windows session. Its
ACL permits only the current user, rejects remote clients, isolates context IDs
by connection, and permits multiple connections. Clients use cancellable
overlapped I/O with a 500 ms wait limit per read/write operation; this is not a
single 500 ms deadline for the entire request. Disconnected clients' contexts
are destroyed. The pipe name retains the historical `fcitx5-windows-v2-` prefix;
compatibility is checked using the framing/handshake version, which is v5.

`SetMode` sets an explicit Pinyin/direct-input mode rather than replaying a
toggle keystroke. Repeated requests are idempotent; changing modes cancels the
current preedit. Core and TSF must both be rebuilt and deployed after this
protocol upgrade; older versions do not connect to v5.

Each key response contains consumption, mode, commit, preedit, UTF-8 byte cursor,
revision, settings epoch and the current candidate page. Reset cancels Core composition. PollState
collects deferred engine updates on Core's event loop. TSF's 100 ms owning-thread
timer polls while a foreground document context exists, including during pending
edits so settings changes invalidate old preedit; it is not a guaranteed output latency. Disconnected clients retry
every two seconds while a foreground context exists. Requests are not replayed
after a timeout.

The key test callback performs no IPC or editing. Actual processing runs once
and decides consumption from Core. Each edit session owns its original context,
generation and immutable reply. Queued edits are serialized and stale generations
cannot modify a new context. Once Core consumes a key, an edit failure does not
cause the original key to be replayed; the error resets the input state.

TSF keeps a persistent composition, converts byte cursors into UTF-16 offsets,
applies a dotted underline, replaces the composition range on commit and cancels
on focus loss. A nonactivating Win32 candidate popup uses DirectWrite layouts,
Direct2D color-font drawing and Per-Monitor V2 DPI/work-area bounds. Label, body
and comment columns use measured text heights and native ellipsis trimming.
See [candidate rendering](windows-candidate-rendering.md) for design, previews
and remaining validation. Candidate selection and paging are handled
by the Pinyin engine; the popup does not insert candidate text itself.
Focus changes create/destroy remote contexts; the current implementation does
not persist separate unfinished preedits for multiple input fields.

## Windows Settings

Right-clicking the language-bar input mode item opens a native menu with
`输入法设置`, `重启服务` and `关闭服务`. The settings command asks Core to launch the independent WinUI 3
`settings/Fcitx5Settings.exe`. The Core connection and the settings launch are
separate steps: a running Core can accept IPC while the settings window is
missing or unable to initialize. The real OS right-click dispatch still
requires registered-TIP validation; unit tests exercise menu construction and
commands.

Build and deploy the x64 settings application separately:

```powershell
./scripts/build-settings.ps1 -Configuration Release -Prefix "$PWD/dist/pinyin"
```

The command must use the same prefix as the deployed Core. It creates
`dist/pinyin/settings/Fcitx5Settings.exe`, which is the path calculated by Core
from its own `bin/Fcitx5.exe` location. It uses framework-dependent Windows App
SDK 1.8 deployment. Install the matching x64 Framework and DDLM packages
(minimum `8000.994.2142.0`) and the Visual C++ runtime for the current user.
Build/test scripts do not install these packages.

Deploy the DLL beside Core using the isolated deployment script:

```powershell
./scripts/deploy-tsf.ps1 -Prefix "$PWD/dist/pinyin" -TsfBuild "$PWD/win32/build/pinyin-tsf"
```

The resulting layout is `bin/Fcitx5.exe`, `tsf/fcitx5-x86_64.dll` and
`settings/Fcitx5Settings.exe`. TSF uses its own DLL location to find the fixed
adjacent Core path, without searching PATH or modifying the host's DLL search
environment. A DLL loaded directly from the development build directory does
not have this deployment layout. Register the deployed DLL explicitly when
ready to test system dispatch; deployment scripts do not change registration.

TSF activation automatically starts Core in the background on a worker thread.
Settings starts only when requested. The input indicator's `输入法设置`
command starts Core if necessary and sends `OpenSettings` through its pipe;
it does not require registering the settings executable. After registering the
TSF DLL and restarting the application that hosts it, select Fcitx5 with
`Win+Space`, right-click the Fcitx5 input-mode item and choose `输入法设置`.
For a direct launch while Core is already running:

```powershell
$settings = (Resolve-Path ".\dist\pinyin\settings\Fcitx5Settings.exe").Path
Start-Process -FilePath $settings -WorkingDirectory (Split-Path $settings)
```

The `用户数据文件夹` menu command opens `%APPDATA%/Fcitx5` in the Windows
file manager, creating it when absent. This directory contains settings under
`config/fcitx5` and Pinyin learning data under `pinyin` (`user.dict` and
`user.history`). It uses Windows' roaming AppData known folder and remains
available while services are stopped, without starting Core or Settings.

`重启服务` gracefully stops and restarts Core, or starts it when absent. If
Settings was open, it closes and reopens after Core is ready; otherwise it
stays closed. `关闭服务` gracefully stops both processes and inhibits automatic
startup for this login until `重启服务` is selected. Uncommitted composition is
cancelled. The TSF DLL stays loaded and the language-bar menu remains available.
If TSF is hosted by Settings itself, a short-lived Core control process handles
the operation so it can wait for the old Settings process to exit.

Service controls use current-user ACLs and SID/session/logon LUID/time names. Core
checks its stop event in the main event loop; Settings checks it in the UI
thread. A manual stop is recorded in a logon-specific `.state` file under
`%LOCALAPPDATA%/fcitx5`, surviving unloading all TSF DLLs without applying to the
next login. A shared 10-second retry interval prevents launch storms on failure.
There is no forced termination by process name. A hung process yields a timeout
instead of being killed. The current IPC protocol is v5;
deploy updated Core, TSF and Settings together for the new control behavior.

Run only one Core process per user/session. A second Core process detects the
owned instance mutex and exits without initializing an engine. To distinguish connection and deployment
failures, run the probes from the repository root:

```powershell
& .\win32\build\pinyin-tsf\tests\ipc_probe.exe --ready
& .\win32\build\pinyin-tsf\tests\settings_probe.exe --open-settings
```

The first command checks the Core handshake. If it passes but the second fails,
Core is reachable and the problem is the settings executable or its Windows
App SDK/Visual C++ runtime. Keep Core, TSF and the settings deployment from
the same protocol revision; after a protocol upgrade rebuild and redeploy all
three components.

The UI supports full/double pinyin, eight built-in profiles, save/cancel,
single-instance activation, system theme resources and PerMonitorV2 DPI.
An existing custom profile is preserved but cannot be created/imported here.

### Third-Party Dictionaries

The `词库` tab lists the extra dictionaries actually discovered by the Pinyin
engine, with loading, loaded, failed or disabled status. It queries Core using
the context-independent `GetDictionaries` request; the UI does not infer load
success from filenames. The list refreshes every two seconds while the tab is
visible. It currently has no import, delete or enable/disable controls.

`打开第三方词库文件夹` creates and opens
`%APPDATA%/Fcitx5/pinyin/dictionaries` using the Windows Roaming AppData known
folder. This action also works when Core cannot be reached. Drop compatible
Fcitx5/libime binary `.dict` files directly in this folder. Other input methods'
text dictionaries and binary formats are not interchangeable just because they
use the same extension. To compile a UTF-8 libime text dictionary:

```powershell
& .\dist\pinyin\bin\libime_pinyindict.exe words.txt poetry.dict
```

For example, a line in `words.txt` is `夜来风雨声 ye'lai'feng'yu'sheng 0`.
The resulting dictionary is shared by full pinyin and all Shuangpin profiles.
An extra dictionary participates in normal libime scoring; loading a word does
not impose a fixed candidate rank.

Core creates this folder on startup. It scans its immediate regular `.dict`
and `.disable` files once per second, comparing names, sizes and modification
times. Two identical scans trigger the upstream `dictmanager` reload on the
Core event loop. Parsing runs on the existing Pinyin worker; completion and
status updates return to the Core event loop. Addition, replacement, rename and
deletion take effect without restarting Core and preserve pending composition.
Subdirectories are not scanned. As with the upstream manager, `name.dict.disable`
disables `name.dict`. Failed files are reported and do not prevent valid files
from loading. A file modified again is retried after it becomes stable.

The small addon status-query extension is recorded in
`patches/chinese-addons-windows.patch`; submodule revisions are unchanged. Core,
TSF and Settings must be rebuilt and deployed together for protocol v5.

With Core and Settings stopped, run:

```powershell
./scripts/test-dictionaries.ps1 -Prefix "$PWD/dist/pinyin" -TsfBuild "$PWD/win32/build/pinyin-tsf"
```

The test creates uniquely named temporary dictionaries and restores input
settings. It verifies startup loading, live changes, Unicode paths, bad files,
disable markers, full/Xiaohe/Ziranma candidates and pending composition. It
starts and stops only its own Core and does not register the TIP. The UI test
also checks the dictionary tab, actual loaded/failed states and the Explorer
folder button, and captures `dictionaries.png`.

### Input Settings

`GetSettings`, `SetSettings`, and `OpenSettings` are connection-independent
requests with context ID zero. Core alone writes the authority file
`conf/windows.conf` under the Fcitx user package configuration directory.
The profile is also applied through the upstream addon interface; its Pinyin
configuration file is a mirror. Switching cancels unfinished preedit, keeps
English contexts in direct mode and applies to new/reconnected contexts.
Save errors and stale-revision conflicts are reported. Timed-out writes are
queried before retrying. Core/TSF/settings must be deployed together.

For isolated settings persistence tests, Core accepts
`--windows-settings-file ABSOLUTE_PATH`. This overrides the Windows settings
file only; upstream dictionaries and addon configuration still use user paths.

```powershell
./scripts/test-settings-ui.ps1 -Prefix "$PWD/dist/pinyin"
# Also validate the Core launch request through a freshly built probe:
./scripts/test-settings-ui.ps1 -Prefix "$PWD/dist/pinyin" -TsfBuild "$PWD/win32/build/pinyin-tsf"
```

This UI Automation test starts/stops its own Core/settings processes, uses a
separate settings file and captures screenshots. Run on an interactive desktop
with the required runtime. It does not register or unregister the TIP. Real
full/Xiaohe/Ziranma conversion is covered by `settings_probe` and `tsf_probe`.
See [the design](windows-settings-design.md) for deployment and validation details.

Validate automatic startup and service controls with no existing Core/Settings:

```powershell
./scripts/test-service.ps1 -Prefix "$PWD/dist/pinyin" -TsfBuild "$PWD/win32/build/pinyin-tsf"
./scripts/test-service.ps1 -Prefix "$PWD/dist/pinyin" -TsfBuild "$PWD/win32/build/pinyin-tsf" -WithSettings
```

`service_probe` loads the deployed DLL, uses a real TSF thread manager with the
existing key/language-bar adapters, and explicitly invokes menu callbacks.
It restores the original service-control state and does not change IM
registration. The settings variant requires the installed WinUI runtime and
also models a Settings process hosting TSF. It is separate from default CTest
because it requires a complete deployment and starts real service processes.

The service-control change passed AMD64 Core, TSF Release and WinUI Release
builds, all eight default CTests, the service probe with actual WinUI, and all
three existing Pinyin probes. The WinUI save/cancel, restart and single-instance
test passed again with screenshots in `build/service-settings-ui-test`.
Registered-TIP OS menu dispatch remains unverified.

## Remaining Validation and Limits

- The user confirmed the registered input indicator works. This is narrower
  than a complete real-application input compatibility test; OS key/hotkey
  dispatch and input in Notepad, browsers and other hosts still need broader
  validation. The test application and Windows-version matrix were not recorded.
- Candidate rendering, DPI behavior and screen-reader integration are not
  verified in those applications; the window does not implement ITfUIElement.
- Input-indicator theme coloring, Explorer restart, multiple-monitor DPI and
  broader Windows-version coverage still need registered-TIP validation.
  The automated probes do not verify taskbar rendering.
- Candidates currently have keyboard-only selection, with no candidate mouse
  interaction, ordinary notification-area tray or unified Core/TSF installer.
- Xiaohe and Ziranma Shuangpin commits are validated by probes. Other profiles,
  Wubi, Rime and Windows Store compatibility need broader validation.
  `SYSTRAYSUPPORT` does not declare `IMMERSIVESUPPORT`.
- Password/secure contexts, dead keys, AltGr/non-US layouts, forwarded keys and
  surrounding-text editing are not claimed as supported. Secure-mode and other
  unimplemented TSF categories are no longer declared.
- Polling is a minimal deferred-output mechanism, not an asynchronous push queue
  or transactional exactly-once delivery across process failures. An IPC timeout
  can lose the pending input; it does not guarantee recovery of a consumed key.
- Only AMD64 Core/TSF/Pinyin was exercised locally. The existing Core ARM64 build
  matrix does not imply Pinyin or TSF ARM64 compatibility.

The first-run Pinyin history load may log a missing/empty-history error; the
system dictionary/model and actual Chinese conversion are verified separately.
