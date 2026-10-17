# Fcitx5 Windows 开发指南

本文件适用于整个仓库。子目录中的 `AGENTS.md` 更具体时，以子目录规则为准。默认使用中文沟通，代码标识符、接口名和命令保留原文。

## 项目定位

- 这是使用 C++20、CMake、PowerShell 和 Windows SDK 的 Fcitx5 Windows port。
- 根工程、`win32` TSF 工程和 `win32/settings` WinUI 工程彼此独立。不要把它们的编译器、头文件、运行库或构建缓存混用。
- Core 是独立进程，TSF DLL 加载到应用进程；两者通过用户级 Named Pipe 通信，不共享 C++ 对象、分配器或引擎 ABI。Settings 也通过 IPC 访问 Core。
- `win32/ipc/protocol.h` 的协议变更必须同步更新 Core、TSF DLL 和 Settings。当前 TSF 只构建/验证 x64；CI 当前不再构建 ARM64，ARM64 输入法能力尚未验证。
- “可以编译”“可以注册为输入法”或“可以提交固定字符”都不等于已经支持完整的 Fcitx5 Windows 输入。报告结果时区分自动化 probe、免注册测试和真实系统/应用验证。

## 目录与文档

- `src/`、`CMakeLists.txt`：Core 宿主和根工程。
- `win32/dll/`、`win32/tsf/`、`win32/ipc/`：COM/注册、TSF、候选窗、语言栏和 Pipe 客户端。
- `win32/settings/`：独立 WinUI 3 Settings 工程。
- `fcitx5/`、`libime/`、`chinese-addons/`：固定版本子模块；`windows-cross/`：Windows 工具链子模块。不要未经要求升级或提交子模块版本变化。
- `scripts/`、`win32/scripts/`、`win32/tests/`：构建、部署、测试和注册/释放辅助脚本。
- 构建与部署细节见 `docs/build.md`、`docs/pinyin.md`；设置、候选窗和安装器设计分别见 `docs/windows-settings-design.md`、`docs/windows-candidate-rendering.md`、`docs/windows-installer-design.md`。UI 语言、Sandboxie、现代应用和开发日志分别见 `docs/windows-ui-localization-design.md`、`docs/windows-sandboxie-design.md`、`docs/windows-modern-apps-design.md`、`docs/windows-developer-logging-design.md`。

## 当前架构边界

- 根工程使用 MSYS2/Clang Windows GNU 工具链；TSF 工程使用 Windows SDK/ATL 环境和 Clang/LLVM 资源编译器，TIP、helper 及原生翻译库统一静态链接 MSVC CRT，部署审计普通和延迟 PE imports；Settings 使用独立 MSBuild 工程及其动态 CRT。共享翻译和日志源码在各工程分别编译，不共享运行库二进制。
- TSF 激活后可在后台启动相邻安装树中的 `bin/Fcitx5.exe`；部署树中的 Settings 位于 `settings/Fcitx5Settings.exe`。部署脚本不负责注册、注销或启动服务。
- Core 主线程拥有核心状态；Pipe 服务线程通过 `EventDispatcher` 投递请求。TSF 的编辑操作必须在正确的 edit session 中执行，并校验原始 context、代次和快照，拒绝迟到请求写入新焦点。
- IPC 使用当前用户/session 范围的 Named Pipe、ACL 和可取消 overlapped I/O。超时请求不重放，跨进程故障不提供 exactly-once 保证。
- 当前 IPC 为 v10；桌面/Sandboxie Pipe 名保留历史 `v2` 前缀，AppContainer 使用专用 `-input` 入口，服务端验证实际 token 并限制消息/资源额度。Sandboxie 和 AppContainer 不启动本地 Core/Settings；Core 与 Settings 拒绝受限或未知环境。Sandboxie 安全模式的匿名 token 兼容仅限既有用户 DACL 保护的管理入口，不扩展至受限输入入口。
- 候选 UIElement/Behavior 与 popup 定位分离；宿主接管显示或没有有效 HWND 时仍发布候选数据。候选动作绑定 context、稳定候选代次和索引，由 Core 引擎处理，TSF 不直接提交 getter 的文本。secure/COM-less、显式密码 scope、只读或禁用 context 透传不发送引擎输入，不据此声明完整密码支持。
- UI 语言使用 gettext + 各工具链独立构建的 Boost.Locale。`windows-ui.conf` v2 保存语言和默认关闭的详细日志开关，两者共享独立 revision；兼容读取 v1。Core/Settings 语言重启服务后生效，日志开关即时同步。日志仅记录诊断元数据，按键结果可含扫描码、修饰键和消费状态，不记录按键字符/Unicode、输入/候选文本、payload、配置值、窗口标题或用户路径；上游 `key_trace` 保持关闭。
- 服务互斥和手动停止状态按当前用户/session/login 维度管理；重启服务才解除停止状态。服务控制不得按进程名强制结束用户应用。
- Ctrl+Space 和语言栏左键按用户有序列表轮换可用输入法；指示器显示当前输入法的图标/标签和名称。`GUID_COMPARTMENT_KEYBOARD_OPENCLOSE` 的布尔值由实际条目是否为键盘推导；独立的系统开关在已选键盘/非键盘条目间切换。新焦点 context 与重连恢复当前条目，切换输入法取消旧预编辑；预编辑不跨输入框保存。
- 按键测试回调只观察输入，不执行 IPC 或编辑副作用；实际按键是否消费以 Core 回复为准。
- 默认关闭 X11、Wayland、DBus、server 和 keyboard engine 等上游组件；优先复用 Fcitx5/libime 抽象，不以 Linux 前端替代 Windows 实现。
- Windows `StandardPaths` 要求宿主位于安装树的 `bin`，资源位于 `share`/`share/fcitx5`。涉及平台能力前检查上游 Windows 分支，部分进程启动和 PID 查询仍可能是降级实现。

