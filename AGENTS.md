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
| `win32/dll/` | COM 类工厂、DLL 入口、输入法注册与注销、内嵌品牌图标和辅助函数 |
| `win32/tsf/` | TSF 生命周期、事件订阅、按键回调、编辑会话、composition、候选窗、语言栏输入模式项和 Pipe 客户端 |
| `win32/ipc/` | Core/TSF 共用的 Named Pipe framing 和协议定义 |
| `win32/settings/` | 独立 WinUI 3 设置程序，使用 MSBuild 构建拼音设置和只读词库页 |
| `win32/assets/` | 输入法图标，使用 ImageMagick 将 SVG 转为 ICO |
| `win32/tests/` | Windows 工程的 CTest 测试 |
| `win32/scripts/` | 开发期注册、注销、DLL 占用释放、格式化和格式检查脚本 |
| `docs/pinyin.md` | 拼音构建、隔离部署、IPC/TSF 设计和验证边界 |
| `docs/build.md` | 统一构建、部署、注册/释放脚本和验证入口 |
| `docs/windows-settings-design.md`、`docs/windows-candidate-rendering.md` | 设置/服务控制和候选窗的当前实现、设计与验证边界 |
| `.github/workflows/ci.yml` | 核心双架构构建、TSF 测试、AMD64 拼音/词库集成验证、WinUI 构建和核心开发包发布 |

## 当前架构与实现边界

### 三套独立构建工程

- 根工程构建上游库及 `src/main.cpp`，没有调用 `add_subdirectory(win32)`。
- `win32` 工程构建 `tsf` 静态库和 `fcitx5-x86_64` DLL；DLL 目前只链接 `tsf`，没有链接 Fcitx5 Core。
- 根工程使用 MSYS2 / Clang Windows GNU 工具链；TSF 工程依赖 Windows SDK 和 ATL。不要将两者的编译器、头文件、运行库环境混为一谈。
- `win32/settings` 使用独立 MSBuild 工程构建 WinUI 设置程序。
- Core 是独立宿主进程，TSF DLL 加载到应用进程，两者通过用户级 Named Pipe 通信，不传递 C++ 对象、分配器或引擎 ABI。TSF 激活后在工作线程自动后台启动 Core；Settings 按需启动。当前没有统一安装器。

### IPC、线程与状态设计

- `win32/ipc/protocol.h` 定义协议 v5 framing；服务在 `src/windowsfrontend.cpp`，客户端在 `win32/tsf/pipeclient.cpp`。context-independent 的 OpenSettings/GetSettings/SetSettings/GetDictionaries 提供设置及第三方词库查询；按键回复带设置运行时代次。升级协议必须同时更新 Core、TSF DLL 和设置程序。
- 管道按 SID/Windows session 命名，设置当前用户 ACL、拒绝远程访问、支持多连接，并校验 context 的连接归属。服务端回收断开的 contexts；客户端采用可取消 overlapped I/O，每次读写等待上限为 500 ms。
- 服务线程通过 `EventDispatcher` 将请求投递到 Core 主事件循环；TSF 在自身线程通过隐藏消息窗口每 100 ms 轮询延迟变化，断开后在有焦点 context 时每两秒尝试重连。当前没有服务端异步推送，超时请求不重放。
- `win32/ipc/service.h` 提供当前用户、session 和登录身份（LUID、登录时间）级服务互斥和正常退出事件；Core 在主事件循环检查退出，Settings 在 UI 线程关闭窗口。启动失败共享 10 秒重试间隔，手动关闭状态保存于当前登录的 `%LOCALAPPDATA%/fcitx5/*.state`，所有 DLL 卸载后仍有效，重启服务解除。不得按进程名强制终止用户应用。
- TSF 订阅线程管理器的 `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE`，系统开关、Ctrl+Space 和语言栏点击复用同一模式状态及 Core SetMode。新的焦点 context 和重连继承该状态；预编辑按焦点创建/销毁，不保存多个输入框的未提交文本。
- 按键测试回调无 IPC 或编辑副作用。实际按键消费依据 Core 回复；编辑请求持有原始 context、代次和不可变快照，串行处理并拒绝迟到请求写入新 context。IPC 超时或文档写入失败仍可能丢失输入，不提供跨进程故障的 exactly-once 保证。

