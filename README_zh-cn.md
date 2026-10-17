# Fcitx5 Windows

[English](README.md)

这是一个正在开发中的 Fcitx5 Windows 移植版本。在 AMD64 上通过 TSF 集成提供拼音、双拼以及多种 `chinese-addons` 码表输入法。
Core 在独立进程中运行，TSF DLL 负责与应用程序集成，WinUI 设置程序管理有序输入法列表和引擎配置。统一 x64 安装包已接入 CI 打包和 Nightly 发布流程。

## 功能与状态

- 支持真实的拼音预编辑、候选选择和中文提交。全拼以及小鹤/自然码双拼提交已通过 IPC 和 TSF probe 验证。
- 支持使用数字键或空格选择候选，并使用 `Ctrl+Space` 按设置中的顺序轮换已启用的输入法。
- 提供不抢焦点的候选浮窗，使用 DirectWrite 排版、Direct2D 彩色字体渲染和 Per-Monitor V2 定位。
- 提供 Windows 输入法指示器，显示当前输入法的图标或简短标签，提示文本显示具体名称；英文键盘显示 `A`。左键点击按同一输入法列表轮换。
- 设置支持添加、删除和排序已安装的输入法，提供拼音/双拼共享选项、八种内置双拼方案及码表引擎选项。随包提供的码表条目包括五笔字型、五笔拼音、仓颉、电报码、二笔、冰蟾、晚风和自然码；条目是否可用取决于已安装的引擎和码表数据。
- 显示第三方拼音词库加载状态，并自动重新加载词库。
- 支持简体中文、繁体中文和英文 UI，可在全局设置中选择跟随系统；Core/Settings 的语言在重启服务后生效，原生菜单和关于窗口通过后台刷新更新。
- 引擎按键列表选项支持快捷键录制，也可手动编辑文本。
- 可选的结构化开发日志覆盖 Core、TSF、Settings 及 Settings 退出/异常诊断，不记录输入文本。
- Sandboxie 内的客户端通过显式 Pipe 规则共享宿主 Core。
- 已接入 AppContainer 输入传输和 TSF 候选 UIElement/Behavior；签名安装和真实现代应用派发仍需验证。
- 普通桌面 TSF 可在后台自动启动 Core，输入指示器的右键菜单提供设置和服务控制。AppContainer/Sandboxie 客户端要求宿主 Core 已运行；安装器提供可选登录启动项。

自动化 probe 使用真实的拼音、码表引擎和 TSF context，但会模拟按键及菜单回调，不能替代 Windows 注册后的事件派发验证。输入法列表 probe 覆盖真实五笔提交和列表管理；其他码表输入法仍需更广泛的真实应用覆盖。自绘候选窗尚无鼠标选择，宿主候选动作由免注册 UIElement probe 覆盖。完整无障碍支持、真实 UWP 输入、Rime、ARM64 拼音/TSF、主题和多显示器 DPI 仍不在已验证范围内。

## 安装

