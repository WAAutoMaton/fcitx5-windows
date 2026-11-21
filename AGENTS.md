# Fcitx5 Windows 开发指南

本文件适用于整个仓库。子目录中的 `AGENTS.md` 更具体时，以子目录规则为准。默认使用中文沟通，代码标识符、接口名和命令保留原文。

## 项目定位

- 这是使用 C++20、CMake、PowerShell 和 Windows SDK 的 Fcitx5 Windows 移植版本，最低支持 Windows 10 1809 x64（build 17763）。
- 根工程、`win32` TSF 工程和 `win32/settings` WinUI 工程彼此独立，不得混用它们的编译器、头文件、运行库或构建缓存。
- Core 是独立进程，TSF DLL 加载到应用进程；两者通过用户级 Named Pipe 通信，不跨进程共享 C++ 对象、分配器或引擎 ABI。Settings 也通过 IPC 访问 Core。
- `win32/ipc/protocol.h` 的协议变更必须同步更新 Core、TSF DLL 和 Settings。x64/x86 TSF 使用独立构建树，共享同一 x64 Core。CI 构建 x64/x86，ARM64 TSF 尚未适配。
- 报告结果时明确区分自动化 probe、免注册测试和真实系统/应用验证。

## 目录与文档

- `src/`、`CMakeLists.txt`：Core 宿主和根工程。
- `win32/dll/`、`win32/tsf/`、`win32/ipc/`：COM/注册、TSF、候选窗、语言栏和 Pipe 客户端。
- `win32/settings/`：独立 WinUI 3 Settings 工程。
- `win32/setup/`、`installer/`：安装/卸载 helper、延迟升级维护程序与 Inno Setup 7 打包脚本。
- `fcitx5/`、`libime/`、`plugins/` 下各插件：固定版本子模块；`windows-cross/`：Windows 工具链子模块。未经要求不得升级或提交子模块版本变化。
- `scripts/`、`win32/scripts/`、`win32/tests/`：构建、部署、测试与注册辅助脚本。
- 文档索引：
  - 构建、架构与插件：`docs/build.md`、`docs/pinyin.md`、`docs/windows-plugins.md`
  - UI 与渲染：`docs/windows-settings-design.md`、`docs/windows-candidate-rendering.md`、`docs/windows-status-menu-design.md`、`docs/windows-ui-localization-design.md`
  - 隔离、权限与日志：`docs/windows-sandboxie-design.md`、`docs/windows-modern-apps-design.md`、`docs/windows-uac-ipc-design.md`、`docs/windows-developer-logging-design.md`
  - 安装、双架构与升级：`docs/windows-installer-design.md`、`docs/windows-x86-tsf-design.md`、`docs/windows-reboot-upgrade-design.md`

## 架构边界

### 工具链与进程部署

- **根工程（Core）**：MSYS2 `clang64`（Clang Windows GNU 工具链），默认关闭 X11、Wayland、DBus、server 和上游 keyboard engine。Windows `StandardPaths` 要求宿主位于安装树的 `bin`，资源位于 `share`/`share/fcitx5`。
- **TSF 工程（`win32`）**：独立 LLVM（配合 MSVC 14.52+ 时需 LLVM 22+，`clang`/`clang++`/`llvm-rc`）+ Windows SDK/ATL；TIP、helper 与原生翻译库统一静态链接 MSVC CRT，部署前审计普通和延迟 PE imports。
- **Settings 工程（`win32/settings`）**：独立 MSBuild 工程与动态 CRT（framework-dependent Windows App SDK 1.8）。共享翻译、日志和协议源码在各工程分别编译，不共享运行库二进制。
- TSF 激活后在后台启动相邻安装树中的 `bin/Fcitx5.exe`；部署树中的 Settings 位于 `settings/Fcitx5Settings.exe`。部署脚本不执行注册、注销或服务启动。

### 线程、IPC 与安全隔离