### 已有实现

- 核心宿主创建 `fcitx::Instance`，注册默认插件加载器和 Windows direct-input 静态引擎；拼音可用时创建内存中的 Windows 输入法组并运行事件循环。
- `EventDispatcher` 已创建并挂接；Named Pipe 服务线程通过它把请求投递到 Core 主事件循环。
- 工具链和核心 CI 已配置 AMD64、ARM64 目标；这不表示 TSF DLL 已支持这两种架构。
- Windows DLL 已有 COM 创建、引用计数、注册/注销、简体中文 profile 和图标注册逻辑。
- DLL 内嵌品牌图标，profile 使用 DLL 路径和图标索引 0；注册 `GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT` 以声明桌面输入指示器支持，不声明尚未验证的 `IMMERSIVESUPPORT`。修改类别或 profile 图标后需重新注册，单纯替换 DLL 不会更新注册信息。
- TSF 已有激活/停用、线程管理和文本编辑事件订阅、焦点文档切换处理以及按键事件订阅。
- 按键经 Core 交给真实 Pinyin 引擎，返回消费结果、中文提交、预编辑、光标和候选页；TSF 持续维护 composition，通过独立 edit session 写入文档。
- TSF 有基础预编辑下划线、UTF-16 光标和不抢焦点的候选浮窗，支持同步编辑失败后异步排队、取消和焦点切换。
- 候选浮窗使用 DirectWrite 统一测量/排版、Direct2D 彩色字体绘制，标签/正文/注释分列并按字形高度计算行高；显式 VS15 文本样式优先单色符号字体。窗口独立使用 PMv2，TSF 几何查询按 owner awareness 执行并将锚点转为物理屏幕坐标，所有临时 DPI 状态均恢复。绘制资源及不变文本 layout 复用，立即提交避免等待屏幕刷新；不变更 Core/IPC 或输入法注册信息。设计与验证边界见 `docs/windows-candidate-rendering.md`。
- TSF 已实现 `GUID_LBI_INPUTMODE` Language Bar 输入模式项，激活时添加、停用时移除；中文显示“中”、英文显示“A”，英文模式不隐藏图标。点击通过 TSF 消息窗口切换现有 keyboard compartment；这不是 `Shell_NotifyIcon` 普通托盘图标，显示由系统语言栏设置控制。透明单色图标由 GDI 绘制，并声明 `TF_LBI_STYLE_TEXTCOLORICON` 供系统主题着色。
- 语言栏对象停用后隐藏并解除窗口绑定；对象独立持有 DLL 引用，迟到点击不影响新激活实例。用户已在重新注册后确认输入指示器功能正常，多显示器 DPI、主题及 Explorer 重启行为仍未验证。
- `test_dll` 仍仅测试辅助函数；`test_langbar` 覆盖语言栏 COM 接口、通知、图标像素和生命周期。`ipc_probe` 验证真实拼音，`tsf_probe` 加载实际 DLL 并使用真实 TSF context 和内存 text store，但以测试适配器代替未注册 TIP 的按键订阅和语言栏管理器，并显式模拟按键/焦点/语言栏点击回调。
- `test_register` 加载实际 DLL 检查内嵌品牌图标和 Shell 图标提取，使用模拟 COM 管理器验证 profile 字符长度、DLL 路径、类别和失败返回；不执行系统注册或修改输入法设置。
- 语言栏右键通过原生 popup menu 提供“输入法设置”“重启服务”“关闭服务”。重启会重启/启动 Core，只有原本打开的 Settings 才重新打开；关闭会停止两者并暂停自动拉起。TSF 在 Settings 自身进程内时，由短暂运行的 `Fcitx5 --restart-services/--stop-services` 控制进程等待宿主退出，避免等待自身。真实已注册指示器右键派发尚未验证；左键仍切换中英文。
- 语言栏右键另提供“用户数据文件夹”，后台通过 Windows Roaming AppData known folder 打开 `%APPDATA%/Fcitx5`（含 `config/fcitx5` 设置和 `pinyin` 用户词典、学习历史），不存在时创建目录。服务关闭时仍可使用，不启动 Core/Settings，不变更 IPC 协议。
- `scripts/deploy-tsf.ps1 -Prefix -TsfBuild` 将 DLL 复制到隔离安装树的 `tsf`；部署后的 TSF 在激活时固定定位并后台启动相邻 `bin/Fcitx5.exe`，Settings 位于 `settings/Fcitx5Settings.exe`。部署脚本本身不启动服务、不注册或注销，旧构建目录 DLL 不能自动推断 Core 安装路径。
- `win32/settings` 是第三套独立 MSBuild 工程，使用框架依赖的 Windows App SDK 1.8；要求当前用户有 x64 Framework/DDLM >= 8000.994.2142.0 和 Visual C++ 运行库。`scripts/build-settings.ps1 -Prefix` 部署到隔离安装树的 `settings`，不安装系统运行时。
- 设置窗口提供全拼/双拼及八种内置键位；Core 在主事件循环切换 `pinyin`/`shuangpin` entry 和真实 `ShuangpinProfile`，保存用户 `conf/windows.conf` 并在重启恢复。存储失败先返回，不清空解码上下文；成功切换取消预编辑，英文 context 保持关闭。设置运行时代次拒绝旧预编辑但保留已经接收的提交。
- 第三方词库使用上游 `%APPDATA%/Fcitx5/pinyin/dictionaries`，加载 Fcitx5/libime 二进制 `.dict`，全拼/双拼共用。Core 每秒扫描该目录的普通 `.dict`/`.disable` 文件，连续两次稳定后在主事件循环调用上游 `dictmanager` 重载；解析仍走现有工作线程，完成回调及状态在 Core 主线程。支持新增、替换、重命名、删除，不递归子目录；损坏文件独立报告失败，`name.dict.disable` 延续上游停用语义。未修改词库格式或固定候选排序。
- WinUI 新增“拼音”“词库”tab；词库页通过 IPC 获取插件实际发现的扩展词库及加载中/已加载/失败/停用状态，显示期间每两秒刷新，只展示、不提供编辑。打开目录按钮通过 Roaming AppData known folder 创建并打开第三方词库目录，Core 不可连接时仍可使用。插件状态查询扩展记录在主仓库兼容补丁，不创建子模块提交。

