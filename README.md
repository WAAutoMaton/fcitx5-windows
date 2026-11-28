# Fcitx5 Windows

[中文文档](README_zh-cn.md) | [日本語](README_ja.md) | [한국어](README_ko.md)

An in-progress Windows port of Fcitx5 for AMD64. Core runs in a separate
process, x64 and x86 TSF DLLs integrate with applications, and a WinUI 3
settings app manages input methods and engine options.

The `base` edition provides Pinyin, Shuangpin and the `fcitx5-chinese-addons`
table input methods; the `full` edition adds Mozc (Japanese), Rime, SKK
(Japanese), Wenzhounese, Zhuyin and `fcitx5-table-extra`. Unified x64
installers are published by the CI and Nightly release workflow.

## Features

- **Chinese & multilingual input**:
  - Full Pinyin and eight built-in Shuangpin profiles (including Xiaohe and
    Ziranma), with preedit, candidate selection and Chinese commits.
  - Base table methods: Wubi, Wubi Pinyin, Cangjie, Dianbaoma, Erbi, Bingchan,
    Wanfeng and Ziranma (available when their engine and table data are
    installed).
  - Full edition extras: Mozc (Japanese), Rime, SKK (Japanese), Wenzhounese,
    Zhuyin and 25 additional tables (including Wubi98, Zhengma, Boshiamy,
    Cangjie5 and Jyutping Table).
  - Multilingual keyboard entries that pass keys through using the active
    Windows keyboard layout.
- **Candidate window**:
  - Nonactivating DirectWrite/Direct2D candidate popup with color-font
    rendering, software DC fallback and Per-Monitor V2 DPI positioning.
  - Configurable vertical (default) or wrapping horizontal candidate layout in
    **Global Settings > General**.
  - Keyboard selection with number keys or Space, plus TSF candidate
    UIElement/Behavior for host-rendered UIs.
- **Input indicator & status menus**:
  - Windows input-method indicator displaying the current method's icon or
    compact label (`A` for English keyboard) and tooltip name.
  - Dynamic right-click status menus exposing plugin modes, options and
    submenus (such as Rime schema selection and shared Chinese/English
    punctuation), followed by settings and service commands.
- **Settings & shortcuts**:
  - Add, remove and reorder enabled input methods; a separate add window groups
    available methods by language (placing languages with non-keyboard methods
    first).
  - Configurable cycle shortcut (`Ctrl+Space` by default) and direct selection
    for entries 1–5 (`Ctrl+Alt+1…5`), with interactive shortcut recording
    alongside manual text editing for both global and engine key-list options.
  - Third-party Pinyin dictionary status view with automatic background reload.
  - Read-only addon status page in **Global Settings > Plugins**, plus
    independent engine/resource plugin package discovery.
  - Simplified Chinese, Traditional Chinese, Japanese, Korean and English UI
    with a follow-system option.
- **Environment & diagnostics**:
  - Automatic background Core startup for standard desktop sessions, plus
    optional login startup in the installer.
  - Support for elevated desktop apps (UAC linked-token), AppContainer input
    transport, and Sandboxie host-sharing via an explicit pipe rule.
  - Persistent Fcitx5 engine logs (`Info` by default, `Debug` when detailed
    logging is enabled) and optional structured developer logs for Core, TSF
    and Settings (including Settings exit/exception diagnostics). Structured
    logs exclude input text.

## Install