- Core 主线程拥有核心状态，Pipe 服务线程通过 `EventDispatcher` 投递请求。TSF 文档编辑必须在正确的 edit session 中执行，校验原始 context、代次和快照，拒绝迟到请求写入新焦点。
- 按键测试回调只观察状态，不执行 IPC 或编辑；按键是否消费以 Core 回复为准。IPC 使用用户/session 范围 Named Pipe、ACL 与可取消 overlapped I/O，超时请求不重放，跨进程故障不提供 exactly-once 保证。
- 当前 IPC 为 **v16**，覆盖输入法目录（含 `language` 元数据）、有序列表、引擎配置、全局语言/日志/快捷键/候选框布局、只读插件查询、候选动作及 context 状态菜单查询/动作。
- 桌面与 Sandboxie Pipe 名保留历史 `v2` 前缀；AppContainer 使用专用 `-input` 入口，服务端校验实际 token 并限制消息白名单与资源额度，拒绝状态菜单查询/动作和配置写入。
- Sandboxie 和 AppContainer 均不启动本地 Core/Settings；Core 与 Settings 拒绝在受限或未知环境启动。Sandboxie 安全模式的匿名 token 兼容仅限已有用户 DACL 保护的管理入口。
- 桌面管理入口支持同用户/session 的 UAC linked-token 登录匹配，`sameLogin()` 保持严格。提权或 elevation 查询失败的桌面 TSF 将服务操作路由给已运行的宿主 Core，连接失败不回退到本地提权启动。
- 服务互斥和手动停止状态按当前用户/session/logon 维度管理，仅重启服务可解除停止状态。服务控制不得按进程名强制结束用户应用。

### 输入法、UI、配置与日志

- **候选窗与 UIElement**：候选 UIElement/Behavior 与自绘 popup 定位分离，支持竖排（默认）和横排（自动换行）布局，宿主接管或缺少 HWND 时仍发布候选数据。候选动作绑定原 context、稳定候选代次和索引并由 Core 引擎处理。secure/COM-less、显式密码 scope、只读或禁用 context 透传按键。
- **状态菜单**：通用桥接读取 Fcitx `StatusArea` / `Action/Menu`，桌面 `HMENU` 和系统托管 `ITfMenu` 每次打开使用冻结视图。动作绑定原 context、连接、输入法和菜单代次，在 Core 主线程调用插件并通过 TSF edit session 应用回复。Core 通用层不解析插件私有方案配置；Rime 的 F4 候选式选单与语言栏方案子菜单走各自原生链路。
- **全局配置与国际化**：UI 使用 gettext + 各工具链独立构建的 Boost.Locale 1.92。`conf/windows-ui.conf` **v4** 保存 UI 语言、默认关闭的详细日志开关、全局快捷键和候选框布局，共享独立 revision（v1/v2 补齐默认快捷键，v1 默认关闭日志，v1–v3 默认竖排）。Core/Settings 语言重启服务后生效；日志开关、Core 快捷键即时生效；候选框布局在 TSF 下一次收到 `KeyReply` 时生效。
- **日志边界**：Core 默认持久化纯文本 Fcitx5 引擎日志（`fcitx5-YYYYMMDD.log`，默认 `Info`，开启详细日志后即时切到 `Debug`，保留上游 addon 消息原文，上游 `key_trace` 保持关闭）。详细日志为结构化 JSON Lines，仅记录诊断元数据；TSF 按键结果可含扫描码、修饰键和消费状态，结构化事件不记录按键字符/Unicode、输入/候选文本、payload、配置值、窗口标题或用户路径。
- **快捷键与指示器**：全局快捷键支持轮换（默认 `Ctrl+Space`）和直切第 1～5 项（默认 `Ctrl+Alt+1…5`），按列表原始位置生效，目标不存在或不可用时透传；语言栏左键轮换。`GUID_COMPARTMENT_KEYBOARD_OPENCLOSE` 由当前条目是否为键盘推导；新焦点与重连恢复当前条目，切换输入法取消旧预编辑。
- **输入法添加与标点**：Settings 独立添加窗口按 `language` 分组并本地化组名，含非键盘输入法的语言排在前面。`windowskeyboard` 提供多语言键盘透传条目。中英文标点通过状态菜单切换，由上游 punctuation 模块管理并保存到 `conf/punctuation.conf`（拼音/双拼及使用该模块的码表共享）；TSF 将空闲 OEM 标点与 Shift+数字送交 Core 决定转换/消费，旧 `Options/pinyin/WindowsPunctuation` 读取时丢弃、下次保存时清除。
- **插件与发行版**：Core 扫描安装树与 `%LOCALAPPDATA%/Fcitx5/plugins` 直接子目录（保留 `bin/lib/share/licenses`，兼容旧共用布局）。Settings 全局插件页只读展示 addon 元数据与状态，不触发加载；插件变更需重启 Core。`base` 含 `fcitx5-chinese-addons`；`full` 另含 `fcitx5-rime`、`fcitx5-table-extra`（复用 `table` 引擎的 25 张码表）、`fcitx5-wenzhounese` 和 `fcitx5-zhuyin`（固定 libpinyin 源码构建的 libzhuyin 与编译模型）。