### 尚未实现或接入

- 完整 Windows `InputContext` 能力仍在完善；当前按焦点创建/销毁远端 context，不持久保存多个输入框的未提交预编辑。
- 全拼及小鹤/自然码双拼真实中文提交已由 IPC/TSF probes 验证；其他双拼键位、五笔、Rime 的真实输入和完整应用兼容性仍需验证。
- 基础消费/放行、Ctrl+Space、按键释放和布局字符转换已接入；死键、AltGr、复杂非美式布局、密码/安全 context 和完整快捷键尚未验证。
- 周边文本、转发按键、Core 格式化预编辑区间的完整映射和跨断线提交可靠性尚未实现；现有 TSF composition 已有基础下划线显示属性。
- 候选鼠标交互、TSF UIElement/无障碍、普通通知区域托盘、原生剪贴板和全面 DPI 适配尚未实现或验证；已有 WinUI 拼音设置窗口，当前 200% 缩放已截图检查，跨屏 DPI/主题/文本缩放仍需全面验证。
- 统一安装包及 TSF 的 ARM64/x86 构建与验证尚未实现；Core/TSF/Settings 的联合构建及隔离目录部署已由 `scripts/build-and-deploy.ps1` 实现。
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

- `scripts/build-and-deploy.ps1` 是 AMD64 Release 统一入口：默认预编译数据，初始化固定子模块并应用补丁，在 `build/all` 隔离构建 Core/libime/addons/TSF，调度独立 Settings 构建并将三套工程产物暂存于 `build/all/prefix`，初始化 VS x64 环境并明确使用独立 LLVM/llvm-rc，默认运行 TSF CTest，成功后复制运行树到 `dist/pinyin`。支持 `-Prefix`/`-BuildRoot`/工具及依赖路径/`-DataArchive`/`-Jobs`/`-SkipTests`，`-WhatIf` 无构建及部署副作用。不安装工具/运行时，不注册/注销、不停止或启动服务；目标或暂存文件占用时退出。最终复制不是原子切换，部署前需由用户关闭服务及加载该 DLL 的应用，或选择另一隔离前缀。`build-pinyin.ps1 -BuildRoot` 供统一入口隔离缓存，默认单独调用仍使用 `build`。
- 预编译数据共享缓存位于 `build/pinyin-data`；下载前还查找该目录及 `build/deps` 的 `libime*.pkg.tar.zst`（包括既有 `libime-data.pkg.tar.zst`），必须匹配锁定 SHA256 才复用。显式 `-DataArchive` 也会先校验并填充标准缓存。已有解包目录或部署文件不单独作为版本校验依据；重复构建继续使用 Ninja 增量缓存。
- `-BuildRoot` 控制 Core/libime/addons/TSF 的构建目录及暂存前缀，不改变 Settings 的 MSBuild 输出/缓存 `win32/build/settings`。三套工程仍独立；Settings 产物构建后复制到统一暂存运行树。

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

