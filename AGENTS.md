# Fcitx5 Windows 开发指南

## 适用范围与项目定位

- 本文件适用于整个仓库；子目录中的 `AGENTS.md` 对其目录有更具体的约束时，优先遵守子目录说明。
- 本项目是尚未完成的 Fcitx5 Windows port，使用 C++20、CMake 和 PowerShell。
- 默认使用中文沟通，保留代码标识符、接口名称和命令的原文。
- 以下进度更新于 2026-10-10。功能变化时同步更新相关说明，以实际代码为准，不把已知缺口当作永久设计限制。
- 不要将“可以编译”“可以注册为输入法”“可以提交固定字符”描述为“已经支持 Fcitx5 输入”。

## 项目结构

| 路径 | 职责 |
| --- | --- |
| `CMakeLists.txt` | 核心移植工程入口，选择 Windows 工具链并裁剪上游组件 |
| `src/` | 核心宿主程序，构建 `Fcitx5` 可执行文件 |
| `fcitx5/` | 上游 Fcitx5 Git 子模块，提供 Core、Config、Utils 和通用插件 |
| `libime/`、`chinese-addons/` | 固定版本的解码库、数据工具和拼音输入引擎 |
| `patches/`、`cmake/pinyin-lock.json` | Windows 兼容补丁和版本、数据校验值 |
| `scripts/` | 隔离拼音构建、补丁应用和免注册集成验证 |
| `windows-cross/` | Git 子模块，提供 MSYS2 / Clang 工具链配置 |
| `win32/CMakeLists.txt` | 独立的 Windows TSF 工程入口 |
| `win32/dll/` | COM 类工厂、DLL 入口、输入法注册与注销、辅助函数 |
| `win32/tsf/` | TSF 生命周期、事件订阅、按键回调、编辑会话、composition 和 Pipe 客户端 |
| `win32/ipc/` | Core/TSF 共用的 Named Pipe framing 和协议定义 |
| `win32/assets/` | 输入法图标，使用 ImageMagick 将 SVG 转为 ICO |
| `win32/tests/` | Windows 工程的 CTest 测试 |
| `win32/scripts/` | 开发期注册、注销、格式化和格式检查脚本 |
| `.github/workflows/ci.yml` | 核心双架构构建、TSF 构建和测试、核心开发包发布 |

## 当前架构与实现边界

### 两套独立构建工程

- 根工程构建上游库及 `src/main.cpp`，没有调用 `add_subdirectory(win32)`。
- `win32` 工程构建 `tsf` 静态库和 `fcitx5-x86_64` DLL；DLL 目前只链接 `tsf`，没有链接 Fcitx5 Core。
- 根工程使用 MSYS2 / Clang Windows GNU 工具链；TSF 工程依赖 Windows SDK 和 ATL。不要将两者的编译器、头文件、运行库环境混为一谈。
- Core 与 TSF 当前通过用户级 Named Pipe 通信；Core 仍是独立宿主进程，TSF DLL 未内嵌 Core。当前协议和连接管理是最小实现，后续可继续加固。

### 已有实现

