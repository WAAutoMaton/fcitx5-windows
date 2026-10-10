# Fcitx5 Windows

An in-progress Windows port of Fcitx5, with a basic Pinyin input path on AMD64.
Core runs in a separate process; a TSF DLL integrates with applications, and a
WinUI settings app provides Pinyin configuration. There is no unified installer yet.

## Features and Status

- Real Pinyin preedit, candidate selection and Chinese commits. Full Pinyin and
  Xiaohe/Ziranma Shuangpin commits are verified by IPC and TSF probes.
- Keyboard candidate selection with number keys or Space, and `Ctrl+Space`
  switching between Chinese and direct input.
- A Windows input-mode indicator showing `中` or `A`; clicking it switches modes.
- Settings for full/double Pinyin and eight built-in Shuangpin profiles, plus
  third-party dictionary loading status and automatic dictionary reload.
- Automatic background Core startup, with settings and service controls in the
  input indicator's right-click menu.

The registered input indicator has been confirmed working. Automated probes use
real Pinyin and TSF contexts but simulate key/menu callbacks. Broader application
compatibility, OS event dispatch, themes and multiple-monitor DPI still need
validation. Candidate mouse selection and accessibility are not implemented;
ARM64 Pinyin/TSF, Wubi and Rime are not validated.

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
Core, TSF and Settings, runs the eight default TSF tests, and deploys to
`dist/pinyin`. It uses checksum-verified precompiled dictionary/model data and
reuses the build cache on subsequent runs.

The script does not install tools/runtimes or register the input method. Before
updating an existing deployment, choose `关闭服务` and close applications loading
its DLL. See [build options and deployment details](docs/build.md) for custom
paths, individual builds and integration checks.

## Run

Register the deployed input method:

```powershell
./win32/scripts/install.ps1
```

The script requests administrator privileges when needed. Keep the complete
`dist/pinyin` tree together, and reopen applications that loaded an older DLL.
Select Fcitx5 with `Win+Space`; if absent, add its keyboard in Windows' Chinese
language options. Activation starts Core automatically.

Type `nihao` in a text application and select a candidate with Space or a number
key. Use `Ctrl+Space` or click `中`/`A` to switch input modes. Focus loss cancels
unfinished preedit.

Right-click the input-mode indicator for:

- `输入法设置`: choose full/double Pinyin and view third-party dictionaries.
- `重启服务`: restart/start Core and reopen Settings if it was already open.
- `关闭服务`: stop Core/Settings and pause automatic startup until a restart.
- `用户数据文件夹`: open `%APPDATA%/Fcitx5`, including settings and Pinyin learning data.

Compatible Fcitx5/libime binary `.dict` files placed in
`%APPDATA%/Fcitx5/pinyin/dictionaries` are loaded automatically. See
[dictionary details](docs/pinyin.md#third-party-dictionaries) for formats and conversion.

To remove the input method, choose `关闭服务`, then run:

```powershell
./win32/scripts/uninstall.ps1
```

Unregistration does not delete files or unload the DLL from running applications.
See [registration and DLL management](docs/build.md#registration-and-dll-management)
for previews, alternate paths and troubleshooting.

## Documentation

- [Build, deployment and validation](docs/build.md)
- [Pinyin architecture, individual builds and validation limits](docs/pinyin.md)
- [Windows settings design](docs/windows-settings-design.md)

## Credits

* [fcitx5](https://github.com/fcitx/fcitx5): LGPL-2.1-or-later
* [weasel](https://github.com/rime/weasel): GPL-3.0-only