- 根工程默认关闭上游测试选项，不要把根目录构建成功描述为通过上游测试。根目录单独的 CMake 构建不包含 libime、chinese-addons 和拼音数据的构建/部署；完整链路使用 `scripts/build-pinyin.ps1`。
- 根工程不强制覆盖 `CMAKE_INSTALL_PREFIX`。部署验证时在配置阶段显式指定隔离前缀，避免直接写入系统目录：

  ```powershell
  cmake -S . -B build/x86_64 "-DCMAKE_INSTALL_PREFIX=$PWD/dist/x86_64"
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
- 环境结论以任务当次实际命令输出为准，不沿用历史工具版本或环境阻塞。当前记录的 AMD64 Core/libime/chinese-addons 构建和 TSF Debug/Release 验证范围见“当前验证记录”。
- 不自动安装或升级系统工具，不为绕过环境问题降低项目要求，除非任务明确要求。

## 已知问题与开发注意事项

以下包含已修复行为、设计约束和剩余验证范围；涉及对应代码时重新核对，不在无关任务中顺手修复。

- COM 卸载返回值、CLSID 验证和 composition sink 查询已修复；免注册 probe 覆盖工厂创建、接口和最终引用释放。用户已确认重新注册后的输入指示器功能正常，但不能据此宣称普通应用的系统按键派发和完整输入兼容性已验证。
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
- `install.ps1` / `uninstall.ps1` 默认操作仓库 `dist/pinyin/tsf/fcitx5-x86_64.dll`，不依赖当前工作目录；支持 `-Prefix` 或 `-DllPath` 以及 `-WhatIf`。实际注册/注销按需请求管理员权限并等待结果，不停止服务或删除文件。未经用户明确要求，不执行注册、注销或修改系统输入法设置。
- `win32/scripts/release-tsf.ps1` 默认注销同一部署 DLL，支持 `-Prefix`/`-DllPath`；`-SkipUnregister` 跳过注销，`-Force` 会终止核实加载精确 DLL 路径的进程，可能包括编辑器、浏览器和 Explorer。Handle/`tasklist /m` 候选必须经实际模块路径核对，无法核对的进程不终止。`-WhatIf` 不要求提权，不调用 Handle、不接受 EULA、不重启 ctfmon。不能作为普通测试或解锁步骤自动执行，注销和终止应用均需用户明确授权；服务停止请使用指示器菜单。
- `win32/tests/test_scripts.ps1` 独立验证注册/释放脚本的路径、参数引号、退出码、`-WhatIf` 和精确 DLL 路径筛选；系统变更命令和进程发现全部使用模拟，要求默认部署 DLL 存在，不属于 CTest。可在不修改系统注册、不终止应用的情况下运行。
- 注册元数据更新需要重新注册；注销不会让已有应用立即卸载 DLL。遇到 DLL 被占用，优先采用隔离构建目录，不自动终止宿主进程或重启 Explorer。
- 不自动修改注册表、复制文件到系统目录、执行安装包，或将这些操作作为普通测试步骤。
- 不创建提交或新分支，除非用户明确要求。
- 完成后简要报告修改文件、行为变化、执行的验证和仍未验证的内容；架构或实现进度改变时更新本文件。

## 当前验证记录

以下保留各功能落地时的验证结果；历史记录中的 8 项或更少测试是当时的集合。当前默认 CTest 为 9 项，`ENABLE_PINYIN_INTEGRATION_TESTS=ON` 另增加三个拼音 probes，共 12 项；词库和服务 probes 由各自脚本运行，不在该选项的 CTest 集合内。

- DirectWrite/Direct2D 候选窗已通过 `win32/build/candidate-d2d` AMD64 TSF Release 构建、9 项默认 CTest 和修改文件格式检查。`test_candidate` 验证 96/120/144/192/288 DPI 同一绘制函数的彩色像素、单色 VS15 心形、长文本省略，三种 owner DPI awareness、线程状态恢复、不抢焦点、边缘定位及 owner 销毁/资源重建；192 DPI 原生 HWND 截图及五种 DPI 离屏图片位于 `build/candidate-d2d-preview`。本机 100 次暖启动选中项更新的 show + UpdateWindow 调用耗时 P50 2.086 ms、P95 3.323 ms，不等同于输入端到端延迟或 GDI 对比。用户关闭 Core 后，`scripts/test-pinyin.ps1 -Prefix ./dist/pinyin -TsfBuild ./win32/build/candidate-d2d` 已通过真实 Core 的 `ipc_probe`、`settings_probe`、加载新 DLL 的 `tsf_probe` 及 9 项 CTest，覆盖全拼/小鹤/自然码中文提交、异步编辑与焦点隔离；测试使用独立设置文件，结束后 Core/Settings 均未运行，Core 日志无错误。未注册/注销或替换默认部署 DLL；probes 使用按键/语言栏适配器，真实应用、物理跨屏 DPI、主题/高对比度及系统文本缩放仍需验证；尚无 TSF 布局变化订阅或超高候选页滚动。
- 统一构建脚本已从干净 `build/all` 构建 Core/libime/chinese-addons、TSF Release、WinUI Release 并部署至 `dist/one-click-test`，8 项 CTest 通过；重复增量构建、重复部署、任意工作目录及完整环境恢复（含空值环境变量）均已验证，386 个部署文件与暂存文件 SHA256 一致。`scripts/test-build-and-deploy.ps1` 覆盖解析、空格/单引号路径、无副作用 `-WhatIf`、非法路径/Jobs 及独占/只读文件拒绝。未更新默认部署、注册/注销或启动服务；Source 数据生成仍未在本地完整验证。
- 预编译缓存复用更新已通过本地包复用、标准缓存命中、损坏缓存修复及不匹配 SHA256 拒绝的脚本回归；不带 `-DataArchive` 的统一入口已复用 `build/pinyin-data` 标准缓存完成增量构建/隔离部署和 8 项 CTest，无词库下载。日志位于 `build/one-click-cache-reuse.log`。
- 第三方词库改动已通过 AMD64 Core/chinese-addons、TSF Release、WinUI Release 构建、8 项默认 CTest 和原有三个拼音 probes；`scripts/test-dictionaries.ps1` 通过真实启动加载、全拼/小鹤/自然码候选、热增删改、Unicode 路径、坏词库、停用及预编辑保留。WinUI 自动化通过词库加载状态、实际 Explorer 目录打开及原有保存/取消/重启/单实例检查；200% 缩放的正常及最小窗口截图位于 `build/dictionaries-settings-ui-test`。本次只使用 `dist/dictionaries-test` 隔离部署，未注册/注销或替换已注册 DLL；真实 OS 派发、跨屏 DPI、完整主题仍未验证。
- “用户数据文件夹”菜单改动已通过 `win32/build/userdata-tsf` 的 TSF Release 构建、8 项默认 CTest 和修改文件格式检查；`test_langbar` 覆盖四项菜单的文字、命令派发和停用后迟到点击。未执行注册/注销，实际 Explorer 打开和真实 OS 右键派发仍需验证。
- AMD64 Core、libime、chinese-addons 已构建；使用 SHA256 固定预编译数据的拼音部署链路已验证。本次 Core、TSF Release 和 WinUI Release 构建、8 项默认 CTest 均已通过；先前 TSF Debug/干净 Release 记录仍有效。默认 `ENABLE_KEYBOARD=OFF`、`ENABLE_WINDOWS_ASCII_FALLBACK=OFF`；direct-input 引擎只放行按键，不自行提交 ASCII。
- `ipc_probe` 验证真实拼音，`tsf_probe` 验证实际 DLL 和真实 TSF context 的文本插入，包括 `nihao` 中文选词/提交、异步取消、焦点及迟到请求隔离、compartment 和语言栏生命周期。probe 的按键订阅、语言栏管理和点击/快捷键回调使用测试适配器，不能据此宣称真实 OS 派发已验证。只注册精确 Ctrl+Space preserved key，不注册普通 Space。
- 用户实测确认重新注册后的“中/A”输入指示器功能正常；测试应用和完整 Windows 版本矩阵未记录。普通应用输入、候选窗口视觉效果、主题、Explorer 重启、多显示器 DPI 和 ARM64 拼音/TSF 仍未完成全面验证。
- `scripts/test-pinyin.ps1` 默认使用 `dist/pinyin` 和 `win32/build/pinyin-tsf`，可通过 `-Prefix` / `-TsfBuild` 指定隔离路径；已有 Core 运行时拒绝执行。脚本使用独立 Windows 设置文件启动 Core、运行三个 probes 和 CTest，再停止自身启动的进程，日志在 `build/pinyin-test`；不执行注册、注销或修改系统输入法设置。
- `scripts/test-dictionaries.ps1` 使用独立 Windows 设置文件和唯一命名的临时词库，验证实际 Core 启动加载、动态新增/替换/重命名/删除、中文路径、坏文件、停用标记、全拼/小鹤/自然码候选及重载期间保留预编辑。要求已有 Core/Settings 关闭；只停止自身启动的 Core，清理临时文件并恢复输入设置，不执行注册/注销。词库转换及加载状态以真实插件为准，文件扩展名不表示其他输入法的词库格式可直接兼容。
- `ENABLE_PINYIN_INTEGRATION_TESTS=ON` 才会将 ipc_probe、tsf_probe、settings_probe 加入 CTest，此时需事先运行已部署 Core；默认 CTest 为 9 项（含 `test_service`、`test_candidate`），不等同于真实应用输入或上游全套测试。
- `scripts/test-service.ps1 -Prefix -TsfBuild [-WithSettings]` 加载部署树中的实际 DLL，以测试适配器激活 TSF 并模拟菜单命令，验证真实 Core 自动启动、关闭、重启、暂停自动启动及 DLL 生命周期；`-WithSettings` 额外验证实际 WinUI 窗口重新打开和 Settings 宿主的委托退出。拒绝已有 Core/Settings，结束恢复原有服务控制状态，不执行系统注册或修改输入法设置。
- 本次使用 `win32/build/service-tsf` 和 `dist/pinyin/tsf` 通过上述服务控制 probe（含实际 WinUI、Settings 宿主委托及手动停止后的迟到启动拒绝），并重新通过 `scripts/test-pinyin.ps1` 的三个 probes 和 8 项 CTest。`scripts/test-settings-ui.ps1` 重新通过保存/取消、双拼键位、重启恢复和单实例，截图位于 `build/service-settings-ui-test`。未执行注册/注销，真实 OS 右键派发仍未验证。
- 注册/释放脚本更新通过 `win32/tests/test_scripts.ps1` 的模拟回归及三个管理脚本的真实 `-WhatIf` 预览，覆盖任意工作目录、含空格/单引号路径、失败退出码、参数错误、精确路径筛选和 ctfmon 预览无副作用；未执行真实注册/注销、UAC 提权或终止应用。
- 本次 `settings_probe` 验证全拼/小鹤/自然码候选与提交、英文保持、冲突/幂等/非法值、国标配置映射；只读存储失败保持真实解码预编辑。`tsf_probe` 额外覆盖小鹤/自然码文档写入及设置变化时旧异步预编辑取消。
- 经用户明确授权安装当前用户 DDLM 后，`scripts/test-settings-ui.ps1` 验证实际 WinUI 框架依赖窗口的加载、确定/取消、键位选择、重启恢复、单实例，截图在 `build/settings-ui-test`。未执行输入法注册/注销；真实右键派发、跨屏 DPI 和完整主题验证仍未完成。
