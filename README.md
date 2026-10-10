# Fcitx5 Windows

An in-progress Windows port of Fcitx5, with a basic Pinyin input path on AMD64.
The pinned versions are Core 5.1.22, chinese-addons 5.1.15 and libime 1.1.17.
TSF automatically starts Core in the background; the settings app starts on
demand. There is no unified installer yet.

## Architecture

Core, TSF and Settings are separate build projects with different Windows toolchains:

| Component | Toolchain | Responsibility |
| --- | --- | --- |
| `Fcitx5.exe` | MSYS2 clang64, Windows GNU runtime | Hosts Fcitx5 and the real Pinyin engine |
| `fcitx5-x86_64.dll` | LLVM Clang, Windows SDK and ATL | Integrates with applications through TSF, edits text and displays candidates/input mode |
| `Fcitx5Settings.exe` | MSVC, MSBuild, C++/WinRT and WinUI 3 | Displays settings; Core saves and applies changes |

The DLL runs inside the host application and connects to Core through a Named
Pipe scoped to the current user's SID and Windows session. No Core C++ objects
or runtime ABI cross the pipe. Pipe requests are dispatched to Core's event
loop; TSF applies replies through edit sessions on its owning thread.

Protocol v4 returns key consumption, Chinese commits, preedit, cursor and
candidate pages, and provides explicit mode switching and state polling.
TSF polls every 100 ms for delayed output and retries disconnected connections
every two seconds. Failed requests are not replayed; IPC or document-edit
failures can lose input. Keep Core, TSF and Settings on the same protocol version.

## Features and Status

- Real Pinyin preedit, Chinese candidate selection and commits, with a basic
  nonactivating candidate window and preedit underline.
- Number/Space selection, cancellation, basic shortcut pass-through and
  `Ctrl+Space` mode switching. Direct input lets the application handle keys.
- System keyboard open/close synchronization, including focus changes and
  reconnection. Focus loss cancels preedit; unfinished text is not retained
  separately for multiple input fields.
- A Windows input-indicator item beside the input-method icon: selected/active
  Fcitx5 shows `中` in Pinyin mode and `A` in direct input;
  switching away removes the item. Clicking it toggles the same mode used by
  `Ctrl+Space`. This is a TSF Language Bar item, not a separate notification-area
  tray icon. Its visibility also depends on Windows language-bar settings.

The user confirmed the input indicator works after registering the updated DLL.
Automated probes cover real Pinyin IPC and actual TSF contexts, but substitute
key-sink and language-bar adapters and invoke callbacks explicitly. They do not
verify OS key dispatch or taskbar rendering. Broader application compatibility,
candidate appearance, themes, Explorer restart and multiple-monitor DPI still
need testing.

Only AMD64 Core/TSF/Pinyin has been exercised locally. The Core CI also builds
ARM64; this does not establish ARM64 Pinyin or TSF support. Full Pinyin and
Xiaohe/Ziranma Shuangpin commits are covered by IPC and TSF probes. Wubi and
Rime are not validated. Candidate mouse selection, TSF UIElement/accessibility,
surrounding text, dead keys/AltGr, password/secure contexts and
Windows Store compatibility are not claimed as supported.

Right-clicking the input mode item provides `输入法设置` (settings),
`重启服务` (restart services) and `关闭服务` (stop services).
The independent WinUI 3 settings app selects full/double pinyin and built-in
Shuangpin profiles; changes are saved and applied by Core. It uses x64 Windows
App SDK 1.8 framework-dependent deployment. Build it with
`scripts/build-settings.ps1 -Prefix "$PWD/dist/pinyin"` and install the matching
runtime separately. Actual window save/cancel, restart and single-instance
behavior were tested; registered-indicator right-click dispatch and multi-monitor
DPI still require validation. See [the settings design](docs/windows-settings-design.md).

## Build and Validate

