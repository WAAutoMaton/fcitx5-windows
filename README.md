# Fcitx5 Windows

An in-progress Windows port of Fcitx5, with a basic Pinyin input path on AMD64.
The pinned versions are Core 5.1.22, chinese-addons 5.1.15 and libime 1.1.17.
There is no unified installer or automatic Core startup yet.

## Architecture

Core and TSF are separate build projects with different Windows toolchains:

| Component | Toolchain | Responsibility |
| --- | --- | --- |
| `Fcitx5.exe` | MSYS2 clang64, Windows GNU runtime | Hosts Fcitx5 and the real Pinyin engine |
| `fcitx5-x86_64.dll` | LLVM Clang, Windows SDK and ATL | Integrates with applications through TSF, edits text and displays candidates/input mode |

The DLL runs inside the host application and connects to Core through a Named
Pipe scoped to the current user's SID and Windows session. No Core C++ objects
or runtime ABI cross the pipe. Pipe requests are dispatched to Core's event
loop; TSF applies replies through edit sessions on its owning thread.

Protocol v3 returns key consumption, Chinese commits, preedit, cursor and
candidate pages, and provides explicit mode switching and state polling.
TSF polls every 100 ms for delayed output and retries disconnected connections
every two seconds. Failed requests are not replayed; IPC or document-edit
failures can lose input. Keep Core and TSF on the same protocol version.

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
ARM64; this does not establish ARM64 Pinyin or TSF support. Shuangpin, Wubi and
Rime are not validated. Candidate mouse selection, TSF UIElement/accessibility,
configuration UI, surrounding text, dead keys/AltGr, password/secure contexts and
Windows Store compatibility are not claimed as supported.

## Build and Validate

See [detailed prerequisites, deployment and validation](docs/pinyin.md).
Both projects require CMake 3.27+ and Ninja. Core additionally needs MSYS2
Clang/pkgconf/ECM/dlfcn/libuv/gettext, Boost/iostreams and zstd. TSF needs the
Windows SDK, ATL, LLVM Clang/llvm-rc and ImageMagick (`magick`).

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
```

An old cache may select the SDK's `rc.exe` for the GNU-style Clang driver.
Reconfigure with `-DCMAKE_RC_COMPILER="C:/Program Files/LLVM/bin/llvm-rc.exe"`
if resource compilation fails for that reason.

Before starting Core manually, run the integration checks:

```powershell
./scripts/test-pinyin.ps1
```

This starts a Core process, runs real Pinyin IPC and TSF probes plus six CTest
tests, then stops the Core it started. It refuses to run alongside an existing
Core. Tests include language-bar state/lifetime, embedded DLL icon extraction
and registration arguments captured by fake COM managers. These build and
validation scripts do not register the DLL or modify input-method settings;
the separate registration/release helpers do.

## Run

After building the TSF project in `win32/build/pinyin-tsf`, register the DLL from
the repository root in an elevated PowerShell:

```powershell
$dll = (Resolve-Path ".\win32\build\pinyin-tsf\dll\fcitx5-x86_64.dll").Path
& "$env:WINDIR\System32\regsvr32.exe" $dll
```

Start the Core from a normal PowerShell in the repository root:

```powershell
$core = (Resolve-Path ".\dist\pinyin\bin\Fcitx5.exe").Path
$coreProcess = Start-Process -FilePath $core -WorkingDirectory (Split-Path $core) -PassThru
```

Use `Win+Space` to select `Fcitx5`, then type `nihao` in a text application.
Use number keys or Space to select a candidate, and `Ctrl+Space` to switch
between Pinyin and direct input, or click the input mode icon. English mode
shows `A` without deselecting Fcitx5. Core must remain running while the TSF is
used. Keep the deployed `bin`, `lib` and `share` directories together.

The branding icon is embedded in the DLL. Registration points the profile at
that DLL and declares `GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT`; `penguin.ico` is only a
build input. Re-register after changing profile/category metadata, and restart
test applications to load the rebuilt DLL. Rebuild both Core and TSF for a
protocol upgrade.

To stop the Core started above, run `$coreProcess | Stop-Process` in the same
normal PowerShell. To unregister TSF, run this from the repository root in an
elevated PowerShell:

```powershell
$dll = (Resolve-Path ".\win32\build\pinyin-tsf\dll\fcitx5-x86_64.dll").Path
& "$env:WINDIR\System32\regsvr32.exe" /u $dll
```

Unregistering does not unload the DLL from existing applications. Close those
applications before rebuilding, or use another build directory. The release
helper defaults to the DLL path above; use `-DllPath` for another build. From
an elevated PowerShell, preview its actions with:

```powershell
.\win32\scripts\release-tsf.ps1 -Force -WhatIf
```

Without `-WhatIf`, it unregisters TSF; `-Force` also terminates detected DLL-owner
processes, which can include editors, browsers and Explorer. Save work before
using it. Detection via Handle or `tasklist /m` can match the same DLL filename
in other build directories. `-SkipUnregister` skips unregistration.

If Sysinternals Handle is not in `PATH`, provide its path explicitly with
`-HandlePath`. Without it, the script uses Windows `tasklist /m` and accessible
process module lists.

## Credits

* [fcitx5](https://github.com/fcitx/fcitx5): LGPL-2.1-or-later
* [weasel](https://github.com/rime/weasel): GPL-3.0-only
