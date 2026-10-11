# Fcitx5 Windows 开发指南

本文件适用于整个仓库。子目录中的 `AGENTS.md` 更具体时，以子目录规则为准。默认使用中文沟通，代码标识符、接口名和命令保留原文。

## 项目定位

- 这是使用 C++20、CMake、PowerShell 和 Windows SDK 的 Fcitx5 Windows port。
- 根工程、`win32` TSF 工程和 `win32/settings` WinUI 工程彼此独立。不要把它们的编译器、头文件、运行库或构建缓存混用。
- Core 是独立进程，TSF DLL 加载到应用进程；两者通过用户级 Named Pipe 通信，不共享 C++ 对象、分配器或引擎 ABI。Settings 也通过 IPC 访问 Core。
- `win32/ipc/protocol.h` 的协议变更必须同步更新 Core、TSF DLL 和 Settings。当前 TSF 只构建/验证 x64；ARM64 仅有 Core 构建检查。
- “可以编译”“可以注册为输入法”或“可以提交固定字符”都不等于已经支持完整的 Fcitx5 Windows 输入。报告结果时区分自动化 probe、免注册测试和真实系统/应用验证。

## 目录与文档

- `src/`、`CMakeLists.txt`：Core 宿主和根工程。
- `win32/dll/`、`win32/tsf/`、`win32/ipc/`：COM/注册、TSF、候选窗、语言栏和 Pipe 客户端。
- `win32/settings/`：独立 WinUI 3 Settings 工程。
- `fcitx5/`、`libime/`、`chinese-addons/`：固定版本子模块；`windows-cross/`：Windows 工具链子模块。不要未经要求升级或提交子模块版本变化。
- `scripts/`、`win32/scripts/`、`win32/tests/`：构建、部署、测试和注册/释放辅助脚本。
- 构建与部署细节见 `docs/build.md`、`docs/pinyin.md`；设置、候选窗和安装器设计分别见 `docs/windows-settings-design.md`、`docs/windows-candidate-rendering.md`、`docs/windows-installer-design.md`。

## 当前架构边界

- 根工程使用 MSYS2/Clang Windows GNU 工具链；TSF 工程使用 Windows SDK/ATL 环境和 Clang/LLVM 资源编译器；Settings 使用独立 MSBuild 工程。
- TSF 激活后可在后台启动相邻安装树中的 `bin/Fcitx5.exe`；部署树中的 Settings 位于 `settings/Fcitx5Settings.exe`。部署脚本不负责注册、注销或启动服务。
- Core 主线程拥有核心状态；Pipe 服务线程通过 `EventDispatcher` 投递请求。TSF 的编辑操作必须在正确的 edit session 中执行，并校验原始 context、代次和快照，拒绝迟到请求写入新焦点。
- IPC 使用当前用户/session 范围的 Named Pipe、ACL 和可取消 overlapped I/O。超时请求不重放，跨进程故障不提供 exactly-once 保证。
- 服务互斥和手动停止状态按当前用户/session/login 维度管理；重启服务才解除停止状态。服务控制不得按进程名强制结束用户应用。
- 输入模式由 `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE`、Ctrl+Space 和语言栏共享；新焦点 context 与重连继承模式。预编辑按焦点创建/销毁，不跨输入框保存未提交文本。
- 按键测试回调只观察输入，不执行 IPC 或编辑副作用；实际按键是否消费以 Core 回复为准。
- 默认关闭 X11、Wayland、DBus、server 和 keyboard engine 等上游组件；优先复用 Fcitx5/libime 抽象，不以 Linux 前端替代 Windows 实现。
- Windows `StandardPaths` 要求宿主位于安装树的 `bin`，资源位于 `share`/`share/fcitx5`。涉及平台能力前检查上游 Windows 分支，部分进程启动和 PID 查询仍可能是降级实现。

## 已知支持范围

- Core、真实 Pinyin、TSF composition、基础候选窗、语言栏输入模式、Settings 拼音/词库页和隔离部署链路已经接入。
- 第三方词库使用 `%APPDATA%/Fcitx5/pinyin/dictionaries` 的 `.dict`/`.disable` 文件；加载状态通过 IPC 提供给 Settings。
- 尚未完成或尚未全面验证：完整 Windows `InputContext`、所有双拼/五笔/Rime、死键/AltGr/复杂布局、密码 context、周边文本映射、断线提交可靠性、候选鼠标交互、TSF UIElement/无障碍、普通托盘、剪贴板、跨屏 DPI/主题、ARM64/x86 TSF，以及真实安装/升级/卸载清理。
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