See [detailed prerequisites, deployment and validation](docs/pinyin.md).
Core and TSF require CMake 3.27+ and Ninja. Core additionally needs MSYS2
Clang/pkgconf/ECM/dlfcn/libuv/gettext, Boost/iostreams and zstd. TSF needs the
Windows SDK, ATL, LLVM Clang/llvm-rc and ImageMagick (`magick`).
Settings uses Visual Studio MSBuild, MSVC and NuGet with Windows SDK 10.0.22000.0+.

From the repository root, build and deploy Core to `dist/pinyin`:

```powershell
git submodule update --init --recursive
./scripts/build-pinyin.ps1 -DataMode Prebuilt
```

The script applies the repository's compatibility patches at the pinned
submodule revisions. `Prebuilt` uses a SHA256-pinned dictionary/model archive;
it does not load Linux libraries. The script's default `Source` data-generation
mode has not been validated end to end locally. A plain root CMake build does
not build and deploy libime, chinese-addons and Pinyin data.

In an initialized Visual Studio x64 development environment, using LLVM Clang
instead of the MSYS2 compiler, build TSF separately:

```powershell
cmake -S win32 -B win32/build/pinyin-tsf -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build win32/build/pinyin-tsf
./scripts/deploy-tsf.ps1 -Prefix "$PWD/dist/pinyin" -TsfBuild "$PWD/win32/build/pinyin-tsf"
```

An old cache may select the SDK's `rc.exe` for the GNU-style Clang driver.
Reconfigure with `-DCMAKE_RC_COMPILER="C:/Program Files/LLVM/bin/llvm-rc.exe"`
if resource compilation fails for that reason.

Build and deploy the independent settings window after building Core. This
copies `Fcitx5Settings.exe` and its Windows App SDK files to the `settings`
directory that Core uses when the language-bar command is selected:

```powershell
./scripts/build-settings.ps1 -Configuration Release -Prefix "$PWD/dist/pinyin"
```

The settings application is framework-dependent. Install the matching x64
Windows App SDK 1.8 Framework/DDLM runtime and Visual C++ runtime for the
current user before opening it. The build script does not install these
system runtimes.

With no existing Core or Settings process, run the integration checks:

```powershell
./scripts/test-pinyin.ps1
./scripts/test-service.ps1 -WithSettings
./win32/tests/test_scripts.ps1
```

`test-pinyin.ps1` starts Core, runs real Pinyin IPC, settings and TSF probes plus
eight CTest tests, then stops the Core it started. `test-service.ps1` loads the
deployed DLL and checks automatic Core startup, service stop/restart, manual-stop
persistence and, with `-WithSettings`, the actual WinUI window lifecycle.
Tests include language-bar state/lifetime, embedded DLL icon extraction
and registration arguments captured by fake COM managers. `test_scripts.ps1`
checks registration/release paths and behavior with mocked system mutations;
it requires the default deployed DLL but does not register it. These build and
validation scripts do not register the DLL or modify input-method settings;
the separate registration/release helpers do.

## Run

Keep the complete deployment tree together:

```text
dist/pinyin/
  bin/Fcitx5.exe
  tsf/fcitx5-x86_64.dll
  settings/Fcitx5Settings.exe
  lib/
  share/
```

The DLL is loaded by Windows, not launched like an EXE. After deployment,
register it from the repository root:

```powershell
./win32/scripts/install.ps1 -WhatIf
./win32/scripts/install.ps1
```

The script requests administrator privileges if needed, waits for registration
and reports failures. Its default DLL is `dist/pinyin/tsf/fcitx5-x86_64.dll`,
resolved relative to the repository regardless of the current working directory.
Use `-Prefix` for another deployment tree, or `-DllPath` for an explicit DLL;
these options are mutually exclusive. A DLL registered from a build directory
cannot locate the deployed Core automatically. Registration does not copy files
or install system runtimes.

