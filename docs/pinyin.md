# Windows Input-Method Integration

This guide describes the AMD64 architecture, Core/TSF/Settings integration,
individual component builds and validation scope. For installation and usage,
see [Install](../README.md#install) and [Run](../README.md#run) in the README;
for unified build options and CI, see [build.md](build.md).

## Versions and Architecture

Pinned revisions (`cmake/pinyin-lock.json`): Fcitx5 5.1.22,
`fcitx5-chinese-addons` 5.1.15 and libime 1.1.17 (with libime's pinned KenLM
submodule). `fcitx5-chinese-addons` uses `HistoryBigram::WordWithCode` and
Pinyin context APIs from libime 1.1.17.

Core (`CMakeLists.txt`), TSF (`win32/CMakeLists.txt`) and Settings
(`win32/settings/Fcitx5Settings.vcxproj`) are independent projects. Core runs in
a separate MSYS2 `clang64` process; x64 and x86 TSF DLLs are compiled with
standalone LLVM + Windows SDK/ATL and loaded into host application processes.
No C++ object, allocator or engine ABI crosses the Named Pipe. Pipe service
threads dispatch work to Core's main loop via `EventDispatcher`; document edits
run inside TSF edit sessions on the TSF owning thread.

| Source | Responsibility |
| --- | --- |
| [src/main.cpp](../src/main.cpp) | Host initialization, in-memory input group and Pinyin warmup |
| [src/windowsfrontend.cpp](../src/windowsfrontend.cpp) | Windows InputContext and Core pipe service |
| [src/windowssettings.cpp](../src/windowssettings.cpp) | Pinyin scheme/profile application and authoritative persistence |
| [src/windowsinputmethods.cpp](../src/windowsinputmethods.cpp) | Input-method catalog, ordered selection, shared options and cycling |
| [src/windowsdictionaries.cpp](../src/windowsdictionaries.cpp) | Dictionary directory scans, addon reload and status queries |
| [src/windowsplugins.cpp](../src/windowsplugins.cpp) | Plugin package discovery, DLL search directories and addon status |
| [src/windowsstatusmenu.cpp](../src/windowsstatusmenu.cpp) | Generic `StatusArea` / `Action/Menu` snapshot and activation bridge |
| [src/windowslogging.cpp](../src/windowslogging.cpp) | Persistent Fcitx5 engine log sink and dynamic Info/Debug threshold |
| [win32/ipc/protocol.h](../win32/ipc/protocol.h) | Shared v17 framing and snapshot format |
| [win32/tsf/pipeclient.cpp](../win32/tsf/pipeclient.cpp) | TSF pipe client and bounded overlapped I/O |
| [win32/tsf/EditSession.cpp](../win32/tsf/EditSession.cpp) | Composition and document edits |
| [win32/tsf/langbaritem.cpp](../win32/tsf/langbaritem.cpp) | Input mode button, dynamic/fixed menus and lifetime |
| [win32/tsf/statusmenu.cpp](../win32/tsf/statusmenu.cpp) | Immutable status-menu view, command IDs and action queue |
| [win32/tsf/servicecontrol.cpp](../win32/tsf/servicecontrol.cpp) | Background service commands and user-data folder opening |
| [win32/tsf/candidatewindow.cpp](../win32/tsf/candidatewindow.cpp) | DirectWrite/Direct2D candidate popup (vertical/horizontal) and DPI positioning |
| [win32/tsf/candidateuielement.cpp](../win32/tsf/candidateuielement.cpp) | Host-readable `ITfCandidateListUIElement` / `Behavior` |
| [win32/settings/SettingsWindow.cpp](../win32/settings/SettingsWindow.cpp) | WinUI input-method list, language-grouped add window, engine/global panels and background IPC |
| [win32/dll/register.cpp](../win32/dll/register.cpp) | COM, profile, branding icon and category registration |

### Input Methods and Indicator

`windowskeyboard` is a static direct-input engine without XKB. It fills Core's
first group slot and publishes multilingual keyboard entries with `language`
metadata for the Settings picker; all keyboard entries pass keys through using
the active Windows keyboard layout.

The `base` edition provides Pinyin, Shuangpin and eight table entries:

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

The `full` edition adds Rime, Wenzhounese, Zhuyin and 25 `fcitx5-table-extra`
tables (including Wubi98, Cangjie5 and Jyutping Table). See
[bundled plugins](windows-plugins.md#仓库插件与-full-包).

An entry is marked available only when its addon and data load successfully. The
user's enabled list defaults to `keyboard-us` and `pinyin`. Settings supports
adding, removing (including English keyboard, keeping at least one available
entry) and reordering entries; its separate add window groups entries by
language, placing languages with non-keyboard methods first.

Switching and state synchronization:

- Left-clicking the indicator (`CycleInputMethod`) or pressing the cycle
  shortcut (`Ctrl+Space` by default) advances through the enabled list, skipping
  unavailable entries. **Global Settings > Shortcuts** also configures direct
  selection for entries 1–5 (`Ctrl+Alt+1…5` by default), using original list
  positions and passing through when absent or unavailable.
- Changing input methods cancels unfinished preedit. TSF restores the current
  entry on focus changes and reconnection.
- `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE` reflects whether the active entry is a
  non-keyboard method; explicit system open/close changes use `SetMode` to
  switch between enabled non-keyboard and keyboard entries (retaining the
  current method if no matching entry is enabled).
- Core prewarms Pinyin before accepting pipe clients so model loading does not
  consume the first key's IPC timeout.

Language Bar indicator:

- TSF publishes one `GUID_LBI_INPUTMODE` button while activated and removes it
  on deactivation. It renders the current method's PNG icon via WIC or falls
  back to a compact GDI label (`拼`, `双`, `A` for English keyboard). The
  tooltip shows the method name and active engine submode when available.
- `TF_LBI_STYLE_TEXTCOLORICON` is intentionally omitted so Windows preserves
  multicolor method icons.
- After removal, a retained language-bar object detaches from its message window
  and holds a DLL reference until released, preventing late clicks from
  affecting another activation.
- The desktop TIP registers `GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT`. Enabling
  `ENABLE_MODERN_TSF_CATEGORIES` (`-ModernApps`, default in CI/Nightly) also
  declares `UIELEMENTENABLED` and `IMMERSIVESUPPORT` (see
  [modern app acceptance](build.md#modern-app-acceptance)).
- `penguin.ico` is embedded in the TSF DLL as `IDI_FCITX5` (icon index 0);
  `RegisterProfile` points directly to the DLL path with character counts.
  Changes to categories or profile icon metadata require re-registration and
  restarting host applications.

## Compatibility Patches

Initialize pinned submodules and apply Windows patches kept in `patches/`:

```powershell
git submodule update --init --recursive
./scripts/apply-pinyin-patches.ps1
```

The script checks locked HEAD revisions, skips already-applied patches and
refuses conflicting local changes. Keep Windows changes in `patches/` rather
than creating local submodule commits.

- `fcitx5-windows-paths.patch`: resolves built-in resources under `share`,
  `share/fcitx5` and `share/locale`, and reads Core's generated addon/data
  search prefixes for independent plugin packages (see
  [plugin discovery](windows-plugins.md#目录与发现)).
- `fcitx5-dynamic-log-level.patch`: makes category thresholds atomic so Core can
  switch engine logging between `Info` and `Debug` at runtime.
- `libime-windows.patch`: appends `.exe` to imported tool paths and uses `;` for
  Windows model path lists to preserve drive letters.
- `chinese-addons-windows.patch`: accepts `std::filesystem::path` for wide
  Windows paths in dictionary loading and exposes extra dictionary status via a
  read-only subconfiguration.
- `chinese-addons-windows-table.patch`: enables Cangjie on Windows and resolves
  built-in table data from `share/libime`.
- `fcitx5-rime-windows.patch`: resolves `share/rime-data` via StandardPaths,
  converts native paths to UTF-8 and adapts schema checks, preedit cancellation
  and UI refresh for status-menu actions.
- `fcitx5-zhuyin-windows.patch` & `libpinyin-windows.patch`: adapt Windows file
  I/O, model saving/replacement, native data generators and missing/invalid
  model error reporting (see
  [Zhuyin builds](build.md#independent-input-method-plugins)).

## Build and Deploy Core

For a full AMD64 Release build (Core, TSF and Settings), use
`./scripts/build-and-deploy.ps1` (see
[unified build options](build.md#unified-build-options)).

To build Core, libime and `fcitx5-chinese-addons` independently (AMD64 `clang64`,
without Qt GUI tools, OpenCC or cloud Pinyin):

```powershell
./scripts/build-pinyin.ps1 -DataMode Prebuilt
```

`Prebuilt` extracts `usr/share/libime` and `usr/lib/libime` from the
SHA256-pinned Arch Linux libime 1.1.17 archive (verified compatible with the
Windows build). `Source` downloads upstream source datasets and compiles models
locally (high time/memory cost; not validated end to end locally):

```powershell
./scripts/build-pinyin.ps1 -DataMode Source
```

| Build Option | Behavior |
| --- | --- |
| `-MSYS2Root` | MSYS2 root (default `C:/msys64`, `clang64` toolchain) |
| `-DependencyPrefix` | Extra `clang64` Boost/zstd prefix |
| `-Prefix` | Deployment directory inside the repository (default `dist/pinyin`) |
| `-BuildRoot` | Parent for `pinyin-core`, `pinyin-libime`, `pinyin-addons` (default `build`; unified script uses `build/all`) |
| `-DataArchive` | Local archive verified against the locked SHA256 for `Prebuilt` mode |
| `-Jobs` | Parallel build jobs (default `6`) |

Prebuilt archives are cached in `build/pinyin-data` and reused across
`-BuildRoot` values after SHA256 verification (checking `build/deps` and
`build/pinyin-data`). When downloading, `build-pinyin.ps1` tries
`prebuiltData.url` and then `prebuiltData.fallbackUrls` from
`cmake/pinyin-lock.json` until a SHA256-verified archive is obtained. The script recursively copies required MSYS2 runtime DLLs
into `bin/`. Always launch the deployed `dist/pinyin/bin/Fcitx5.exe` with `bin`,
`lib` and `share` kept together. User dictionaries remain under Fcitx5's user
data directories.

## Build and Test TSF

In an initialized Visual Studio x64 environment with standalone LLVM (`clang`,
`clang++`, `llvm-rc`), Windows SDK/ATL, Ninja and ImageMagick:

```powershell
cmake -S win32 -B win32/build/pinyin-tsf -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  '-DCMAKE_RC_COMPILER=C:/Program Files/LLVM/bin/llvm-rc.exe'
cmake --build win32/build/pinyin-tsf
./scripts/test-pinyin.ps1
```

`test-pinyin.ps1` launches an isolated Core with a temporary settings file
(refusing to run alongside an existing `Fcitx5` process), runs `ipc_probe`,
`settings_probe`, `tsf_probe` and CTest, then stops only the Core it started.
Logs go to `build/pinyin-test/core.stdout.log` and `core.stderr.log`. Pass
`-Prefix` and `-TsfBuild` for alternate directories.

To run CTest alone without starting Core:

```powershell
ctest --test-dir win32/build/pinyin-tsf --output-on-failure
```

Default CTest targets (`win32/tests/CMakeLists.txt`; run `ctest --test-dir <build> -N` for the exact configured list):

| Scope | Tests |
| --- | --- |
| DLL, protocol and input | `test_dll`, `test_protocol`, `test_input` |
| Persistence, UI language and diagnostics | `test_settingsfile`, `test_i18n`, `test_diagnostic_log`, `test_keycapture` |
| Candidate rendering and host UI | `test_candidate`, `test_candidate_tsf`, `test_candidateuielement` |
| Status menu, language bar, About and registration | `test_statusmenu`, `test_langbar`, `test_about`, `test_register`, `test_register_other`, `test_registration_views` |
| Transport and isolation | `test_transport`, `test_pipeidentity`, `test_isolationpolicy`, `test_sandbox`, `test_sandboxie_template`, `test_hostcommands` |
| UAC and service routing | `test_uacidentity`, `test_uactoken`, `test_serviceclient`, `uac_pipe_probe` |
| Services and setup | `test_service`, `test_startup`, `test_setup`, `test_uninstall` |
| Reboot upgrade state, activation, shutdown and files | `test_upgrade`, `test_upgrade_files`, `test_upgrade_registration`, `test_upgrade_activation`, `test_upgrade_shutdown` |
| Host runtime compatibility | `test_tsf_runtime_imports`, `test_Fcitx5SetupHelper_runtime_imports`, `test_Fcitx5Upgrade_runtime_imports`, `test_tsf_runtime_host`, `test_tsf_runtime_audit` |

Key probe coverage:

- **`ipc_probe`**: real `nihao` candidates/commits, number/space selection,
  backspace, cancellation, Ctrl+C pass-through, Ctrl+Space cycling and
  per-connection context ownership.
- **`settings_probe`**: real full/Xiaohe/Ziranma candidates and commits, English
  mode preservation, invalid settings, revision conflicts, idempotence and
  `GB Standard` mapping.
- **`inputmethods_probe` (`scripts/test-inputmethods.ps1`)**: catalog, ordered
  list updates, shared Pinyin options, real Wubi/Pinyin/Shuangpin commits,
  cycling, active/English removal, deletion protection, revision conflicts and
  restart persistence; also runs `tsf_probe <DLL> --cycles`.
- **`tsf_probe`**: loads the actual TSF DLL against an in-memory
  `ITextStoreACP` and real Windows TSF contexts/compartments, with test adapters
  for key-sink and language-bar callbacks (unregistered TIP). Covers
  composition, commits, sync-to-async fallback, cancellation, late edits, focus
  isolation, indicator updates/clicks and keyboard compartment notifications.
- **`test_candidate` & `test_candidate_tsf`**: production DirectWrite/Direct2D
  rendering (5 synthetic DPIs, color emoji/VS15, software DC fallback, vertical/
  horizontal layout, child-edit overflow and point anchors) plus real TSF
  context `TS_E_NOLAYOUT`, `ITfTextLayoutSink`, UIElement show/hide and
  sensitive context pass-through (see
  [candidate verification](windows-candidate-rendering.md#verification)).
- **`test_register` / `test_register_other` / `test_setup` / `test_uninstall`**:
  DLL icon extraction, fake COM registration/category verification for both
  modern-category build states, user-profile backup/restore and mocked
  elevation/silent/`3010` uninstall flows.

Optional CMake flags: `-DENABLE_PINYIN_INTEGRATION_TESTS=ON` adds `ipc_probe`,
`tsf_probe` and `settings_probe` to CTest (requires a running deployed Core);
`-DTSF_PRELOAD_RUNTIME=<path>` adds `test_tsf_old_runtime_host`. Additional
probes (`dictionaries_probe`, `inputmethods_probe`, `service_probe`,
`statusmenu_probe`, `appcontainer_probe`, `modern_input_probe`,
`candidate_render_probe`, `logging_probe`) are driven by the scripts in
[build.md](build.md#validation).

If a registered DLL is locked during rebuilds, close applications using it or
build in a separate directory. See
[DLL release helpers](build.md#registration-and-dll-management).

## Protocol and Behavior

Protocol **v17** uses a Named Pipe scoped to the user's SID and Windows session
(retaining the historical `fcitx5-windows-v2-` prefix). Its ACL permits only the
current user, rejects remote clients, isolates context IDs per connection and
supports multiple concurrent clients. Clients use cancellable overlapped I/O
with a 500 ms wait limit per read/write operation; timed-out requests are not
replayed. Core destroys a client's contexts on disconnect. Rebuild and deploy
Core, TSF and Settings together whenever the protocol version changes.

Access and isolation policies:

- **Elevated desktop (UAC)**: desktop management pipes accept a verified UAC
  linked-token login from the same user/session; elevated TSF routes service
  commands through the running host Core (see
  [UAC IPC](windows-uac-ipc-design.md)).
- **Sandboxie**: uses the management pipe via an explicit `OpenPipePath` rule
  with a narrow anonymous-token compatibility branch for security mode (see
  [Sandboxie](windows-sandboxie-design.md#进程策略)).
- **AppContainer**: uses the dedicated `-input` pipe with authenticated peer
  identity, a query-only `AuthenticateServer` host-handle check, an input-only
  request allowlist and resource quotas (see
  [modern apps](windows-modern-apps-design.md)).

Key handling, state and rendering:

- Key-test callbacks observe state without IPC or edits; actual key processing
  runs once and follows Core's consumption decision.
- `KeyReply` returns consumption, mode, active input-method ID/name, submode
  name/label, icon label/path, settings panel ID, commit, preedit, UTF-8 byte
  cursor, revision, settings revision, detailed-logging flag,
  `statusMenuRevision`, `candidateGeneration`, `candidateLayout` and the current
  bounded candidate batch.
- `PollState` runs on a 100 ms owning-thread timer while a foreground context
  exists (including during pending edits so settings updates cancel stale
  preedit); disconnected clients retry every 2 seconds.
- Edit sessions bind their original context, generation and immutable reply,
  serializing queued edits and rejecting stale generations. An edit failure
  after Core consumes a key resets input state.
- `SetMode` maps Windows keyboard open/close compartment changes to enabled
  non-keyboard/keyboard entries; TSF coalesces duplicate cycle-shortcut
  callbacks until key release. `tsf_probe <DLL> --cycles` tests these paths
  against a running Core.
- TSF maintains a persistent composition with dotted underline, converts UTF-8
  cursors to UTF-16, replaces the composition range on commit and cancels
  unfinished preedit on focus loss.
- Candidate rendering supports DirectWrite/Direct2D vertical and horizontal
  layouts with software DC fallback and Per-Monitor V2 DPI positioning (see
  [candidate rendering](windows-candidate-rendering.md)), while
  `ITfCandidateListUIElementBehavior` exposes host selection/finalize/abort
  actions validated by Core against context ownership, focus, generation and
  index.

## Windows Settings

Right-clicking the language-bar indicator opens a native menu with the active
engine's dynamic status actions/submenus followed by five fixed commands:
`输入法设置`, `用户数据文件夹`, `重启服务`, `关闭服务` and `关于`.
Immersive/UIElement-only hosts use `ITfMenu`; AppContainer menus expose only
Settings. Secure/COM-less activation, explicit password scopes, read-only and
disabled contexts pass keys through.

Individual Settings build and TSF deployment:

```powershell
./scripts/build-settings.ps1 -Configuration Release -Prefix "$PWD/dist/pinyin"
./scripts/deploy-tsf.ps1 -Prefix "$PWD/dist/pinyin" -TsfBuild "$PWD/win32/build/pinyin-tsf"
```

Settings uses unpackaged, framework-dependent Windows App SDK 1.8 deployment
(x64 Framework/DDLM >= `8000.994.2142.0` + Visual C++ runtime for the current
user). To launch Settings directly while Core is running:

```powershell
$settings = (Resolve-Path ".\dist\pinyin\settings\Fcitx5Settings.exe").Path
Start-Process -FilePath $settings -WorkingDirectory (Split-Path $settings)
```

Command and service behavior:

- **Core startup**: TSF activation starts adjacent `bin/Fcitx5.exe` on a worker
  thread; Settings starts on demand via `OpenSettings`. One Core runs per
  user/session (enforced by an instance mutex).
- **`用户数据文件夹`**: creates if needed and opens `%APPDATA%/Fcitx5`
  (`config/fcitx5` settings and `pinyin/user.dict`, `user.history`); works even
  while services are stopped.
- **`关于`**: nonmodal native window on the TSF thread showing architecture, CI
  build number, commit and project/license links; needs no Core or WinUI
  runtime.
- **`重启服务` / `关闭服务`**: graceful stop/restart using current-user
  SID/session/logon-scoped objects and a `.state` file under
  `%LOCALAPPDATA%/fcitx5` to persist manual stop for the current login (with a
  shared 10-second retry interval). If TSF is hosted inside Settings itself, a
  short-lived Core control process coordinates the exit. Hung processes time out
  rather than being killed by process name.

Diagnosing Core vs. Settings startup:

```powershell
& .\win32\build\pinyin-tsf\tests\ipc_probe.exe --ready
& .\win32\build\pinyin-tsf\tests\settings_probe.exe --open-settings
```

If `ipc_probe --ready` passes while `settings_probe --open-settings` fails, Core
is healthy and the issue is the Settings binary or Windows App SDK/VC++ runtime.

Settings UI overview:

- Ordered input-method list (immediate save) and shared panels for Pinyin /
  Shuangpin (8 built-in profiles + fuzzy Pinyin; preserves existing Custom
  profile), table methods and keyboard (**Apply** saves drafts; switching pages
  prompts to save/discard/cancel).
- **Global Settings** tabs: **General** (UI language + vertical/horizontal
  candidate layout), **Developer options** (detailed logging toggle +
  `%LOCALAPPDATA%/Fcitx5/logs` directory stats alongside default Info engine
  logs), **Plugins** (read-only addon status) and **Shortcuts** (cycling and
  direct-selection 1–5 with shortcut recording).
- Global preferences share a revision in `conf/windows-ui.conf` v4, separate
  from `conf/windows.conf`. See [UI localization](windows-ui-localization-design.md)
  and [developer diagnostics](windows-developer-logging-design.md).

### Third-Party Dictionaries

The Pinyin **词库** tab queries `GetDictionaries` every 2 seconds while visible
to display each discovered `.dict` file's actual status (`Loading`, `Loaded`,
`Failed` or `Disabled`). `打开第三方词库文件夹` creates and opens
`%APPDATA%/Fcitx5/pinyin/dictionaries` even if Core is unreachable.

Drop compatible Fcitx5/libime binary `.dict` files directly in this folder (no
subdirectories). To compile a UTF-8 libime text dictionary (e.g.,
`夜来风雨声 ye'lai'feng'yu'sheng 0`):

```powershell
& .\dist\pinyin\bin\libime_pinyindict.exe words.txt poetry.dict
```

Dictionaries are shared across full Pinyin and all Shuangpin profiles and
participate in normal libime scoring. Core scans immediate `.dict` / `.disable`
files once per second; two identical scans trigger `dictmanager` reload on
Pinyin's worker thread without restarting Core or clearing pending composition.
`name.dict.disable` disables `name.dict`; failed files are reported individually
and retried once modified again.

Run dictionary integration checks (with Core/Settings stopped):

```powershell
./scripts/test-dictionaries.ps1 -Prefix "$PWD/dist/pinyin" -TsfBuild "$PWD/win32/build/pinyin-tsf"
```

### Input Settings

`GetInputMethods` / `SetInputMethods`, `GetMethodConfig` / `SetMethodConfig`,
legacy `GetSettings` / `SetSettings` and `OpenSettings` are context-independent
(`contextId = 0`); `CycleInputMethod` and `SelectInputMethod` require an owned,
focused context.

Core is the sole writer of `conf/windows.conf` v2 (storing ordered list, engine
options, Pinyin scheme/profile and shared revision; v1 migrates automatically).
Pinyin/Shuangpin share engine options while table methods store per-entry
options; upstream `conf/pinyin.conf` and `table/*.conf` act as mirrors. Timed-out
writes should be re-queried before retrying. For isolated tests, Core accepts
`--windows-settings-file ABSOLUTE_PATH`.

Chinese/English punctuation is toggled from the status menu and persisted by the
upstream `punctuation` module in `conf/punctuation.conf` (shared across Pinyin,
Shuangpin and table methods using the module). Obsolete
`Options/pinyin/WindowsPunctuation` is dropped on read and removed on the next
save. TSF routes idle OEM punctuation and Shift+number keys to Core (see
[settings punctuation details](windows-settings-design.md#扩展约定)).

UI and service validation scripts:

```powershell
./scripts/test-settings-ui.ps1 -Prefix "$PWD/dist/pinyin" -TsfBuild "$PWD/win32/build/pinyin-tsf"
./scripts/test-service.ps1 -Prefix "$PWD/dist/pinyin" -TsfBuild "$PWD/win32/build/pinyin-tsf" -WithSettings
```

See [Windows settings design](windows-settings-design.md) for full details.

## Remaining Validation and Limits

- **Registered OS dispatch & application matrix**: OS key/hotkey/language-bar
  dispatch, taskbar theme coloring, Explorer restarts, mixed-DPI monitors and
  input across Notepad, browsers and complex hosts need live validation across
  Windows 10 1809+ and Windows 11.
- **Candidate popup & accessibility**: the self-drawn popup supports keyboard
  selection only (no mouse clicks); host actions work through UIElement/Behavior
  with unregistered probe coverage, while real host rendering and screen-reader
  integration remain pending. No ordinary notification-area tray icon is
  implemented.
- **Engines & profiles**: probes cover full Pinyin, Xiaohe/Ziranma Shuangpin,
  Wubi, Wenzhounese, Wubi98, Cangjie5, Jyutping Table, Standard Zhuyin (with
  model save/reload) and isolated Rime schemas (`luna_pinyin` /
  `luna_pinyin_simp`, F4 menu, redeployment). Remaining Shuangpin profiles,
  normal-user Rime deployment and real-app coverage for these engines need live
  validation.
- **Input edge cases**: full password/secure desktop input (`SECUREMODE` /
  `COMLESS` undeclared), dead keys, AltGr/non-US layouts, forwarded keys and
  surrounding-text editing are incomplete or unverified. Polling is a minimal
  deferred-output mechanism without transactional exactly-once recovery across
  IPC timeouts.
- **Installers, signing & architectures**: CI runs machine-only install/uninstall
  smoke tests for both no-runtime editions and x86/x64 probes. Bundled-runtime
  installs, interactive user initialization, reboot upgrades, code signing, real
  UWP/x86/UAC app input and ARM64 TSF remain pending.

Note: a first-run Pinyin history load may log a missing/empty-history message;
system dictionaries and Chinese conversion work normally.
