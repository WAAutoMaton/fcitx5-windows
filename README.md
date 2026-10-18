# Fcitx5 Windows

[中文文档](README_zh-cn.md)

An in-progress Windows port of Fcitx5. On AMD64 it provides Pinyin, Shuangpin
and multiple `chinese-addons` table input methods through a TSF integration.
Core runs in a separate process; a TSF DLL integrates with applications, and a
WinUI settings app manages the ordered input-method list and engine
configuration. Unified x64 installers are available through the CI packaging
and Nightly release workflow.

## Features and Status

- Real Pinyin preedit, candidate selection and Chinese commits. Full Pinyin and
  Xiaohe/Ziranma Shuangpin commits are verified by IPC and TSF probes.
- Keyboard candidate selection with number keys or Space, and `Ctrl+Space`
  cycling through the enabled input methods in the order set in Settings.
- A nonactivating candidate popup with DirectWrite layout, Direct2D color-font
  rendering and Per-Monitor V2 positioning.
- A Windows input-method indicator showing the current method's icon or compact
  label, with its name in the tooltip; English keyboard uses `A`. Left-clicking
  cycles through the same input-method list.
- Settings to add, remove and reorder installed input methods, with shared
  Pinyin/Shuangpin options, eight built-in Shuangpin profiles and table-engine
  options. The bundled table entries are Wubi, Wubi Pinyin, Cangjie, Dianbaoma,
  Erbi, Bingchan, Wanfeng and Ziranma; an entry is available when its engine and
  table data are installed.
- Third-party Pinyin dictionary loading status and automatic dictionary reload.
- Simplified Chinese, Traditional Chinese and English UI, with a system-language
  option in Global Settings. Engine/UI language changes take effect after a
  service restart; native menus and About refresh in the background.
- Shortcut recording for engine key-list options, alongside manual text editing.
- Optional structured developer logs for Core, TSF and Settings, including
  Settings exit/exception diagnostics, without recording input text.
- Sandboxie clients share the host Core through an explicit pipe rule.
- AppContainer input transport and TSF candidate UIElement/Behavior are
  implemented; signed installation and real modern-app dispatch still need validation.
- Automatic background Core startup, with settings and service controls in the
  input indicator's right-click menu. AppContainer and Sandboxie clients require
  an already-running host Core; the installer offers optional login startup.

Automated probes use real Pinyin, table-engine and TSF contexts, but simulate
key/menu callbacks rather than Windows' registered event dispatch. The input
method probes cover real Wubi commits and list management; other table methods
still need broader application coverage. The popup has no mouse selection;
host candidate actions are covered by unregistered UIElement probes. Full
accessibility, real UWP input, Rime, ARM64 Pinyin/TSF, themes and multiple-monitor DPI remain
outside the validated scope.

## Install