Reopen applications that loaded the previous DLL. Use `Win+Space` to select
Fcitx5; if it is absent, add the Fcitx5 keyboard in Windows' Chinese language
options. TSF activation starts the adjacent `bin/Fcitx5.exe` in the background,
without a console window. Concurrent applications share one Core per user and
login session. Settings remains closed until requested.

Use `Win+Space` to select `Fcitx5`, then type `nihao` in a text application.
Use number keys or Space to select a candidate, and `Ctrl+Space` to switch
between Pinyin and direct input, or click the input mode icon. English mode
shows `A` without deselecting Fcitx5. Core must remain running for Chinese input.

Right-click the Fcitx5 input-mode item in the Windows input indicator and
choose `输入法设置` to open the settings window. Core launches
`dist/pinyin/settings/Fcitx5Settings.exe`; the settings executable is not part
of TSF registration and must have been deployed by `build-settings.ps1` first.
The menu's service commands behave as follows:

- `重启服务`: gracefully restart Core, or start it if absent. Reopen Settings
  only if it was already open. Uncommitted preedit and settings drafts are discarded.
- `关闭服务`: gracefully stop Core and Settings and pause automatic startup for
  this login, including in other applications. Choose `重启服务` to resume.
  The input indicator remains available. Stopping services does not unregister TSF.

Service operations run on a worker thread and use user/login-scoped coordination.
Startup failures share a 10-second retry interval. A hung process reports a
timeout; the service menu does not forcibly terminate it.

If the menu reports that settings cannot be opened, choose `重启服务` if services
were manually stopped, and check that the settings executable exists at that exact path. A
successful `ipc_probe --ready` confirms the Core connection; `OpenSettings`
can still fail when the settings deployment or its Windows App SDK runtime is
missing. If Core and TSF were rebuilt after a protocol change, deploy and
restart all three components together. Real registered-indicator right-click
dispatch still requires validation; the automated probes invoke menu callbacks
through test adapters.

The branding icon is embedded in the DLL. Registration points the profile at
that DLL and declares `GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT`; `penguin.ico` is only a
build input. Re-register after changing profile/category metadata, and restart
test applications to load the rebuilt DLL. Rebuild Core, TSF and Settings for a
protocol upgrade.

To unregister TSF, first use `关闭服务`, then run:

```powershell
./win32/scripts/uninstall.ps1 -WhatIf
./win32/scripts/uninstall.ps1
```

Unregistration requests elevation when necessary. It does not stop services,
delete files or unload the DLL from existing applications. Close those applications
before replacing the deployed DLL, or use another isolated deployment tree.
To diagnose/release a loaded DLL, preview the release helper:

```powershell
.\win32\scripts\release-tsf.ps1 -Force -WhatIf
```

All three helpers default to the deployed DLL and support `-Prefix`, `-DllPath`
and `-WhatIf`. Previewing requires no elevation and makes no registration or
process changes. To inspect another deployment:

```powershell
./win32/scripts/release-tsf.ps1 -Prefix "$PWD/dist/another" -Force -WhatIf
```

Without `-WhatIf`, the release helper requires an elevated PowerShell to
unregister TSF; `-Force` also terminates verified DLL-owner
processes, which can include editors, browsers and Explorer. Save work before
using it. Handle/`tasklist /m` results are checked against the exact loaded
module path; owners whose paths cannot be verified are not terminated.
`-SkipUnregister` skips unregistration. The helper does not stop Core or Settings
as services; use the service menu before updating the complete deployment.

If Sysinternals Handle is not in `PATH`, provide its path explicitly with
`-HandlePath`. Without it, the script uses Windows `tasklist /m` and accessible
process module lists. Handle is not run under `-WhatIf`, and the script does not
automatically accept its EULA.

## Credits

* [fcitx5](https://github.com/fcitx/fcitx5): LGPL-2.1-or-later
* [weasel](https://github.com/rime/weasel): GPL-3.0-only
