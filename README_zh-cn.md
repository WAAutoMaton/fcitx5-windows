# Fcitx5 Windows

[English](README.md)

面向 AMD64 的 Fcitx5 Windows 移植版本（持续开发中）。Core 在独立进程中运行，x64 与 x86 TSF DLL 负责与应用程序集成，WinUI 3 设置程序管理输入法列表和引擎配置。

`base` 版提供拼音、双拼和 `fcitx5-chinese-addons` 码表输入法；`full` 版额外提供 Rime、温州话、注音和 `fcitx5-table-extra`。统一 x64 安装包已接入 CI 打包与 Nightly 发布流程。

## 功能

- **中文与多语言输入**：
  - 全拼与八种内置双拼方案（含小鹤、自然码），支持预编辑、候选选择和中文提交。
  - base 码表：五笔字型、五笔拼音、仓颉、电报码、二笔、冰蟾、晚风和自然码（已安装对应引擎与码表数据时可用）。
  - full 扩展：Rime、温州话、注音及 25 张额外码表（含五笔 98、郑码、嘸蝦米、仓颉五代、粤拼码表等）。
  - 多语言键盘候选：按当前 Windows 键盘布局透传按键。
- **候选窗**：
  - 不抢焦点的 DirectWrite/Direct2D 候选浮窗，支持彩色字体渲染、软件 DC 回退与 Per-Monitor V2 DPI 定位。
  - 可在**全局设置 → 常规**中切换候选框布局：竖排（默认）或自动换行的横排。
  - 支持数字键/空格选词，以及面向现代应用宿主渲染的 TSF 候选 UIElement/Behavior。
- **输入法指示器与状态菜单**：
  - 显示当前输入法图标或简短标签（英文键盘显示 `A`），悬停提示显示输入法名称。
  - 右键菜单顶部展示插件提供的动态状态动作与子菜单（如 Rime 方案选择、中英文标点切换），底部保留设置与服务控制命令。
- **设置与快捷键**：
  - 支持添加、删除和排序已启用输入法；独立添加窗口按语言分组，有非键盘输入法的语言排在前面。
  - 可配置轮换快捷键（默认 `Ctrl+Space`）及直接切换第 1～5 项（默认 `Ctrl+Alt+1…5`）；全局快捷键和引擎按键列表选项均支持快捷键录制与手动编辑。
  - 提供第三方拼音词库加载状态页，自动后台热重载词库。
  - **全局设置 → 插件**只读展示已发现 addon 的状态，支持独立插件包发现。
  - 支持简体中文、繁体中文、英文 UI 以及跟随系统语言。
- **环境兼容与诊断**：
  - 普通桌面 TSF 激活后自动在后台启动 Core；安装器提供可选登录启动项。
  - 支持 UAC 提权桌面应用、AppContainer 受限输入传输，以及通过显式 Pipe 规则实现的 Sandboxie 宿主共享。
  - Core 默认持久化 Fcitx5 引擎日志（默认 `Info`，开启详细日志后即时切到 `Debug`），开启详细日志后另为 Core、TSF 和 Settings 记录结构化开发日志（含 Settings 退出与异常诊断；结构化日志不记录输入文本）。

## 安装