The [Nightly release](https://github.com/WAAutoMaton/fcitx5-windows/releases/tag/latest)
workflow publishes two Windows 11 x64 installers and their SHA256 files:

- `Fcitx5-<run>-x64-setup.exe`: bundles Visual C++ and Windows App Runtime installers.
- `Fcitx5-<run>-x64-no-runtime-setup.exe`: requires those runtimes to be installed
  beforehand, including x64 Windows App SDK 1.8 Framework/DDLM >= `8000.994.2142.0`
  for the current user.

The installer targets Program Files, registers TSF, configures the initiating
user's input method and starts Core. Optional login startup is unchecked by
default and respects a manual service stop. CI has a no-runtime, machine-only
install/registration/uninstall smoke test; complete clean-machine user setup,
upgrades, UAC user handling and restart cleanup still need validation.
Only x64 TSF is built; x86 and native ARM64
applications are not covered. See [installer status and limits](docs/windows-installer-design.md).

For an installer-managed copy, use **Uninstall Fcitx5** in the Start menu or
Windows' installed-apps list. User data and shared runtimes are retained;
locked files may require a Windows restart to finish removal. The PowerShell
registration helpers below are for development deployments.

## Build and Deploy

Prepare CMake 3.27+, Git, MSYS2 clang64 and its dependencies, standalone LLVM,
ImageMagick, and Visual Studio with C++ tools, Windows SDK, ATL, MSBuild and NuGet.
See [prerequisites](docs/build.md#prerequisites) for the dependency list and paths.
The settings app also requires the matching x64 Windows App SDK 1.8
Framework/DDLM and Visual C++ runtime.

From the repository root in a normal Windows PowerShell terminal, run:

```powershell
./scripts/build-and-deploy.ps1
```

This initializes pinned submodules, applies Windows patches, builds AMD64 Release
Core, TSF, the setup helper and Settings, runs the default TSF CTest suite,
and deploys to `dist/pinyin`. It uses checksum-verified precompiled dictionary/model data and
reuses the build cache on subsequent runs.

The script does not install tools/runtimes or register the input method. Before
updating an existing deployment, choose `Stop Services` and close applications loading
its DLL. See [build options and deployment details](docs/build.md) for custom
paths, individual builds and integration checks.

## Run

For a development deployment, register the input method:

```powershell
./win32/scripts/install.ps1
```

The script requests administrator privileges when needed. Keep the complete
`dist/pinyin` tree together, and reopen applications that loaded an older DLL.
Select Fcitx5 with `Win+Space`; if absent, add its keyboard in Windows' Chinese
language options. Activation starts Core automatically.

Use `Ctrl+Space` or left-click the input-method indicator to cycle through the
enabled list. The default list is English keyboard and Pinyin; Settings lets
you add Shuangpin or any available table method and change their order.
Unavailable entries are skipped. With Pinyin selected, type `nihao` in a text
application and select a candidate with Space or a number key. With Wubi
selected, enter a Wubi code and select its candidate in the same way. Changing
input methods or losing focus cancels unfinished preedit without committing it.

Right-click the input-method indicator for:

- `Input Method Settings`: manage the input-method list, edit engine options and view
  third-party Pinyin dictionaries. List changes save immediately; option edits
  save with `Apply`.
- `Restart Services`: restart/start Core and reopen Settings if it was already open.
- `Stop Services`: stop Core/Settings and pause automatic startup until a restart.
- `User Data Folder`: open `%APPDATA%/Fcitx5`, including settings and Pinyin learning data.
- `About`: show architecture, CI build number, commit and project/license links.

In **Global Settings**, the **General** tab selects the UI language; restart
services after applying it to update Core and Settings. **Developer options**
enables detailed logging immediately and opens `%LOCALAPPDATA%/Fcitx5/logs`.
Logs rotate automatically; maintenance removes expired files after 7 days and
enforces capacity limits. See [developer diagnostics](docs/windows-developer-logging-design.md).

For Sandboxie, follow the [host-sharing setup guide](docs/windows-sandboxie-design.md).
Modern-app runtime behavior and acceptance limits are documented in
[modern app support](docs/windows-modern-apps-design.md). CI/Nightly installers
declare modern TSF categories; local builds require `-ModernApps`. Re-register
after changing category declarations. AppContainer menus expose only Settings.

Compatible Fcitx5/libime binary `.dict` files placed in
`%APPDATA%/Fcitx5/pinyin/dictionaries` are loaded automatically. See
[dictionary details](docs/pinyin.md#third-party-dictionaries) for formats and conversion.

To unregister a development deployment, choose `Stop Services`, then run:

```powershell
./win32/scripts/uninstall.ps1
```

Unregistration does not delete files or unload the DLL from running applications.
See [registration and DLL management](docs/build.md#registration-and-dll-management)
for previews, alternate paths and troubleshooting.

## Documentation

- [Build, deployment and validation](docs/build.md)
- [Windows installer implementation and validation limits](docs/windows-installer-design.md)
- [Input-method architecture, individual builds and validation limits](docs/pinyin.md)
- [Windows settings design](docs/windows-settings-design.md)
- [Candidate rendering and DPI validation](docs/windows-candidate-rendering.md)
- [UI language and localization](docs/windows-ui-localization-design.md)
- [Sandboxie host sharing and configuration](docs/windows-sandboxie-design.md)
- [Modern app transport and TSF UIElement](docs/windows-modern-apps-design.md)
- [Developer logging and Settings diagnostics](docs/windows-developer-logging-design.md)

## Credits

* [fcitx5](https://github.com/fcitx/fcitx5): LGPL-2.1-or-later
* [weasel](https://github.com/rime/weasel): GPL-3.0-only