## 当前实现与待验收范围

### 已接入与自动化覆盖

- Core、真实 Pinyin/Shuangpin/码表、TSF composition、DirectWrite/Direct2D 竖排/横排候选窗（含软件 DC 回退、子编辑框溢出回退及点锚点）、候选 UIElement/Behavior、语言栏轮换、通用状态菜单及标点切换、Settings（有序列表、语言分组添加、共享引擎选项、快捷键录制、词库页、全局语言/布局/日志/插件页）均已接入。
- 第三方拼音词库监听 `%APPDATA%/Fcitx5/pinyin/dictionaries` 的 `.dict`/`.disable` 文件，通过 IPC 向 Settings 提供加载状态。
- 隔离 probe 与 CI 覆盖：Wenzhounese/Wubi98/Cangjie5/Jyutping Table/标准注音提交及注音模型保存重载、Rime/Pinyin 状态菜单动作与双架构并发、AppContainer 真实 token 传输与免注册候选宿主动作、x86 CTest 与跨架构 Pipe 并发、base/full 不含运行库安装包机器级双架构注册/卸载 smoke，以及安装向导/重启选择脚本回归。
- 延迟升级默认启用：完整暂存新版并保持旧版可用，返回 `3010`，重启前通过 `suspend-upgrade` 或临时 SYSTEM 预关机服务（`Fcitx5-UpgradeShutdown`）暂停双视图 COM 入口，重启后由受保护维护程序与临时 SYSTEM 开机任务提交。自动化覆盖文件/映像保护、状态机、COM 暂停/恢复、关机服务逻辑、注册恢复与新增文件卸载。
- CI/Nightly 默认启用 `UIELEMENTENABLED` / `IMMERSIVESUPPORT`（本地需 `-ModernApps`），打包提供 `base`/`full` × 含/不含运行库四种安装包。新增 category、profile 图标或注册元数据后需重新注册生效。

### 尚未完成或待真实系统验收

- 完整 Windows `InputContext`、死键/AltGr/复杂布局、完整密码 context、周边文本映射、断线提交可靠性、自绘候选鼠标交互、完整无障碍、普通托盘、剪贴板、ARM64 TSF、代码签名部署。
- 真实系统与宿主矩阵验收：全部双拼方案、码表/温州话/注音/Rime 在普通用户环境与真实应用中的输入与语言栏派发、跨屏混合 DPI/主题、真实 UWP/现代应用派发、x86/UAC 真实应用输入，以及干净 VM 中的完整安装、含运行库部署、开机时序延迟升级与重启清理。

## 开始任务前