[Nightly 发布](https://github.com/WAAutoMaton/fcitx5-windows/releases/tag/latest)提供四种 x64 安装包及其 SHA256 校验文件，适用于 Windows 10 1809（build 17763）及以上版本。两种版本均包含 Core、x64/x86 TSF DLL 和 Settings：

- `base`：内置 `fcitx5-chinese-addons`。
- `full`：内置 `fcitx5-chinese-addons`、`fcitx5-rime`、`fcitx5-table-extra`、`fcitx5-wenzhounese` 和 `fcitx5-zhuyin`。

每种版本均提供两种运行库组合：

- `Fcitx5-<run>-<base|full>-x64-setup.exe`：附带 Visual C++ 和 Windows App Runtime 安装程序。
- `Fcitx5-<run>-<base|full>-x64-no-runtime-setup.exe`：需预先为当前用户安装 Visual C++ 运行库和 x64 Windows App SDK 1.8 Framework/DDLM >= `8000.994.2142.0`。

安装器将完整运行树部署到 Program Files，注册共享同一 x64 Core 的 x64/x86 TSF DLL，配置安装发起用户的输入法并启动 Core。可选登录启动默认不勾选，且遵守手动关闭服务状态。详见[安装说明](docs/windows-installer-design.md)。

升级时完整暂存新版并在等待重启期间保持当前版本可用，暂存成功返回 `3010`，重启 Windows 后完成提交。状态查询、取消与恢复见[重启升级说明](docs/windows-reboot-upgrade-design.md)。

卸载安装器管理的版本请使用开始菜单中的 **Uninstall Fcitx5** 或 Windows 已安装应用列表。用户数据与共享运行库会保留；被占用的文件将在重启 Windows 后完成删除。下文 PowerShell 注册脚本仅用于开发部署。

## 构建与部署

环境要求：CMake 3.27+、Git、MSYS2 `clang64` 依赖、独立 LLVM（配合 MSVC 14.52+ 时需 LLVM 22+）、ImageMagick，以及带 C++ 工具、Windows SDK、ATL、MSBuild 和 NuGet 的 Visual Studio。运行 Settings 还需 Visual C++ 运行库和 x64 Windows App SDK 1.8 Framework/DDLM >= `8000.994.2142.0`。完整依赖与路径见[前置条件](docs/build.md#prerequisites)。

在仓库根目录的 Windows PowerShell 中运行以下命令构建并部署 `base` 版：

```powershell
./scripts/build-and-deploy.ps1
```

脚本会初始化固定版本子模块、应用 Windows 补丁，构建 AMD64 Release 版 Core/Settings、x64/x86 TSF DLL 及安装/升级 helper，运行默认 TSF CTest 测试集，并使用经过 SHA256 校验的预编译词典/模型数据部署到 `dist/pinyin`。后续运行会自动复用构建缓存。

使用同一次构建的 SDK 补齐 `full` 版全部插件：

```powershell
./scripts/build-plugins.ps1 -CorePrefix ./build/all/prefix -Prefix ./dist/pinyin
```

额外插件构建需要 Git LFS、匹配的 `clang64` librime/方案数据包，以及注音依赖的 GLib/Berkeley DB，详见[插件构建说明](docs/build.md#independent-input-method-plugins)。

更新已有部署前，请先选择**关闭服务**并关闭加载其 TSF DLL 的应用程序。自定义路径、单独构建与集成验证见[构建选项和部署详情](docs/build.md)。

## 输入法插件

输入法可作为独立引擎或资源包构建和安装。`fcitx5-chinese-addons` 随 Core 一起构建；`fcitx5-table-extra` 复用 base 的 `table` 引擎提供 25 张码表；`fcitx5-zhuyin` 附带由固定版本 libpinyin 源码构建的 libzhuyin 与编译模型；Rime 随包提供共享方案与 OpenCC 数据。

### 安装与启用

插件可安装到以下任一位置：

| 位置 | 用途 |
| --- | --- |
| `%LOCALAPPDATA%/Fcitx5/plugins/<插件包名>` | 当前用户安装，无需管理员权限 |
| `<Core 安装目录>/plugins/<插件包名>` | 随 `bin/Fcitx5.exe` 一起保存的便携部署 |

1. 在输入法指示器右键菜单中选择**关闭服务**。
2. 将包的 `bin`、`lib`、`share`、`licenses` 目录放入 `<插件包名>`（`lib/fcitx5` 放引擎 DLL，`bin` 放私有依赖 DLL，`lib`/`share` 放资源）。Core 扫描直接子目录，同时兼容旧的 `plugins/bin`、`plugins/lib`、`plugins/share` 共用布局。
3. 选择**重启服务**，打开**输入法设置**，将新输入法加入已启用列表。

更新或移除插件前同样先关闭服务，完成后重启；删除 `<插件包名>` 即可移除独立插件包。**全局设置 → 插件**展示已发现 addon 的已加载、未加载或已禁用状态，并提供个人插件目录入口。

### 兼容要求

各插件的构建与打包步骤见各自仓库。插件必须匹配 Core 的架构、Fcitx5/libime 版本、Windows 补丁、Clang Windows GNU 工具链及运行库，并共享宿主提供的 Core/libime 公共运行库。完整布局与 SDK 说明见 [Windows 插件指南](docs/windows-plugins.md)。

## 运行

### 开发部署注册

```powershell
./win32/scripts/install.ps1
```

需要时脚本会请求管理员权限。请保持完整的 `dist/pinyin` 目录树，并重新打开加载过旧 DLL 的应用。使用 `Win+Space` 选择 Fcitx5（若缺失请在 Windows 中文语言选项中添加），激活后 Core 自动启动。

注销开发部署前先选择**关闭服务**，再执行：

```powershell
./win32/scripts/uninstall.ps1
```

重新打开应用以卸载已加载的 DLL。预览、备用路径与占用排查见[注册和 DLL 管理](docs/build.md#registration-and-dll-management)。

### 日常输入与切换

- 使用配置的轮换快捷键（默认 `Ctrl+Space`）或左键点击指示器按已启用列表顺序轮换，自动跳过不可用条目。
- 默认列表为英文键盘和拼音；可在设置中添加双拼、码表、Rime、温州话、注音或多语言键盘。
- 使用空格或数字键选词；切换输入法或失去焦点时取消未完成的预编辑。
- 在**全局设置 → 快捷键**中配置轮换键及第 1～5 项直切键（清空即禁用）。直切按列表原始位置生效，目标不存在或不可用时透传按键。
- 中英文标点通过指示器状态菜单切换，拼音、双拼及使用标点模块的码表共享同一状态。
- 选中 Rime 后，可通过状态菜单的方案子菜单切换方案，或按 `F4` 打开候选式方案选单。详见[状态菜单说明](docs/windows-status-menu-design.md)。

### 指示器菜单与设置

右键点击输入法指示器，顶部展示当前引擎的状态动作和子菜单，底部固定提供五项命令：

- **输入法设置**：管理已启用列表（即时保存）、编辑引擎选项（点击**应用**保存）并查看第三方拼音词库状态。
- **重启服务**：启动或重启 Core；若 Settings 原本已打开则自动重新打开。
- **关闭服务**：停止 Core 和 Settings，并暂停自动启动，直到再次重启服务。
- **用户数据文件夹**：打开 `%APPDATA%/Fcitx5`（含配置与拼音学习数据）。
- **关于**：显示架构、CI 编号、commit 及项目/许可证链接。

**全局设置**包含四个分页：

- **常规**：选择 UI 语言（应用后重启服务更新 Core 与 Settings；原生菜单和关于窗口在后台刷新）以及候选框布局（**竖排**或**横排**）。
- **开发者选项**：即时切换详细日志并打开 `%LOCALAPPDATA%/Fcitx5/logs`。Core 默认持久化引擎日志（默认 `Info`，开启详细日志后为 `Debug`），详细模式同时为 Core、TSF 和 Settings 写入结构化 JSON Lines 日志。日志按 8 MiB 和 UTC 日期自动轮转，保留 7 天并执行 512 MiB 目录配额。详见[开发诊断](docs/windows-developer-logging-design.md)。
- **插件**：只读查看已发现 addon，并可打开个人插件目录。
- **快捷键**：配置输入法轮换与第 1～5 项直切快捷键。

### 词库与特殊环境

- **第三方拼音词库**：将兼容的 Fcitx5/libime 二进制 `.dict` 文件放入 `%APPDATA%/Fcitx5/pinyin/dictionaries` 即可自动加载。详见[词库详情](docs/pinyin.md#third-party-dictionaries)。
- **Sandboxie**：按[宿主共享配置指南](docs/windows-sandboxie-design.md)放行 Pipe，且要求宿主 Core 已运行。
- **现代应用 / AppContainer**：CI/Nightly 安装包默认声明现代 TSF category，本地构建需显式传入 `-ModernApps` 并重新注册。要求宿主 Core 已运行，AppContainer 菜单仅保留设置入口。详见[现代应用支持](docs/windows-modern-apps-design.md)。
- **UAC 提权桌面应用**：连接已运行的普通权限宿主 Core，并将服务操作交由宿主执行。详见[UAC 提权客户端说明](docs/windows-uac-ipc-design.md)。

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

- [fcitx5](https://github.com/fcitx/fcitx5)：LGPL-2.1-or-later
- [weasel](https://github.com/rime/weasel)：GPL-3.0-only
