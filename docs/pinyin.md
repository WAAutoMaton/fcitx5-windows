# Windows Input-Method Integration

This guide describes the AMD64 integration for Pinyin, Shuangpin and the
`chinese-addons` table input methods, together with its current validation scope.
For installation, see [Install in the README](../README.md#install);
for input usage, see [Run](../README.md#run).
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
| [src/windowssettings.cpp](../src/windowssettings.cpp) | Pinyin scheme/profile application and authoritative persistence |
| [src/windowsinputmethods.cpp](../src/windowsinputmethods.cpp) | Input-method catalog, ordered selection, shared engine options and cycling |
| [src/windowsdictionaries.cpp](../src/windowsdictionaries.cpp) | Stable directory scans, addon reload and dictionary status queries |
| [win32/ipc/protocol.h](../win32/ipc/protocol.h) | Shared framing and snapshot format |
| [win32/tsf/pipeclient.cpp](../win32/tsf/pipeclient.cpp) | TSF pipe client and bounded I/O |
| [win32/tsf/EditSession.cpp](../win32/tsf/EditSession.cpp) | Composition and document edits |
| [win32/tsf/langbaritem.cpp](../win32/tsf/langbaritem.cpp) | Input mode button, notifications and lifetime |
| [win32/tsf/servicecontrol.cpp](../win32/tsf/servicecontrol.cpp) | Background service commands and user-data folder opening |
| [win32/tsf/candidatewindow.cpp](../win32/tsf/candidatewindow.cpp) | DirectWrite/Direct2D candidate popup and DPI positioning |
| [win32/settings/SettingsWindow.cpp](../win32/settings/SettingsWindow.cpp) | WinUI input-method list, engine options, Pinyin dictionary tab and background IPC |
| [win32/dll/register.cpp](../win32/dll/register.cpp) | Profile, branding icon and capability registration |

### Input Methods and Indicator

`windowskeyboard` is a static direct-input engine with no XKB dependency. It
provides the first group entry required by Core; it never commits ASCII itself.
The in-memory Windows group includes installed, available entries from the
Pinyin and table addons. The current table set is:

| Entry | Input method |
| --- | --- |
| `wbx` | Wubi |
| `wbpy` | Wubi Pinyin |
| `cangjie` | Cangjie |
| `db` | Dianbaoma |
| `erbi` | Erbi |
| `qxm` | Bingchan |
| `wanfeng` | Wanfeng |
| `zrm` | Ziranma |

An entry is advertised as available only when its addon and table data can be
loaded. The user's enabled list and order are stored separately; the default
list is `keyboard-us` and `pinyin`.
Settings can add, remove and reorder entries, including removing English
keyboard, while retaining at least one available method.

Indicator left-clicks use `CycleInputMethod` to advance through the ordered list,
skipping unavailable entries. Keyboard cycling defaults to Ctrl+Space; Global
Settings > Shortcuts also provides direct selection of entries 1–5, defaulting
to Ctrl+Alt+1…5. Direct selection uses the original list position and passes
through if the target is absent or unavailable. Changing methods cancels old
preedit without committing it. TSF restores the current entry on focus changes
and reconnection. Its thread-manager `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE`
reflects whether the actual entry is a non-keyboard method; explicit system
open/close changes use `SetMode` to select an enabled non-keyboard/keyboard entry.
If no matching entry exists, the current method is retained. Ordinary,
preserved-key and compartment callbacks use the configured shortcuts, with
duplicate/repeat suppression until the shortcut key is released. Existing user groups are not saved or
overwritten. Both upstream keyboard support and ASCII fallback are disabled by
default and by the Windows build script.
Core prewarms Pinyin before accepting pipe clients so initial model paging does
not consume the first key's IPC timeout. Runtime DLL search includes its own bin.

TSF publishes one Language Bar button with `GUID_LBI_INPUTMODE` while the TIP
is activated and removes it when deactivated. It shows the current method's
installed PNG icon, loaded through WIC, or a compact text-label fallback such as
"拼" or "双"; English keyboard uses "A". The tooltip shows the method's name.
Left-clicking posts a cycle request to the TSF owning thread; the Core reply
updates the selected method, icon and keyboard compartment. Closing the
compartment does not remove the item. Fallback glyphs use GDI at the system
small-icon size. The button does not declare `TF_LBI_STYLE_TEXTCOLORICON`, so
Windows does not recolor installed method icons as monochrome glyphs.
This is a Windows input-indicator item, not a `Shell_NotifyIcon` tray icon;
its display is controlled by Windows and the user's language-bar settings.
After removal, a retained language-bar object is hidden and detached from its
message window, so late clicks cannot affect another activation. It holds an
independent DLL reference until released.

The icon identifies the current method from Core's last snapshot; it is not a
Core connection-health indicator and may remain visible while Core is unavailable.
TSF activation starts Core in the background, and Core must remain running for
engine input. Registration itself
does not start services.

The desktop TIP registers `GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT` for its input indicator.
`ENABLE_MODERN_TSF_CATEGORIES` (the unified script's `-ModernApps`) additionally
declares `UIELEMENTENABLED` and `IMMERSIVESUPPORT`. CI/Nightly enables this option;
local builds default to off. See [modern app acceptance](build.md#modern-app-acceptance)
for registration and interactive validation.
The branding icon is embedded in the TSF DLL as `IDI_FCITX5`, the
only group icon, and the profile refers to the DLL path with icon index 0.
`RegisterProfile` receives character counts for the description and DLL path.
The generated `penguin.ico` remains a build input; it is no longer the profile's
runtime icon file. Its existing generation and checksum are unchanged.

When an update changes registered categories or profile icon metadata,
explicitly re-register the rebuilt DLL; replacing the binary alone does not
update these settings. Restart the test application so it loads the rebuilt DLL.
Candidate rendering and Pinyin setting changes do not change registration
metadata. Test scripts do not perform registration.

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
- `chinese-addons-windows-table.patch`: enables the bundled Cangjie entry on
  Windows and resolves built-in table data from the deployed `share/libime` tree.
- `fcitx5-rime-windows.patch`: resolves Rime shared data through the installed
  `share/rime-data` search path and converts native filesystem paths to UTF-8.
  The full plugin build supplies librime and its schema data; see
  [Windows plugins](windows-plugins.md).

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
gettext, zstd, and Boost headers/iostreams. Build/deploy scripts do not install or
upgrade system packages. Run the commands from the repository root. The build
script currently targets AMD64 using clang64 and does not offer an ARM64 Pinyin
build option.

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
cmake -S win32 -B win32/build/pinyin-tsf -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  '-DCMAKE_RC_COMPILER=C:/Program Files/LLVM/bin/llvm-rc.exe'
cmake --build win32/build/pinyin-tsf
./scripts/test-pinyin.ps1
```

The TSF DLL requires RC compilation for its embedded branding icon.
With the GNU-style Clang driver, use LLVM's `llvm-rc`; if an old CMake cache
selects the SDK's `rc.exe`, reconfigure with
`-DCMAKE_RC_COMPILER="C:/Program Files/LLVM/bin/llvm-rc.exe"`.

The test script starts only the isolated Core with an independent Windows
settings file, runs `ipc_probe`, `settings_probe`, `tsf_probe` and CTest, then
stops the process it started. It refuses to run alongside an existing Core.
Neither the probes nor the CTest tests register/unregister an input method or
call `DllRegisterServer`. The separate install/uninstall/release scripts do
modify registration and are not integration test steps.

For CTest alone, without starting Core:

```powershell
ctest --test-dir win32/build/pinyin-tsf --output-on-failure
```

The default tests are declared in
[`win32/tests/CMakeLists.txt`](../win32/tests/CMakeLists.txt):

| Scope | Tests |
| --- | --- |
| DLL, protocol and input | `test_dll`, `test_protocol`, `test_input` |
| Persistence, UI language and diagnostics | `test_settingsfile`, `test_i18n`, `test_diagnostic_log`, `test_keycapture` |
| Candidate rendering and host UI | `test_candidate`, `test_candidate_tsf`, `test_candidateuielement` |
| Transport and isolation | `test_transport`, `test_pipeidentity`, `test_isolationpolicy`, `test_sandbox`, `test_sandboxie_template`, `test_hostcommands` |
| UAC and service routing | `test_uacidentity`, `test_uactoken`, `test_serviceclient`, `uac_pipe_probe` |
| Services and setup | `test_service`, `test_startup`, `test_setup`, `test_uninstall` |
| Upgrade state and file protection | `test_upgrade`, `test_upgrade_files`, `test_upgrade_registration` |
| Language bar, About and registration | `test_langbar`, `test_about`, `test_register`, `test_register_other`, `test_registration_views` |
| Host runtime compatibility | `test_tsf_runtime_imports`, `test_Fcitx5SetupHelper_runtime_imports`, `test_Fcitx5Upgrade_runtime_imports`, `test_tsf_runtime_host`, `test_tsf_runtime_audit` |

Use `ctest --test-dir <build> -N` for the actual configured list. Root/upstream
tests are disabled by default; this is not the full upstream test suite.

- `ipc_probe`: real `nihao` candidates and Chinese commits, number/space
  selection, backspace, cancellation, Ctrl+C pass-through, Ctrl+Space switching,
  and separate connections/context ownership.
- `settings_probe`: real full/Xiaohe/Ziranma candidates and Chinese commits,
  English mode preservation, invalid settings, revision conflicts, idempotence
  and the `GB Standard` configuration mapping. The UI test separately exercises
  restart persistence, applying options and discarding unapplied drafts.
- `inputmethods_probe` / `scripts/test-inputmethods.ps1`: installed method catalog,
  ordered list changes, shared Pinyin options, real Wubi/Pinyin/Shuangpin commits,
  cycling, removal of English keyboard or the active entry, deletion protection,
  revision conflicts and restart persistence. The script also runs the Pinyin
  probes and `tsf_probe <DLL> --cycles`; it is separate from default CTest.
- `tsf_probe`: actual DLL loading/factory/interfaces, actual Windows TSF document
  contexts backed by an in-memory ITextStoreACP, composition, Chinese commit,
  synchronous-to-asynchronous edit fallback, cancellation, late edits and focus
  isolation. Because this TIP is not registered, a test-only adapter substitutes
  key-sink subscription, and the probe explicitly supplies key/focus callbacks.
  This does not verify Windows' registered TIP activation or OS key dispatch.
  A test-only language-bar manager captures the input mode item instead of
  publishing an unregistered service to the desktop. The probe checks add/remove,
  current-method labels/tooltips, keyboard/non-keyboard state and notifications,
  explicitly invoked cycle clicks, activation rollback and detached-item
  lifetime/late-click isolation; it does
  not verify taskbar rendering or actual OS language-bar click dispatch.
  Mode regressions use a real thread-manager keyboard compartment, including
  open/close notifications, cancellation, focus changes and a simulated failure
  to register the exact Ctrl+Space preserved key. Preserved-key callbacks are
  explicitly invoked by the adapter.
- Default CTest: helper functions, protocol framing/snapshots, UTF-8/UTF-16/key policy,
  real pipe timeout/cancellation followed by a successful read, and language-bar
  COM identity, sink cookies, state notifications and nonblank/distinct fallback
  icon pixels for different methods.
- `test_candidate`: production DirectWrite/Direct2D rendering at five synthetic
  DPIs, color emoji and monochrome VS15, ellipsis/layout reuse, owner awareness,
  thread DPI restoration, focus, positioning, immediate paint and bounded
  resource recreation. See
  [candidate verification](windows-candidate-rendering.md#verification).
- `test_candidate_tsf`: a real TSF context checks retained candidate content and
  anchor during `TS_E_NOLAYOUT`, merged `ITfTextLayoutSink` repositioning through
  read-only sessions, caret fallback for clipped composition, timeout/recovery,
  commit/cancel, hidden owners and late focus/deactivation sessions. It also
  covers host UIElement show/hide, no-HWND data publication, optional language
  bar failure and sensitive/disabled context policy. Geometry regressions include
  zero-sized per-monitor input sites and clipped carets inside document bounds.
- `test_register`: actual DLL branding resource and Shell icon extraction,
  plus profile/category calls captured by fake COM managers to verify character
  lengths, DLL path, icon index, `SYSTRAYSUPPORT` and failure handling.
  `test_register_other` checks the opposite modern-category build setting. Neither
  test invokes system registration or proves taskbar rendering.
- `test_setup` and `test_uninstall`: user-profile backup/restore, registration
  ownership and maintenance checks, plus the production uninstall launcher with
  mocked system calls, including elevation, cancellation, silent operation and
  successful removal requiring restart (`3010`). No real installation occurs.

Optional `ENABLE_PINYIN_INTEGRATION_TESTS=ON` adds three probes to CTest;
a deployed Core must already be running. `TSF_PRELOAD_RUNTIME` adds a separate
old-runtime host regression. `dictionaries_probe`, `inputmethods_probe`,
`service_probe`, `appcontainer_probe`, `modern_input_probe` and `logging_probe`
are invoked separately; see [validation](build.md#validation),
[modern app acceptance](build.md#modern-app-acceptance) and
[developer logging](build.md#developer-logging).

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

Protocol v12 uses a desktop pipe name scoped to the user's SID and Windows session.
Its ACL permits only the current user, rejects remote clients, isolates context IDs
by connection, and permits multiple connections. Clients use cancellable
overlapped I/O with a 500 ms wait limit per read/write operation; this is not a
single 500 ms deadline for the entire request. Disconnected clients' contexts
are destroyed. The pipe name retains the historical `fcitx5-windows-v2-` prefix;
compatibility is checked using the framing/handshake version, which is v12.
Core verifies actual peer identity after Hello. Desktop management connections
accept a verified UAC linked-token login from the same user/session; elevated
TSF clients route service operations through the host Core. If Core is absent,
start it from the ordinary desktop session. See [UAC identity and service
control](windows-uac-ipc-design.md). Sandboxie security-mode anonymous
tokens have a narrowly scoped management-pipe exception described in the
[host-sharing guide](windows-sandboxie-design.md#进程策略).

AppContainer clients use a separate `-input` pipe with authenticated peer identity,
an input-only request allowlist and resource limits. They verify the fixed host
Core through a query-only process-handle handshake before sending input. Sandboxie
and AppContainer clients never start their own Core/Settings or access local
service state; the host Core must already be running. See
[modern transport and UI policy](windows-modern-apps-design.md).

`SetMode` chooses a selected keyboard/non-keyboard entry for the Windows
open/close compartment. Indicator clicks cycle through the user's ordered list;
Core matches configured keyboard shortcuts before routing keys to the engine.
Repeated mode requests are idempotent; changing input methods cancels preedit.
Windows can deliver a configured cycle shortcut as a keyboard open/close
compartment notification instead of a key-sink callback. TSF checks its cached
shortcut state and coalesces duplicate callbacks until the key is released.
Other explicit compartment changes use `SetMode`.

`tsf_probe <DLL> --cycles` checks the currently enabled list without changing
configuration. It simulates ordinary/preserved key callbacks, system-hotkey
compartment notifications in both callback orders, repeats and releases, and
indicator clicks using a real TSF context and keyboard compartment. It requires
a running Core; it does not verify real OS keyboard dispatch. The input-method
regression script runs it with the persisted three-method list.

Core, TSF and Settings must be rebuilt
and deployed together; older protocol versions do not connect to v12.

Each key response contains consumption, mode, the actual input-method ID/name,
icon label/path and settings-panel ID, commit, preedit, UTF-8 byte cursor,
revision, settings epoch, detailed-logging state, stable candidate generation
and the current candidate page. Reset cancels Core composition. PollState
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
and remaining validation. `ITfCandidateListUIElementBehavior` exposes host
selection/finalize/abort actions independently of popup geometry; Core checks
context ownership, focus, candidate generation and index before invoking the
real engine. The published list is the current bounded batch, not the entire
engine candidate set. The popup itself has no mouse selection and does not
insert candidate text.
Focus changes create/destroy remote contexts; the current implementation does
not persist separate unfinished preedits for multiple input fields.

## Windows Settings

Right-clicking the language-bar input mode item opens a native menu with
`输入法设置`, `用户数据文件夹`, `重启服务`, `关闭服务` and `关于`. The settings
command asks Core to launch the independent WinUI 3
`settings/Fcitx5Settings.exe`. The Core connection and the settings launch are
separate steps: a running Core can accept IPC while the settings window is
missing or unable to initialize. The real OS right-click dispatch still
requires registered-TIP validation; unit tests exercise menu construction and
commands.

Immersive/UIElement-only hosts use the system TSF menu; AppContainer clients
expose only Settings. Desktop menus and About also negotiate UIElement display
permission. Unsupported secure/COM-less activation and explicit password scopes,
read-only or disabled contexts pass through without sending engine input. This
policy covers these explicit states; other password/custom controls need validation.

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

`关于` opens a nonmodal native window on the TSF thread, showing the DLL target
architecture, CI build number, commit/time and project/license links. It does
not require Core, Settings or Windows App SDK. Reopening activates the existing
window; TSF deactivation closes it.

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
instead of being killed. Elevated desktop TSF uses the host Core for these
commands, preserving the ordinary desktop's service identity and stop state.

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

The UI has an ordered input-method list with add/remove/move controls and shared
settings panels for Pinyin/Shuangpin, table methods and keyboard. List changes
save immediately; engine-option edits save with `应用` (Apply), leaving the
window open. Switching pages with an unapplied draft prompts to save, discard
or cancel; closing the window discards unapplied edits. Pinyin options include
eight built-in Shuangpin profiles and fuzzy Pinyin; an existing custom profile
is preserved but cannot be created/imported here. The app supports
single-instance activation, system theme resources and PerMonitorV2 DPI.
Key-list options support [shortcut recording](windows-settings-design.md#快捷键录制).
Global Settings has General, Developer options, Plugins and Shortcuts tabs.
UI language applies to Core/Settings after restarting services, while detailed
logging and Core shortcuts apply immediately. Plugins lists discovered addon
metadata and status without triggering loads. Language, logging and shortcuts
share a revision in `conf/windows-ui.conf` v3, separate from input-method
settings; v1/v2 gain default shortcuts and v1 loads with logging disabled. Native menus/About refresh
their language in the background. See [UI localization](windows-ui-localization-design.md)
and [developer diagnostics](windows-developer-logging-design.md).

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
TSF and Settings must be rebuilt and deployed together for protocol v12.

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

`GetInputMethods` / `SetInputMethods` manage the catalog and ordered enabled
list; `GetMethodConfig` / `SetMethodConfig` expose engine-declared options.
These requests, legacy Pinyin `GetSettings` / `SetSettings`, and `OpenSettings`
are context-independent with context ID zero. `CycleInputMethod` and
`SelectInputMethod` require an owned, focused context.

Core alone writes the authority file `conf/windows.conf` v2 under the Fcitx user
package configuration directory. It stores the ordered list, engine options,
Pinyin scheme/profile and shared revision; v1 settings migrate to English
keyboard plus the previously selected Pinyin entry. Pinyin/Shuangpin share
engine options, while table options are stored per entry. Upstream Pinyin/table
configuration files are mirrors. Changing methods cancels unfinished preedit;
TSF restores the current entry in new/reconnected contexts. Legacy Pinyin scheme
updates keep English contexts in direct mode.
Save errors and stale-revision conflicts are reported. Timed-out writes are
queried before retrying. Core/TSF/settings must be deployed together.

Pinyin and Shuangpin share a `WindowsPunctuation` option for Chinese/English
punctuation. Core persists it in `Options/pinyin` and applies it to the upstream
punctuation module, which is also used by table methods. TSF routes idle OEM
punctuation and Shift+number keys to Core for conversion; unconsumed keys pass
through. See [settings behavior and punctuation tests](windows-settings-design.md#扩展约定).

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

These service/UI probes use explicit adapter callbacks; registered-TIP OS menu
dispatch remains unverified.

## Remaining Validation and Limits

- Automated language-bar and TSF probes cover the indicator's state, labels and
  commands, but not Windows' registered event dispatch. OS key/hotkey dispatch
  and input in Notepad, browsers and other hosts still need broader validation;
  the test application and Windows-version matrix were not recorded.
- Candidate rendering, DPI behavior and screen-reader integration are not
  verified in those applications. Candidate UIElement/Behavior is implemented
  and covered by unregistered probes; full accessibility remains unverified.
- Input-indicator theme coloring, Explorer restart, multiple-monitor DPI and
  broader Windows-version coverage still need registered-TIP validation.
  The automated probes do not verify taskbar rendering.
- The popup has keyboard-only selection; host candidate actions are implemented
  through UIElement/Behavior. Popup mouse interaction and an ordinary
  notification-area tray are not implemented.
- Unified Windows 11 x64 installers provide base/full editions, each with or
  without bundled runtimes. CI runs machine-only install/registration/uninstall
  smoke tests for both no-runtime editions. Full user initialization, deferred
  upgrades and reboot cleanup still need system testing. See
  [installer status](windows-installer-design.md).
- Xiaohe and Ziranma Shuangpin commits are validated by probes; input-method
  probes also cover real Wubi commits. Other profiles, Wubi and other table
  methods in real applications and registered modern-app compatibility need
  broader validation. Rime is built with its shared data in the full edition;
  its real application input still needs coverage. CI/Nightly declares modern
  categories; signing and real UWP dispatch remain pending.
- Password scopes and disabled contexts have pass-through checks, but complete
  password/secure input, dead keys, AltGr/non-US layouts, forwarded keys and
  surrounding-text editing remain incomplete or unverified. `SECUREMODE` and
  `COMLESS` are not declared.
- Polling is a minimal deferred-output mechanism, not an asynchronous push queue
  or transactional exactly-once delivery across process failures. An IPC timeout
  can lose the pending input; it does not guarantee recovery of a consumed key.
- CI covers x64 Core, x64/x86 TSF probes, Pinyin/Shuangpin/Wubi input and
  Wenzhounese plugin commits. Real x86 application input remains pending.
  ARM64 is outside the current CI build matrix.

The first-run Pinyin history load may log a missing/empty-history error; the
system dictionary/model and actual Chinese conversion are verified separately.