- 核心宿主创建 `fcitx::Instance`，注册默认插件加载器和 Windows direct-input 静态引擎；拼音可用时创建内存中的 Windows 输入法组并运行事件循环。
- `EventDispatcher` 已创建并挂接；Named Pipe 服务线程通过它把请求投递到 Core 主事件循环。
- 工具链和核心 CI 已配置 AMD64、ARM64 目标；这不表示 TSF DLL 已支持这两种架构。
- Windows DLL 已有 COM 创建、引用计数、注册/注销、简体中文 profile 和图标注册逻辑。
- DLL 内嵌品牌图标，profile 使用 DLL 路径和图标索引 0；注册 `GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT` 以声明桌面输入指示器支持，不声明尚未验证的 `IMMERSIVESUPPORT`。修改类别或 profile 图标后需重新注册，单纯替换 DLL 不会更新注册信息。
- TSF 已有激活/停用、线程管理和文本编辑事件订阅、焦点文档切换处理以及按键事件订阅。
- 按键经 Core 交给真实 Pinyin 引擎，返回消费结果、中文提交、预编辑、光标和候选页；TSF 持续维护 composition，通过独立 edit session 写入文档。
- TSF 有基础预编辑下划线、UTF-16 光标和不抢焦点的候选浮窗，支持同步编辑失败后异步排队、取消和焦点切换。
- TSF 已实现 `GUID_LBI_INPUTMODE` Language Bar 输入模式项，激活时添加、停用时移除；中文显示“中”、英文显示“A”，点击通过 TSF 消息窗口切换现有 keyboard compartment。透明单色图标由 GDI 绘制，并声明 `TF_LBI_STYLE_TEXTCOLORICON` 供系统主题着色；用户已在重新注册后确认输入指示器功能正常，多显示器 DPI、主题及 Explorer 重启行为仍未验证。
- `test_dll` 仍仅测试辅助函数；`test_langbar` 覆盖语言栏 COM 接口、通知、图标像素和生命周期。`ipc_probe` 验证真实拼音，`tsf_probe` 加载实际 DLL 并使用真实 TSF context 和内存 text store，但以测试适配器代替未注册 TIP 的按键订阅和语言栏管理器，并显式模拟按键/焦点/语言栏点击回调。
- `test_register` 加载实际 DLL 检查内嵌品牌图标和 Shell 图标提取，使用模拟 COM 管理器验证 profile 字符长度、DLL 路径、类别和失败返回；不执行系统注册或修改输入法设置。

### 尚未实现或接入

- 完整 Windows `InputContext` 能力仍在完善；当前按焦点创建/销毁远端 context，不持久保存多个输入框的未提交预编辑。
- 拼音已接入并验证；双拼、五笔、Rime 的实际输入和部署尚未验证。
- 基础消费/放行、Ctrl+Space、按键释放和布局字符转换已接入；死键、AltGr、复杂非美式布局、密码/安全 context 和完整快捷键尚未验证。
- 周边文本、转发按键、格式化预编辑区间和跨断线提交可靠性尚未实现。
- 候选鼠标交互、TSF UIElement/无障碍、普通通知区域托盘、配置界面、原生剪贴板和全面 DPI 适配尚未实现或验证；语言栏输入模式项已实现，用户已确认重新注册后的输入指示器功能正常。
- 统一安装包、核心与 DLL 的联合部署，以及 TSF 的 ARM64/x86 构建与验证。
- 默认配置关闭上游 X11、Wayland、DBus、server 和 keyboard engine 等组件；不要直接启用 Linux 前端来替代 Windows 实现。
- 上游已有候选、配置、引擎管理和输入上下文等抽象，优先复用；插件能构建不代表其 Windows 系统后端已经实现。

## 开始任务前

1. 检查 `git status --short`，保留用户现有修改；确认任务属于根工程、TSF 工程还是两者之间的桥接。
2. 检查目标目录中更具体的 `AGENTS.md`，阅读相应 CMake 文件和调用链。
3. 检查 `git submodule status`。缺少源码时可检出仓库固定版本：

   ```powershell
   git submodule update --init --recursive
   ```

4. 不使用 `git submodule update --remote`，不顺手升级子模块，不提交子模块版本变更，除非任务明确要求。
5. 优先在主仓库完成 Windows 接入。确需修改子模块时，先说明原因、影响和版本管理方式，并检查子模块内的开发约束。
6. 初始化后运行 `scripts/apply-pinyin-patches.ps1`。本项目在主仓库记录补丁，不创建本地子模块提交；不要把补丁产生的 dirty 状态误当作待丢弃修改。

## 构建与验证

### 核心工程

- 要求 CMake 3.27+，以及 MSYS2 的 Clang、Ninja、pkgconf、ECM、dlfcn、libuv 和 gettext 工具。
- 默认 MSYS2 根目录为 `C:/msys64`；工具链也支持通过 `MSYSTEM_PREFIX` 或 `MSYS2_ROOT` 定位。
- AMD64 使用 `clang64` sysroot；ARM64 使用 `clangarm64` sysroot，需要目标架构对应的依赖。
- 编译器必须与工具链的 Windows GNU 目标匹配；遇到问题先检查 PATH 和 CMake 缓存，不要修改源码来掩盖环境错误。
- 当前固定 Core 5.1.22、chinese-addons 5.1.15、libime 1.1.17。后一版本由实际 API 需求决定，不能仅依据 chinese-addons 声明的 1.1.14 最低版本降级。
- 完整 AMD64 拼音构建见 `docs/pinyin.md` 和 `scripts/build-pinyin.ps1`，额外要求同运行库的 Boost/iostreams 和 zstd。默认从源生成数据；已验证路径为 SHA256 固定的预编译数据归档模式。
- 在仓库根目录、正确的 MSYS2 / Clang 工具环境中执行：

  ```powershell
  cmake -B build/x86_64 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DARCH=AMD64
  cmake --build build/x86_64
  ```

