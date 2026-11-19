# Fcitx5 Windows

[English](README.md)

这是一个正在开发中的 Fcitx5 Windows 移植版本。在 AMD64 上通过 TSF 集成提供拼音、双拼和码表输入法，full 版还提供 Rime、温州话和注音。
Core 在独立进程中运行，TSF DLL 负责与应用程序集成，WinUI 设置程序管理有序输入法列表和引擎配置。统一 x64 安装包已接入 CI 打包和 Nightly 发布流程。

## 功能

- 支持拼音预编辑、候选选择和中文提交，包括全拼以及小鹤/自然码双拼。
- 支持使用数字键或空格选择候选，可配置轮换快捷键（默认 `Ctrl+Space`），以及直接切换到列表第 1～5 项的快捷键（默认 `Ctrl+Alt+1…5`）。
- 提供不抢焦点的候选浮窗，使用 DirectWrite 排版、Direct2D 彩色字体渲染和 Per-Monitor V2 定位。
- 提供 Windows 输入法指示器，显示当前输入法的图标或简短标签，提示文本显示具体名称；英文键盘显示 `A`。左键点击按同一输入法列表轮换。
- 设置支持添加、删除和排序已安装的输入法，提供拼音/双拼共享选项、八种内置双拼方案及码表引擎选项。base 版的码表条目包括五笔字型、五笔拼音、仓颉、电报码、二笔、冰蟾、晚风和自然码；条目是否可用取决于已安装的引擎和码表数据。
- full 版增加 Rime、温州话、注音及 25 张额外码表，包括五笔 98、郑码、嘸蝦米、仓颉五代和粤拼码表。
- 独立的添加窗口按语言分组展示可用输入法，有非键盘输入法的语言优先显示，并提供多语言键盘候选。
- 显示第三方拼音词库加载状态，并自动重新加载词库。
- 支持独立构建和安装引擎 DLL 及资源包，全局设置提供只读 addon 状态页。
- 支持简体中文、繁体中文和英文 UI，可在全局设置中选择跟随系统；Core/Settings 的语言在重启服务后生效，原生菜单和关于窗口通过后台刷新更新。
- 全局快捷键和引擎按键列表选项支持快捷键录制，也可手动编辑文本。
- 输入法状态菜单展示插件提供的模式、选项和子菜单，包括 Rime 方案选择。中英文标点在该菜单中切换，拼音、双拼及使用标点模块的码表共享状态。
- 可选的结构化开发日志覆盖 Core、TSF、Settings 及 Settings 退出/异常诊断，不记录输入文本。
- Sandboxie 内的客户端通过显式 Pipe 规则共享宿主 Core。
- 支持 AppContainer 输入传输和 TSF 候选 UIElement/Behavior。
- 普通桌面 TSF 可在后台自动启动 Core，输入指示器的右键菜单提供设置和服务控制。AppContainer/Sandboxie 客户端要求宿主 Core 已运行，提权桌面客户端的服务操作也交由宿主执行；安装器提供可选登录启动项。

## 安装