## 已知支持范围

- Core、真实 Pinyin、TSF composition、DirectWrite/Direct2D 候选窗、候选 UIElement/Behavior、语言栏输入法轮换、Settings 有序输入法列表/共享引擎选项/快捷键录制/拼音词库页/全局语言与开发日志和隔离部署链路已经接入；码表条目是否可用取决于已安装引擎与数据，probe 覆盖真实五笔提交。
- 第三方词库使用 `%APPDATA%/Fcitx5/pinyin/dictionaries` 的 `.dict`/`.disable` 文件；加载状态通过 IPC 提供给 Settings。
- AppContainer 真实 token 传输和免注册候选宿主动作已有 probe；CI/Nightly 显式启用 `UIELEMENTENABLED` / `IMMERSIVESUPPORT`，本地构建需 `-ModernApps`。安装器提供默认不选中的当前用户登录启动项，遵守手动停止状态。能力声明和机器级安装 smoke 不代表签名部署或真实现代应用输入验收。
- 尚未完成或尚未全面验证：完整 Windows `InputContext`、全部双拼方案、五笔及其他码表的真实应用覆盖、Rime、死键/AltGr/复杂布局、完整密码 context、周边文本映射、断线提交可靠性、自绘候选鼠标交互、完整无障碍、普通托盘、剪贴板、跨屏 DPI/主题、真实 UWP/现代应用系统派发、ARM64/x86 TSF，以及完整用户安装/升级/重启卸载清理。
- 注册 TSF category 不代表对应能力已经实现；新增 category、profile 图标或注册元数据后需要重新注册才能生效。

## 开始任务前

1. 运行 `git status --short`，保留用户已有修改；确认任务属于根工程、TSF 工程或两者桥接。
2. 检查目标目录的更具体 `AGENTS.md`、相关 CMake 文件和调用链。
3. 运行 `git submodule status`。缺少源码时使用 `git submodule update --init --recursive`；不要使用 `git submodule update --remote`。
4. 初始化子模块后按需运行 `scripts/apply-pinyin-patches.ps1`。补丁作用于子模块工作树，不要把它误判为需要丢弃的用户修改，也不要创建子模块提交。

## 构建与验证

- AMD64 完整链路优先使用 `scripts/build-and-deploy.ps1`，用 `-Prefix`/`-BuildRoot` 隔离构建和部署；`-WhatIf` 不应产生构建、部署或系统副作用。构建、数据缓存、Settings、安装包和 CI 选项以 `docs/build.md` 为准。
- 根工程单独构建示例：

  ```powershell
  cmake -B build/x86_64 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DARCH=AMD64
  cmake --build build/x86_64
  ```

  根工程默认不包含 libime、chinese-addons、拼音数据或上游测试；不要把它描述为完整拼音验证。
- ARM64 Core 使用 `-DARCH=ARM64` 和对应 `clangarm64` sysroot。编译器必须匹配目标工具链，先检查 PATH 和 CMake cache，不要改源码掩盖环境错误。
- TSF 工程在已初始化 VS/Windows SDK/ATL 环境中构建，并使用 `llvm-rc`：

  ```powershell
  cmake -S win32 -B win32/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
  cmake --build win32/build
  ctest --test-dir win32/build --output-on-failure
  ```

- 修改 COM/TSF 行为时补充相关回归测试；优先测试受影响目标，再扩大范围。真实系统派发、注册状态、物理 DPI 和安装器行为必须明确标注为未验证，除非当次确实验证。
- 遵循各目录 `.clang-format`；需要时使用 `win32/scripts/format.ps1` 和 `win32/scripts/lint.ps1`。报告阻塞时给出实际命令、错误和未验证范围。

## 代码与系统安全

- 保持改动聚焦，复用现有抽象；不要未经要求重写架构、升级依赖、创建提交或新分支。
- COM 方法检查输入指针、初始化输出、返回正确 HRESULT，并可靠处理引用计数和失败路径。跨线程调用遵守 Core 事件循环与 TSF 线程边界。
- 真正输入必须依据 Core 结果消费按键并提交文本，不把 demo 固定字符逻辑扩展为业务实现。
- `install.ps1`、`uninstall.ps1` 和 `release-tsf.ps1` 的真实注册、注销、提权、终止占用进程和系统输入法修改都需要用户明确要求；普通测试只使用 `-WhatIf`、模拟系统调用或隔离前缀。
- 不自动修改注册表、复制到系统目录、执行安装包、停止用户服务或按进程名结束应用。DLL 被占用时优先使用隔离构建目录；只有确认精确 DLL 路径后才可按授权处理占用。
- 修改完成后报告变更文件、行为影响、实际验证命令和剩余风险。不要在本文件追加单次修复、截图、构建目录或历史测试日志；长期设计写入对应 `docs/`，单次验证留在提交/评审记录。