- ARM64 的对应命令：

  ```powershell
  cmake -B build/arm64 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DARCH=ARM64
  cmake --build build/arm64
  ```

- 根工程关闭了上游测试选项，不要把根目录构建成功描述为通过上游测试。
- 根工程不强制覆盖 `CMAKE_INSTALL_PREFIX`。部署验证时在配置阶段显式指定隔离前缀，避免直接写入系统目录：

  ```powershell
  $env:DESTDIR = "dist/x86_64"
  cmake --install build/x86_64
  ```

### TSF 工程

- 要求 CMake 3.27+、Ninja、适用于 Windows SDK / ATL 环境的 `clang` / `clang++`，以及提供 `magick` 命令的 ImageMagick。
- DLL 品牌图标通过 RC 编译嵌入。Clang GNU 风格驱动的资源编译应使用 `llvm-rc`；旧缓存错误选择 `rc.exe` 时通过 `-DCMAKE_RC_COMPILER="C:/Program Files/LLVM/bin/llvm-rc.exe"` 修正工具配置。
- 推荐使用已初始化 Visual Studio 开发环境的终端，并确认调用的不是根工程所用的 MSYS2 Windows GNU 编译器。
- 在 `win32` 目录执行，或为命令明确设置工作目录：

  ```powershell
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
  cmake --build build
  ctest --test-dir build --output-on-failure
  ```

- 当前 DLL 目标名固定为 `fcitx5-x86_64`，构建配置没有提供 TSF 双架构矩阵；不要仅凭目标名称推断实际产物架构。
- 修改辅助函数时运行现有测试；修改 COM/TSF 行为时，优先补充相关回归测试，并明确哪些验证仍需真实 Windows 应用配合。
- CI 中 `md5sum -c checksum` 检查生成图标。只有图标或生成流程有意变化时才考虑更新 checksum，不能为了通过检查直接替换它。

### 格式与结果报告

- 遵循各目录的 `.clang-format`，保持既有风格，不对无关文件批量重排。
- 主仓库提供 `win32/scripts/format.ps1` 和 `win32/scripts/lint.ps1`；脚本基于当前工作目录执行 `git ls-files`，调用前确认检查范围。
- 优先验证修改涉及的目标，再考虑更广泛的构建和测试。
- 如果依赖或工具版本阻塞验证，报告具体命令、错误和未验证范围，不宣称测试通过。
- 旧基线检查的 CMake 为 3.23.2；本次环境为 CMake 4.4.4，AMD64 Core、libime、chinese-addons 和 TSF 均已构建，不能继续引用旧版本限制作为本次验证结果。
- 不自动安装或升级系统工具，不为绕过环境问题降低项目要求，除非任务明确要求。

## 已知问题与开发注意事项

以下事项来自静态检查，尚未通过完整运行验证；涉及对应代码时重新核对，不在无关任务中顺手修复。

- COM 卸载返回值、CLSID 验证和 composition sink 查询已修复；免注册 probe 覆盖工厂创建、接口和最终引用释放。真实注册激活仍未验证。
- 按键测试回调无编辑副作用；实际编辑请求持有原始 context 和代次。IPC 超时或文档写入失败仍可能丢失输入，不提供跨进程故障的 exactly-once 保证。
- Core 5.1.22 的 Windows `StandardPaths` 不使用旧资源目录环境覆盖；项目补丁将内置资源目录改为安装树的 `share` / `share/fcitx5`。宿主必须位于 `bin`。
- 当前 Windows 默认路径计算要求宿主可执行文件位于 `bin` 目录；根工程已将构建输出统一到 `bin`。构建树中的宿主可以启动，但完整资源验证应使用 `cmake --install` 生成的安装树。
- 上游仍有部分 Windows 空实现或降级实现，例如启动外部进程和根据 PID 查询可执行文件；使用相关能力前检查平台分支。
- 注册了某些 TSF category 不代表相应能力已经实现；新增能力声明时必须与实际接口和行为一致。