[Nightly 发布](https://github.com/WAAutoMaton/fcitx5-windows/releases/tag/latest)流程提供四种 x64 安装包及其 SHA256 文件。安装器要求 Windows 10 1809（build 17763）或更新版本，Windows 10 系统验收待完成。

两种版本都包含 Core、x64/x86 TSF 和 Settings；`base` 的输入法插件为 fcitx5-chinese-addons，`full` 包含全部插件（目前为 fcitx5-chinese-addons、fcitx5-rime、fcitx5-table-extra、fcitx5-wenzhounese 和 fcitx5-zhuyin）。每种版本均提供两种运行库组合：

- `Fcitx5-<run>-<base|full>-x64-setup.exe`：附带 Visual C++ 和 Windows App Runtime 安装程序。
- `Fcitx5-<run>-<base|full>-x64-no-runtime-setup.exe`：需要预先安装上述运行库，包括当前用户的 x64 Windows App SDK 1.8 Framework/DDLM >= `8000.994.2142.0`。

安装器将完整运行树安装到 Program Files，注册 TSF，配置安装发起用户的输入法并启动 Core。可选登录启动默认不勾选，并遵守手动关闭服务状态。构建包含独立的 x64/x86 TSF，共享 x64 Core；不提供原生 ARM64 TSF。详见[安装说明](docs/windows-installer-design.md)。

受管理版本升级会完整暂存新版，在重启 Windows 后完成；等待期间继续使用当前版本，暂存成功返回 `3010`。状态查询、取消和恢复方法见[重启升级说明](docs/windows-reboot-upgrade-design.md)。

安装器管理的版本请通过开始菜单的 **Uninstall Fcitx5** 或 Windows 已安装应用列表卸载。用户数据和共享运行库会保留；文件被占用时可能需要重启 Windows 才能完成删除。下文的 PowerShell 注册脚本用于开发部署。

## 构建与部署

请准备 CMake 3.27+、Git、MSYS2 clang64 及其依赖、独立的 LLVM、ImageMagick，以及带有 C++ 工具、Windows SDK、ATL、MSBuild 和 NuGet 的 Visual Studio。
依赖列表和路径请参阅[前置条件](docs/build.md#prerequisites)。设置程序还需要匹配的 x64 Windows App SDK 1.8 Framework/DDLM 和 Visual C++ 运行库。

在仓库根目录的普通 Windows PowerShell 终端中运行：

```powershell
./scripts/build-and-deploy.ps1
```

该脚本会初始化固定版本的子模块、应用 Windows 补丁，构建 AMD64 Release 版 Core/Settings、独立的 x64/x86 TSF 和安装/升级 helper，运行两套默认 TSF CTest 测试集，并部署到 `dist/pinyin`。脚本使用经过校验和验证的预编译词典/模型数据，并在后续运行中复用构建缓存。

上述命令构建 base 版。使用同一次构建的 SDK 补齐 full 版插件：

```powershell
./scripts/build-plugins.ps1 -CorePrefix ./build/all/prefix -Prefix ./dist/pinyin
```

额外插件构建需要 Git LFS、匹配的 clang64 librime/方案数据包，以及注音依赖的 GLib/Berkeley DB，详见[插件构建说明](docs/build.md#independent-input-method-plugins)。

更新已有部署前，请选择`关闭服务`并关闭加载其 DLL 的应用程序。自定义路径、单独构建和集成检查请参阅[构建选项和部署详情](docs/build.md)。

## 输入法插件

输入法可作为独立引擎或资源包安装，无需加入 Core 工程重新编译。现有 `fcitx5-chinese-addons` 继续随 Core 统一构建部署。

`fcitx5-table-extra` 复用 base 的 `table` 引擎，提供 25 张码表，不新增引擎 DLL。`fcitx5-zhuyin` 使用固定版本 libpinyin 源码构建的 libzhuyin，并携带模型数据。Rime 随包提供共享方案数据；各插件文件和许可保留在各自包目录中。

### 安装与启用

插件可安装到以下任一位置：

| 位置 | 用途 |
| --- | --- |
| `%LOCALAPPDATA%/Fcitx5/plugins/<插件包名>` | 当前用户安装，无需管理员权限，与 Core 安装目录分离 |
| `<Core 安装目录>/plugins/<插件包名>` | 随安装树一起保存的便携部署；安装目录是包含 `bin/Fcitx5.exe` 的目录 |

1. 在输入法指示器右键菜单中选择 `关闭服务`。
2. 将每个包附带的 `bin`、`lib`、`share`、`licenses` 保留在各自的 `<插件包名>` 目录。引擎 DLL 位于 `<插件包名>/lib/fcitx5`，私有依赖 DLL 位于 `<插件包名>/bin`，资源位于相应的 `lib` 或 `share` 子目录；纯资源包无需 DLL。Core 扫描直接子目录，同时兼容旧的 `plugins/bin`、`plugins/lib`、`plugins/share` 共用布局。
3. 选择 `重启服务`，打开 `输入法设置`，将新输入法加入已启用列表并调整顺序。之后使用 `Ctrl+Space` 或左键点击指示器切换。

更新或移除插件前也应关闭服务，完成后重启。独立包可直接删除其目录；旧共用布局只删除该包自身的文件。全局设置的“插件”页展示已发现 addon 的已加载、未加载或已禁用状态，并提供个人插件目录入口。插件加载与卸载需重启 Core。

### 兼容要求

各插件的构建和打包方法见其自身仓库。插件必须匹配 Core 的架构、Fcitx5/libime 版本、Windows 补丁及 Clang Windows GNU 工具链和运行库；Linux/macOS 插件或其他工具链生成的 DLL 不能直接使用。插件应共享宿主提供的 Core/libime 公共运行库，避免覆盖它们。完整文件布局和依赖要求见 [Windows 插件指南](docs/windows-plugins.md)。

## 运行

开发部署完成后，注册输入法：

```powershell
./win32/scripts/install.ps1
```

需要时脚本会请求管理员权限。请保持完整的 `dist/pinyin` 目录树，并重新打开已经加载旧 DLL 的应用程序。
使用 `Win+Space` 选择 Fcitx5；如果列表中没有，请在 Windows 的中文语言选项中添加该键盘。激活后 Core 会自动启动。

使用配置的轮换快捷键（默认 `Ctrl+Space`）或左键点击输入法指示器，按已启用列表的顺序轮换。默认列表为英文键盘和拼音；可在设置中添加双拼或其他已安装输入法并调整顺序，轮换时跳过不可用条目。选中拼音后，在文本应用中输入 `nihao`，然后使用空格或数字键选择候选；选中五笔后输入五笔编码并以相同方式选词。切换输入法或失去焦点会取消未完成的预编辑，不自动提交。

添加窗口也列出多语言键盘条目。这些条目将按键透传给应用，字符映射取决于 Windows 当前键盘布局。

在**全局设置 → 快捷键**中可修改轮换键及直接切换到第 1～5 项的组合，清空即可禁用。直接切换使用列表原始位置，目标不存在或不可用时透传按键。中英文标点通过输入法指示器的状态菜单切换，标点模块保存共享状态；拼音/双拼设置页提供引擎的其他选项。

右键点击输入法指示器，顶部展示当前引擎的状态动作和子菜单，之后提供以下固定命令：

- `输入法设置`：管理输入法列表、修改引擎选项并查看第三方拼音词库。列表修改即时保存，选项修改通过`应用`保存。
- `重启服务`：重启或启动 Core；如果 Settings 原本已打开，则重新打开它。
- `关闭服务`：停止 Core/Settings，并暂停自动启动，直到服务再次重启。
- `用户数据文件夹`：打开 `%APPDATA%/Fcitx5`，其中包括设置和拼音学习数据。
- `关于`：显示架构、CI 编号、commit 及项目/许可证链接。

选中 Rime 后，可在方案子菜单中切换 Rime 方案，或按 `F4` 打开引擎的候选式方案选单。`Ctrl+Space` 和左键点击仍轮换已启用输入法列表。Rime 部署、方案切换及繁简提交已有隔离 probe 覆盖；真实语言栏派发、普通用户环境首次部署及应用覆盖仍需验收，详见[状态菜单说明](docs/windows-status-menu-design.md)。

在**全局设置**的**常规**页选择 UI 语言并应用后，重启服务使 Core 和 Settings 使用新语言。**开发者选项**可即时启用详细日志，并打开 `%LOCALAPPDATA%/Fcitx5/logs`。日志自动轮转，维护时清理超过 7 天的文件并按 512 MiB 目录配额清理；活跃文件保留到后续维护，详见[开发诊断](docs/windows-developer-logging-design.md)。

Sandboxie 使用前请按[宿主共享配置指南](docs/windows-sandboxie-design.md)放行 Pipe。现代应用配置见[现代应用支持](docs/windows-modern-apps-design.md)。CI/Nightly 安装包启用现代 TSF category，本地构建需显式传入 `-ModernApps`；更改 category 后需重新注册。AppContainer 菜单仅提供设置入口。

放置在 `%APPDATA%/Fcitx5/pinyin/dictionaries` 中的兼容 Fcitx5/libime 二进制 `.dict` 文件会自动加载。格式和转换方法请参阅[词库详情](docs/pinyin.md#third-party-dictionaries)。

注销开发部署时，先选择`关闭服务`，然后运行：

```powershell
./win32/scripts/uninstall.ps1
```

注销不会删除文件，也不会卸载正在运行的应用程序中的 DLL。预览、备用路径和故障排查请参阅[注册和 DLL 管理](docs/build.md#registration-and-dll-management)。

## 文档

- [构建、部署与验证](docs/build.md)
- [Windows 安装说明](docs/windows-installer-design.md)
- [延迟重启升级与恢复](docs/windows-reboot-upgrade-design.md)
- [x86 TSF 构建、部署与验证](docs/windows-x86-tsf-design.md)
- [UAC 提权客户端与宿主服务控制](docs/windows-uac-ipc-design.md)
- [输入法架构与单独构建](docs/pinyin.md)
- [Windows 输入法插件设计与独立构建](docs/windows-plugins.md)
- [Windows 设置设计](docs/windows-settings-design.md)
- [输入法状态菜单与 Rime 方案](docs/windows-status-menu-design.md)
- [候选窗渲染](docs/windows-candidate-rendering.md)
- [UI 语言与国际化](docs/windows-ui-localization-design.md)
- [Sandboxie 宿主共享与配置](docs/windows-sandboxie-design.md)
- [现代应用传输与 TSF UIElement](docs/windows-modern-apps-design.md)
- [开发日志与 Settings 诊断](docs/windows-developer-logging-design.md)

## 致谢

* [fcitx5](https://github.com/fcitx/fcitx5)：LGPL-2.1-or-later
* [weasel](https://github.com/rime/weasel)：GPL-3.0-only