[Nightly 发布](https://github.com/WAAutoMaton/fcitx5-windows/releases/tag/latest)流程提供两种 Windows 11 x64 安装包及其 SHA256 文件：

- `Fcitx5-<run>-x64-setup.exe`：附带 Visual C++ 和 Windows App Runtime 安装程序。
- `Fcitx5-<run>-x64-no-runtime-setup.exe`：需要预先安装上述运行库，包括当前用户的 x64 Windows App SDK 1.8 Framework/DDLM >= `8000.994.2142.0`。

安装器将完整运行树安装到 Program Files，注册 TSF，配置安装发起用户的输入法并启动 Core。可选登录启动默认不勾选，并遵守手动关闭服务状态。CI 提供不含运行库包的机器级安装/注册/卸载 smoke；干净机器完整用户初始化、升级、UAC 用户身份处理及重启清理仍需验证。当前只构建 x64 TSF，不覆盖 x86 和原生 ARM64 应用。详见[安装实现与验证边界](docs/windows-installer-design.md)。

安装器管理的版本请通过开始菜单的 **Uninstall Fcitx5** 或 Windows 已安装应用列表卸载。用户数据和共享运行库会保留；文件被占用时可能需要重启 Windows 才能完成删除。下文的 PowerShell 注册脚本用于开发部署。

## 构建与部署

请准备 CMake 3.27+、Git、MSYS2 clang64 及其依赖、独立的 LLVM、ImageMagick，以及带有 C++ 工具、Windows SDK、ATL、MSBuild 和 NuGet 的 Visual Studio。
依赖列表和路径请参阅[前置条件](docs/build.md#prerequisites)。设置程序还需要匹配的 x64 Windows App SDK 1.8 Framework/DDLM 和 Visual C++ 运行库。

在仓库根目录的普通 Windows PowerShell 终端中运行：

```powershell
./scripts/build-and-deploy.ps1
```

该脚本会初始化固定版本的子模块、应用 Windows 补丁、构建 AMD64 Release 版 Core、TSF、安装 helper 和 Settings，运行默认 TSF CTest 测试集，并部署到 `dist/pinyin`。脚本使用经过校验和验证的预编译词典/模型数据，并在后续运行中复用构建缓存。

脚本不会安装工具或运行库，也不会注册输入法。更新已有部署前，请选择`关闭服务`并关闭加载其 DLL 的应用程序。自定义路径、单独构建和集成检查请参阅[构建选项和部署详情](docs/build.md)。

## 运行

开发部署完成后，注册输入法：

```powershell
./win32/scripts/install.ps1
```

需要时脚本会请求管理员权限。请保持完整的 `dist/pinyin` 目录树，并重新打开已经加载旧 DLL 的应用程序。
使用 `Win+Space` 选择 Fcitx5；如果列表中没有，请在 Windows 的中文语言选项中添加该键盘。激活后 Core 会自动启动。

使用 `Ctrl+Space` 或左键点击输入法指示器，按已启用列表的顺序轮换。默认列表为英文键盘和拼音；可在设置中添加双拼或任意可用的码表输入法并调整顺序，轮换时跳过不可用条目。选中拼音后，在文本应用中输入 `nihao`，然后使用空格或数字键选择候选；选中五笔后输入五笔编码并以相同方式选词。切换输入法或失去焦点会取消未完成的预编辑，不自动提交。

右键点击输入法指示器可以使用：

- `输入法设置`：管理输入法列表、修改引擎选项并查看第三方拼音词库。列表修改即时保存，选项修改通过`应用`保存。
- `重启服务`：重启或启动 Core；如果 Settings 原本已打开，则重新打开它。
- `关闭服务`：停止 Core/Settings，并暂停自动启动，直到服务再次重启。
- `用户数据文件夹`：打开 `%APPDATA%/Fcitx5`，其中包括设置和拼音学习数据。
- `关于`：显示架构、CI 编号、commit 及项目/许可证链接。

在**全局设置**的**常规**页选择 UI 语言并应用后，重启服务使 Core 和 Settings 使用新语言。**开发者选项**可即时启用详细日志，并打开 `%LOCALAPPDATA%/Fcitx5/logs`。日志自动轮转，按 30 天期限及容量上限清理，详见[开发诊断](docs/windows-developer-logging-design.md)。

Sandboxie 使用前请按[宿主共享配置指南](docs/windows-sandboxie-design.md)放行 Pipe。现代应用的运行策略和验收边界见[现代应用支持](docs/windows-modern-apps-design.md)。CI/Nightly 安装包启用现代 TSF category，本地构建需显式传入 `-ModernApps`；更改 category 后需重新注册。AppContainer 菜单仅提供设置入口。

放置在 `%APPDATA%/Fcitx5/pinyin/dictionaries` 中的兼容 Fcitx5/libime 二进制 `.dict` 文件会自动加载。格式和转换方法请参阅[词库详情](docs/pinyin.md#third-party-dictionaries)。

注销开发部署时，先选择`关闭服务`，然后运行：

```powershell
./win32/scripts/uninstall.ps1
```

注销不会删除文件，也不会卸载正在运行的应用程序中的 DLL。预览、备用路径和故障排查请参阅[注册和 DLL 管理](docs/build.md#registration-and-dll-management)。

## 文档

- [构建、部署与验证](docs/build.md)
- [Windows 安装实现与验证边界](docs/windows-installer-design.md)
- [输入法架构、单独构建与验证限制](docs/pinyin.md)
- [Windows 设置设计](docs/windows-settings-design.md)
- [候选窗渲染与 DPI 验证](docs/windows-candidate-rendering.md)
- [UI 语言与国际化](docs/windows-ui-localization-design.md)
- [Sandboxie 宿主共享与配置](docs/windows-sandboxie-design.md)
- [现代应用传输与 TSF UIElement](docs/windows-modern-apps-design.md)
- [开发日志与 Settings 诊断](docs/windows-developer-logging-design.md)

## 致谢

* [fcitx5](https://github.com/fcitx/fcitx5)：LGPL-2.1-or-later
* [weasel](https://github.com/rime/weasel)：GPL-3.0-only
