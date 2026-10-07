# Fcitx5 Windows 开发指南

## 适用范围与项目定位

- 本文件适用于整个仓库；子目录中的 `AGENTS.md` 对其目录有更具体的约束时，优先遵守子目录说明。
- 本项目是尚未完成的 Fcitx5 Windows port，使用 C++20、CMake 和 PowerShell。
- 默认使用中文沟通，保留代码标识符、接口名称和命令的原文。
- 以下进度基于 2026-10-07 的代码检查。功能变化时同步更新相关说明，以实际代码为准，不把已知缺口当作永久设计限制。
- 不要将“可以编译”“可以注册为输入法”“可以提交固定字符”描述为“已经支持 Fcitx5 输入”。

## 项目结构

| 路径 | 职责 |
| --- | --- |
| `CMakeLists.txt` | 核心移植工程入口，选择 Windows 工具链并裁剪上游组件 |
| `src/` | 核心宿主程序，构建 `Fcitx5` 可执行文件 |
| `fcitx5/` | 上游 Fcitx5 Git 子模块，提供 Core、Config、Utils 和通用插件 |
| `windows-cross/` | Git 子模块，提供 MSYS2 / Clang 工具链配置 |
| `win32/CMakeLists.txt` | 独立的 Windows TSF 工程入口 |
| `win32/dll/` | COM 类工厂、DLL 入口、输入法注册与注销、辅助函数 |
| `win32/tsf/` | TSF 生命周期、事件订阅、按键回调、编辑会话和 composition |
| `win32/assets/` | 输入法图标，使用 ImageMagick 将 SVG 转为 ICO |
| `win32/tests/` | Windows 工程的 CTest 测试 |
| `win32/scripts/` | 开发期注册、注销、格式化和格式检查脚本 |
| `.github/workflows/ci.yml` | 核心双架构构建、TSF 构建和测试、核心开发包发布 |

## 当前架构与实现边界

### 两套独立构建工程

- 根工程构建上游库及 `src/main.cpp`，没有调用 `add_subdirectory(win32)`。
- `win32` 工程构建 `tsf` 静态库和 `fcitx5-x86_64` DLL；DLL 目前只链接 `tsf`，没有链接 Fcitx5 Core。
- 根工程使用 MSYS2 / Clang Windows GNU 工具链；TSF 工程依赖 Windows SDK 和 ATL。不要将两者的编译器、头文件、运行库环境混为一谈。
- 当前没有 TSF DLL 与核心宿主之间的 IPC，也没有在 DLL 内嵌入核心。后续架构尚未确定，不要把某一种方案写成既定事实。

### 已有实现

- 核心宿主设置资源目录相关环境变量，创建 `fcitx::Instance`，注册默认插件加载器，初始化实例并运行事件循环。
- `EventDispatcher` 已创建并挂接，但没有接入 TSF 业务请求。
- 工具链和核心 CI 已配置 AMD64、ARM64 目标；这不表示 TSF DLL 已支持这两种架构。
- Windows DLL 已有 COM 创建、引用计数、注册/注销、简体中文 profile 和图标注册逻辑。
- TSF 已有激活/停用、线程管理和文本编辑事件订阅、焦点文档切换处理以及按键事件订阅。
- 当前按键路径忽略键值并请求编辑会话；编辑会话创建 composition、写入固定字符 `哈`，然后立即结束 composition。
- `test_dll` 只检查 GUID 格式化和 UTF-8 转宽字符串，不测试 DLL 加载、注册、TSF 生命周期或真实输入。

### 尚未实现或接入

- Windows `fcitx::InputContext`、TSF context 映射、按键转换、焦点同步，以及核心提交文本/预编辑/候选变化的回传。
- 真正的输入引擎集成和数据部署，例如拼音、双拼、五笔或 Rime。
- 正常的按键消费与放行、修饰键和按键释放处理、输入状态切换及快捷键。
- 持续预编辑、预编辑光标和显示属性、提交/取消、周边文本操作和可靠的异步编辑生命周期。
- Windows 候选界面、语言栏/托盘、配置界面、原生剪贴板接入和 DPI 适配。
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

## 构建与验证

### 核心工程

- 要求 CMake 3.27+，以及 MSYS2 的 Clang、Ninja、pkgconf、ECM、dlfcn、libuv 和 gettext 工具。
- 默认 MSYS2 根目录为 `C:/msys64`；工具链也支持通过 `MSYSTEM_PREFIX` 或 `MSYS2_ROOT` 定位。
- AMD64 使用 `clang64` sysroot；ARM64 使用 `clangarm64` sysroot，需要目标架构对应的依赖。
- 编译器必须与工具链的 Windows GNU 目标匹配；遇到问题先检查 PATH 和 CMake 缓存，不要修改源码来掩盖环境错误。
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
- 本次基线检查所在环境的 CMake 为 3.23.2，两套工程都在最低版本检查处停止；这属于环境限制，不是编译失败的代码证据。
- 不自动安装或升级系统工具，不为绕过环境问题降低项目要求，除非任务明确要求。

## 已知问题与开发注意事项

以下事项来自静态检查，尚未通过完整运行验证；涉及对应代码时重新核对，不在无关任务中顺手修复。

- `win32/dll/main.cpp` 的 `DllCanUnloadNow()` 返回布尔表达式，当前结果与应返回的 `S_OK` / `S_FALSE` 含义相反。
- `DllGetClassObject()` 没有验证请求的 CLSID。
- `Tsf` 继承 `ITfCompositionSink`，但 `QueryInterface()` 没有对应分支。
- KeyUp 和 preserved key 回调返回成功，却没有设置 `pfEaten` 输出参数。
- `processKey()` 没有检查 context 是否为空，也没有检查编辑会话请求结果，仍始终报告按键已消费。
- 按键测试回调会触发编辑副作用；异步编辑会话使用可随焦点变化的成员 context。设计真实输入链路时需处理请求所属 context、生命周期和事件顺序。
- `src/main.cpp` 将运行时资源指向 `share` / `share/fcitx5` 和 `lib/fcitx5`；上游 Windows `StandardPaths` 的内置回退仍使用 `data` / `data/fcitx5`，因此部署和插件加载任务必须保持宿主位于 `bin`，并核对环境变量与安装布局。
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