1. 运行 `git status --short`，保留用户已有修改；确认任务属于根工程、TSF 工程或两者桥接。
2. 检查目标目录的更具体 `AGENTS.md`、相关 CMake 文件和调用链。
3. 运行 `git submodule status`。缺少源码时使用 `git submodule update --init --recursive`；不要使用 `git submodule update --remote`。
4. 初始化子模块后按需运行 `scripts/apply-pinyin-patches.ps1`。补丁作用于子模块工作树，不要将其误判为需丢弃的修改，也不要创建子模块提交。

## 构建与验证

- AMD64 完整链路优先使用 `scripts/build-and-deploy.ps1`，用 `-Prefix`/`-BuildRoot` 隔离构建和部署；`-WhatIf` 无副作用。完整选项见 `docs/build.md`。
- 统一脚本构建 `base`；`full` 需再运行 `scripts/build-plugins.ps1 -CorePrefix <同次构建的 SDK 前缀> -Prefix <部署前缀>`（执行 `git lfs pull`，需 Git LFS 与匹配依赖，SDK 必须使用原 staging 路径）。
- 状态菜单集成验证使用 `scripts/test-status-menu.ps1`（需独立 `FCITX5_UI_TEST` 构建，设置独立 `FCITX5_TEST_USER_ROOT`，`-Rime` 验证 Rime 方案动作，`-PeerTsfBuild` 增加跨架构并发）；根工程可启用 `-DENABLE_WINDOWS_STATUS_MENU_TESTS=ON` 运行 `test_windowsstatusmenu`。
- 根工程单独构建（仅构建 Core 宿主，不含 libime/插件/数据）：

  ```powershell
  cmake -B build/x86_64 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DARCH=AMD64
  cmake --build build/x86_64
  ```

- ARM64 Core 使用 `-DARCH=ARM64` 与对应 `clangarm64` sysroot。编译器必须匹配目标工具链，先检查 PATH 和 CMake cache，不要改源码掩盖环境错误。
- TSF 工程在已初始化 VS/Windows SDK/ATL 环境中使用 `llvm-rc` 构建：

  ```powershell
  cmake -S win32 -B win32/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
  cmake --build win32/build
  ctest --test-dir win32/build --output-on-failure
  ```

- 修改 COM/TSF 行为时补充回归测试；先跑受影响目标，再扩大范围。真实系统派发、注册状态、物理 DPI 和安装器行为除非当次实测，否则明确标注待验证。
- 遵循各目录 `.clang-format`，需要时运行 `win32/scripts/format.ps1` 和 `win32/scripts/lint.ps1`。报告阻塞时提供实际命令、错误和未验证范围。

## 代码与系统安全

- 保持改动聚焦，复用现有抽象；未经要求不重写架构、升级依赖、创建提交或新分支。
- COM 方法检查输入指针、初始化输出、返回正确 HRESULT，妥善处理引用计数和失败路径；跨线程调用遵守 Core 事件循环与 TSF 线程边界。
- 真实输入依据 Core 结果消费按键并提交文本。
- `install.ps1`、`uninstall.ps1` 和 `release-tsf.ps1` 的真实注册、注销、提权、终止占用进程和系统输入法修改需用户明确要求；普通测试只使用 `-WhatIf`、模拟系统调用或隔离前缀。
- 不自动修改注册表、复制到系统目录、执行安装包、停止用户服务或按进程名结束应用。DLL 被占用时优先使用隔离构建目录；确认精确 DLL 路径后才可按授权处理占用。
- 修改完成后报告变更文件、行为影响、实际验证命令和剩余风险。不要在本文件追加单次修复、截图、构建目录或历史测试日志；长期设计写入对应 `docs/`。
- 文档编写原则：直接描述当前行为、前提和具体限制，避免无有效信息的防御性措辞（如重复的“不包含 xxx”“不代表 xxx”“不宣称 xxx”）；有实际意义的权限、隐私和验证边界应集中、清晰地说明。
