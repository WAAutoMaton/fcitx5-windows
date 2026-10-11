# Fcitx5 Windows

[English](README.md)

这是一个正在开发中的 Fcitx5 Windows 移植版本，目前在 AMD64 上提供基本的拼音输入路径。
Core 在独立进程中运行，TSF DLL 负责与应用程序集成，WinUI 设置程序提供拼音配置。当前还没有统一安装程序。

## 功能与状态

- 支持真实的拼音预编辑、候选选择和中文提交。全拼以及小鹤/自然码双拼提交已通过 IPC 和 TSF probe 验证。
- 支持使用数字键或空格选择候选，并使用 `Ctrl+Space` 在中文输入和直接输入之间切换。
- 提供不抢焦点的候选浮窗，使用 DirectWrite 排版、Direct2D 彩色字体渲染和 Per-Monitor V2 定位。
- 提供 Windows 输入模式指示器，显示 `中` 或 `A`；点击即可切换模式。
- 提供全拼/双拼设置和八种内置双拼方案，并显示第三方词库加载状态、自动重新加载词库。
- Core 在后台自动启动，输入指示器的右键菜单提供设置和服务控制。

已确认注册后的输入指示器可以正常工作。自动化 probe 使用真实的拼音和 TSF context，但会模拟按键及菜单回调。更广泛的应用兼容性、操作系统事件派发、主题和多显示器 DPI 仍需验证。尚未实现候选鼠标选择和无障碍支持；ARM64 拼音/TSF、五笔和 Rime 尚未验证。

## 构建与部署

请准备 CMake 3.27+、Git、MSYS2 clang64 及其依赖、独立的 LLVM、ImageMagick，以及带有 C++ 工具、Windows SDK、ATL、MSBuild 和 NuGet 的 Visual Studio。
依赖列表和路径请参阅[前置条件](docs/build.md#prerequisites)。设置程序还需要匹配的 x64 Windows App SDK 1.8 Framework/DDLM 和 Visual C++ 运行库。

在仓库根目录的普通 Windows PowerShell 终端中运行：

```powershell
./scripts/build-and-deploy.ps1
```

该脚本会初始化固定版本的子模块、应用 Windows 补丁、构建 AMD64 Release 版 Core、TSF 和 Settings，运行九项默认 TSF 测试，并部署到 `dist/pinyin`。脚本使用经过校验和验证的预编译词典/模型数据，并在后续运行中复用构建缓存。

脚本不会安装工具或运行库，也不会注册输入法。更新已有部署前，请选择`关闭服务`并关闭加载其 DLL 的应用程序。自定义路径、单独构建和集成检查请参阅[构建选项和部署详情](docs/build.md)。

## 运行

注册已部署的输入法：

```powershell
./win32/scripts/install.ps1
```

需要时脚本会请求管理员权限。请保持完整的 `dist/pinyin` 目录树，并重新打开已经加载旧 DLL 的应用程序。
使用 `Win+Space` 选择 Fcitx5；如果列表中没有，请在 Windows 的中文语言选项中添加该键盘。激活后 Core 会自动启动。

在文本应用中输入 `nihao`，然后使用空格或数字键选择候选。使用 `Ctrl+Space` 或点击 `中`/`A` 切换输入模式。失去焦点会取消未完成的预编辑。

右键点击输入模式指示器可以使用：

- `输入法设置`：选择全拼/双拼并查看第三方词库。
- `重启服务`：重启或启动 Core；如果 Settings 原本已打开，则重新打开它。
- `关闭服务`：停止 Core/Settings，并暂停自动启动，直到服务再次重启。
- `用户数据文件夹`：打开 `%APPDATA%/Fcitx5`，其中包括设置和拼音学习数据。

放置在 `%APPDATA%/Fcitx5/pinyin/dictionaries` 中的兼容 Fcitx5/libime 二进制 `.dict` 文件会自动加载。格式和转换方法请参阅[词库详情](docs/pinyin.md#third-party-dictionaries)。

移除输入法时，先选择`关闭服务`，然后运行：

```powershell
./win32/scripts/uninstall.ps1
```

注销不会删除文件，也不会卸载正在运行的应用程序中的 DLL。预览、备用路径和故障排查请参阅[注册和 DLL 管理](docs/build.md#registration-and-dll-management)。

## 文档

- [构建、部署与验证](docs/build.md)
- [拼音架构、单独构建与验证限制](docs/pinyin.md)
- [Windows 设置设计](docs/windows-settings-design.md)
- [候选窗渲染与 DPI 验证](docs/windows-candidate-rendering.md)

## 致谢

* [fcitx5](https://github.com/fcitx/fcitx5)：LGPL-2.1-or-later
* [weasel](https://github.com/rime/weasel)：GPL-3.0-only