The [Nightly release](https://github.com/WAAutoMaton/fcitx5-windows/releases/tag/latest)
publishes four x64 installers and their SHA256 checksums for Windows 10 version
1809 (build 17763) or later. Both editions include Core, x64/x86 TSF DLLs and
Settings:

- `base`: bundles `fcitx5-chinese-addons`.
- `full`: bundles `fcitx5-chinese-addons`, `fcitx5-mozc`, `fcitx5-rime`,
  `fcitx5-skk`, `fcitx5-table-extra`, `fcitx5-wenzhounese` and `fcitx5-zhuyin`.

Each edition comes in two runtime variants:

- `Fcitx5-<run>-<base|full>-x64-setup.exe`: bundles Visual C++ and Windows App
  Runtime installers.
- `Fcitx5-<run>-<base|full>-x64-no-runtime-setup.exe`: requires Visual C++ and
  x64 Windows App SDK 1.8 Framework/DDLM >= `8000.994.2142.0` to be installed
  beforehand for the current user.

The installer deploys to Program Files, registers both x64 and x86 TSF DLLs
sharing the x64 Core, configures the initiating user's input method and starts
Core. Optional login startup is unchecked by default and respects manual
service stops. See [installer details](docs/windows-installer-design.md).

Upgrades stage the new version while keeping the current version usable, return
`3010`, and finish after a Windows restart. See
[reboot upgrades](docs/windows-reboot-upgrade-design.md) for status queries,
cancellation and recovery.

To remove an installer-managed copy, use **Uninstall Fcitx5** in the Start menu
or Windows Installed Apps. User data and shared runtimes are preserved; locked
files finish removal after a Windows restart. The PowerShell registration
scripts below are for development deployments only.

## Build and Deploy

Prerequisites: CMake 3.27+, Git, MSYS2 `clang64` packages, standalone LLVM
(22+ when using MSVC 14.52+), ImageMagick, and Visual Studio with C++ tools,
Windows SDK, ATL, MSBuild and NuGet. Running Settings requires the Visual C++
runtime and x64 Windows App SDK 1.8 Framework/DDLM >= `8000.994.2142.0`. See
[prerequisites](docs/build.md#prerequisites) for the full dependency list.

From the repository root in Windows PowerShell, build and deploy the `base`
edition:

```powershell
./scripts/build-and-deploy.ps1
```

This initializes pinned submodules, applies Windows patches, builds AMD64
Release Core/Settings, x64/x86 TSF DLLs and setup/upgrade helpers, runs the
default TSF CTest suites, and deploys to `dist/pinyin` using checksum-verified
prebuilt dictionary/model data. Subsequent runs reuse the build cache.

To add all `full`-edition plugins using the same SDK:

```powershell
./scripts/build-plugins.ps1 -CorePrefix ./build/all/prefix -Prefix ./dist/pinyin
```

Additional plugin builds require Git LFS, matching `clang64` librime/schema
packages, and GLib/Berkeley DB for Zhuyin. See
[plugin build details](docs/build.md#independent-input-method-plugins).

Before overwriting an existing deployment, choose **Stop Services** and close
applications that loaded its TSF DLL. See [build options and deployment
details](docs/build.md) for custom paths, individual builds and validation.

## Input-Method Plugins

Input methods can be built and installed as independent engine or resource
packages. `fcitx5-chinese-addons` is built with Core; `fcitx5-table-extra`
reuses the base `table` engine with 25 tables; `fcitx5-zhuyin` bundles
libzhuyin built from pinned libpinyin sources with compiled models; Rime
bundles shared schema and OpenCC data.

### Install and Enable

Install packages into either directory:

| Location | Purpose |
| --- | --- |
| `%LOCALAPPDATA%/Fcitx5/plugins/<package>` | Per-user installation without administrator privileges |
| `<Core installation>/plugins/<package>` | Portable deployment beside `bin/Fcitx5.exe` |

1. Choose **Stop Services** from the input-method indicator's right-click menu.
2. Place the package's `bin`, `lib`, `share` and `licenses` directories inside
   `<package>` (`lib/fcitx5` for engine DLLs, `bin` for private dependency
   DLLs, `lib`/`share` for resources). Core scans direct package subdirectories
   and also supports the legacy shared `plugins/bin`, `plugins/lib`,
   `plugins/share` layout.
3. Choose **Restart Services**, open **Input Method Settings**, and add the new
   input method to the enabled list.

Stop services before updating or removing a plugin, then restart afterward.
Delete `<package>` to remove an independent package. **Global Settings >
Plugins** displays discovered addons and their Loaded, Not Loaded or Disabled
state, and provides a shortcut to the personal plugin folder.

### Compatibility

Build instructions live in each plugin's repository. Plugins must match Core's
architecture, Fcitx5/libime versions, Windows patches, Clang Windows GNU
toolchain and runtime libraries, and should share Core's public runtime DLLs
rather than overwriting them. See the
[Windows plugin guide](docs/windows-plugins.md) for layout and SDK details.

## Run

### Register a Development Deployment

```powershell
./win32/scripts/install.ps1
```

The script requests elevation when needed. Keep the complete `dist/pinyin` tree
together and reopen applications that loaded an older DLL. Select Fcitx5 with
`Win+Space` (add it under Windows Chinese language options if absent);
activation starts Core automatically.

To unregister a development deployment, choose **Stop Services** first, then:

```powershell
./win32/scripts/uninstall.ps1
```

Reopen applications to unload the DLL. See
[registration and DLL management](docs/build.md#registration-and-dll-management)
for previews, alternate paths and locked-DLL helpers.

### Everyday Input and Switching

- Cycle enabled input methods with `Ctrl+Space` (default) or by left-clicking
  the indicator. Unavailable entries are skipped.
- The default list contains English keyboard and Pinyin; add Shuangpin, table
  methods, Rime, Wenzhounese, Zhuyin or multilingual keyboards in Settings.
- Select candidates with Space or number keys. Switching input methods or
  losing focus cancels unfinished preedit.
- Configure the cycle shortcut and direct-selection shortcuts for entries 1–5
  under **Global Settings > Shortcuts** (clear a shortcut to disable it).
  Direct selection targets the list's original positions and passes through if
  absent or unavailable.
- Switch Chinese/English punctuation from the indicator's status menu; Pinyin,
  Shuangpin and table methods using the punctuation module share this state.
- With Rime selected, switch schemas from the status menu's schema submenu or
  press `F4` for the candidate-based schema menu. See
  [status menu details](docs/windows-status-menu-design.md).

### Indicator Menu and Settings

Right-click the input-method indicator to access the active engine's status
actions and submenus, followed by five fixed commands:

- **Input Method Settings**: manage the enabled list (saves immediately),
  configure engine options (saved with **Apply**) and inspect third-party
  Pinyin dictionaries.
- **Restart Services**: start or restart Core, and reopen Settings if it was
  open.
- **Stop Services**: stop Core and Settings and pause automatic startup until
  restarted.
- **User Data Folder**: open `%APPDATA%/Fcitx5` (settings and Pinyin learning
  data).
- **About**: show architecture, CI build number, commit and project/license
  links.

Under **Global Settings**:

- **General**: choose the UI language (restart services to update Core and
  Settings; native menus and About refresh in the background) and candidate
  window layout (**Vertical** or **Horizontal**).
- **Developer options**: toggle detailed logging immediately and open
  `%LOCALAPPDATA%/Fcitx5/logs`. Core persists engine logs (`Info` by default,
  `Debug` with detailed logging), and detailed mode adds structured JSON Lines
  logs for Core, TSF and Settings. Logs rotate at 8 MiB / UTC day changes with
  7-day retention and a 512 MiB directory budget. See
  [developer diagnostics](docs/windows-developer-logging-design.md).
- **Plugins**: view discovered addons and open the personal plugin directory.
- **Shortcuts**: configure input-method cycling and direct selection (entries
  1–5).

### Dictionaries and Special Environments

- **Third-party Pinyin dictionaries**: place compatible Fcitx5/libime binary
  `.dict` files in `%APPDATA%/Fcitx5/pinyin/dictionaries` for automatic loading.
  See [dictionary details](docs/pinyin.md#third-party-dictionaries).
- **Sandboxie**: configure an explicit pipe rule per the
  [Sandboxie guide](docs/windows-sandboxie-design.md). Host Core must already
  be running.
- **Modern / AppContainer apps**: CI/Nightly installers enable modern TSF
  categories; local builds require `-ModernApps` and re-registration. Host Core
  must already be running, and AppContainer menus expose only Settings. See
  [modern app support](docs/windows-modern-apps-design.md).
- **Elevated desktop apps (UAC)**: connect to the running normal-privilege host
  Core and delegate service commands to it. See
  [UAC IPC details](docs/windows-uac-ipc-design.md).

## Documentation

- [Build, deployment and validation](docs/build.md)
- [Windows installer](docs/windows-installer-design.md)
- [Deferred reboot upgrades and recovery](docs/windows-reboot-upgrade-design.md)
- [x86 TSF builds, deployment and validation](docs/windows-x86-tsf-design.md)
- [UAC elevated clients and host service control](docs/windows-uac-ipc-design.md)
- [Input-method architecture and individual builds](docs/pinyin.md)
- [Independent Windows input-method plugins and SDK builds](docs/windows-plugins.md)
- [Windows settings design](docs/windows-settings-design.md)
- [Input-method status menus and Rime schemas](docs/windows-status-menu-design.md)
- [Candidate rendering](docs/windows-candidate-rendering.md)
- [UI language and localization](docs/windows-ui-localization-design.md)
- [Sandboxie host sharing and configuration](docs/windows-sandboxie-design.md)
- [Modern app transport and TSF UIElement](docs/windows-modern-apps-design.md)
- [Developer logging and Settings diagnostics](docs/windows-developer-logging-design.md)

## Credits

- [fcitx5](https://github.com/fcitx/fcitx5): LGPL-2.1-or-later
- [weasel](https://github.com/rime/weasel): GPL-3.0-only