## 代码修改与系统安全

- 保持改动聚焦，复用 Fcitx5 现有抽象；不要未经要求进行架构重写、依赖升级或无关重构。
- 不把 demo 的固定文本逻辑扩展为业务实现；真正输入必须依据核心处理结果消费按键并提交文本。
- COM 方法应检查输入指针、初始化输出参数、正确返回 HRESULT，并可靠处理引用计数和失败路径。
- TSF 编辑应在合适的编辑会话中进行，确保异步操作持有正确的 context，避免依赖已经变化的焦点状态。
- 核心事件循环与 Windows/TSF 调用之间需要明确线程边界；不要从任意回调线程直接操作核心状态。
- 涉及 DLL 内嵌核心或独立进程 IPC 的任务，先说明所选方案及线程、生命周期、ABI/运行库和部署影响，不假设两套工程可以直接拼接。
- `install.ps1` / `uninstall.ps1` 会请求管理员权限并修改注册状态，而且路径依赖在 `win32` 下运行。未经用户明确要求，不执行注册、注销或修改系统输入法设置。
- 不自动修改注册表、复制文件到系统目录、执行安装包，或将这些操作作为普通测试步骤。
- 不创建提交或新分支，除非用户明确要求。
- 完成后简要报告修改文件、行为变化、执行的验证和仍未验证的内容；架构或实现进度改变时更新本文件。

## 进度更新：Core/TSF 基础拼音链路

- 当前已实现用户级 Windows Named Pipe：`win32/ipc/protocol.h` 定义版本化 framing，Core 端服务在 `src/windowsfrontend.cpp`，TSF 端客户端在 `win32/tsf/pipeclient.cpp`。
- 协议 v3 包括 context/焦点/按键、Reset、PollState 和幂等 SetMode，返回预编辑、UTF-8 字节光标、候选页、mode 和 revision。TSF 使用 100 ms 同线程轮询接收延迟变化，不是服务端异步推送。升级协议必须同时更新 Core 与 TSF DLL。
- TSF 订阅线程管理器的 `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE`，将系统输入法开关同步到 Core 的 Pinyin/direct-input 模式；新焦点 context 和重连继承该状态。只注册精确 Ctrl+Space preserved key，不注册普通 Space。免注册 probe 覆盖真实 compartment 通知和显式模拟的快捷键回调，不能据此宣称真实系统热键派发已验证。
- `win32/tsf/langbaritem.cpp` 提供 Language Bar 输入模式项，复用相同的 compartment 状态和 Core SetMode；停用后仍被持有的对象隐藏并解除窗口绑定，独立持有 DLL 引用。免注册 probe 用语言栏适配器验证注册/移除、模式刷新、点击、失败回滚和迟到点击隔离，不代表 Windows 输入指示器视觉效果已验证。
- 输入指示器注册已补充 `SYSTRAYSUPPORT` 和内嵌 DLL 品牌图标，并修正 `RegisterProfile` 长度参数为字符数；免注册测试覆盖注册传参和真实 DLL 图标提取，用户已在重新注册后确认输入指示器功能正常。
- 管道按 SID/Windows session 命名，设置当前用户 ACL、拒绝远程访问、支持多连接，并校验 context 的连接归属。客户端有可取消的有界 overlapped I/O 和重连；服务端回收断开的 contexts。
- 默认 `ENABLE_KEYBOARD=OFF`、`ENABLE_WINDOWS_ASCII_FALLBACK=OFF`。静态 Windows direct-input 引擎只放行按键，不绕过真实拼音引擎提交 ASCII。
- AMD64 真实拼音 IPC 和免注册 TSF context 文本插入已验证，包括 `nihao` 中文选词/提交、异步取消、焦点及迟到请求隔离。尚未验证真实注册 TIP 的系统激活、按键派发和普通应用输入，也未验证候选窗口视觉效果及 ARM64 拼音/TSF。
- `scripts/test-pinyin.ps1` 启动隔离 Core、运行两个 probes 和 CTest，再停止自身启动的进程；不执行注册、注销或修改系统输入法设置。
