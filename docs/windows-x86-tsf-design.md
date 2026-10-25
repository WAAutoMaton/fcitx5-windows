# Windows x86 应用支持设计

## 状态与目标

本方案的实现采用独立 x64/x86 构建树，以及单根目录中的 `tsf/x86/fcitx5-x86.dll` 布局。构建、部署、打包、双 view 注册/注销和升级快照/恢复均已接入；CI 包含独立 x86 测试和双架构机器级注册 smoke。真实 x86 应用、签名部署和完整升级/重启卸载仍需隔离环境验收，不因自动化 probe 通过而宣称完成。

x86 应用无法把 x64 DLL 加载到自己的进程中，因此缺少 x86 DLL 时不会激活 Fcitx5 TSF，也不会产生对应的 TSF 诊断日志。Microsoft 对 TSF 的建议是在 64 位 Windows 上并行安装 32 位和 64 位文本服务版本，并让两个版本使用相同的 CLSID。[64-bit Considerations](https://learn.microsoft.com/zh-cn/windows/win32/tsf/64-bit-platform-considerations)

本设计的目标是在 x64 Windows 上支持 x86 Win32 应用，同时保留现有 x64 应用和 Core 的行为：

| 宿主进程 | 加载的 TSF | Core |
| --- | --- | --- |
| x64 | `fcitx5-x86_64.dll` | 共享同一个 x64 Core |
| x86 | `fcitx5-x86.dll` | 共享同一个 x64 Core |

本设计不包含 32 位 Windows、原生 ARM64 TSF、修改输入引擎 ABI 或为每种架构启动独立 Core。ARM64 应用支持仍需单独设计和验证。

## 架构原则

TSF 是宿主进程内的 COM DLL，必须与宿主进程使用相同的位数。x86 TSF 不能通过 WOW64 被 x64 宿主加载，x64 TSF 也不能被 x86 宿主加载。出进程代理不能替代 x86 TSF：`ITfContext`、编辑会话、COM apartment 和文本存储对象都属于宿主进程，不能把指针或 COM 对象转发到另一个进程。

因此实现两个从同一源码构建的 TSF 前端，继续把引擎状态和文本输入请求交给 Core：

```text
x64 application  -- x64 TSF --+
                                +-- current-user Named Pipe -- x64 Core
x86 application  -- x86 TSF --+
```

两个 TSF 版本可以同时运行。它们不得共享进程内对象；需要共享的输入法配置、学习数据和日志状态由 Core 或现有用户配置机制协调。

## 构建方案

### 独立构建树

为 x64 和 x86 使用两个完全独立的 CMake cache 和构建目录。不能在同一构建树中切换编译器、Windows SDK、ATL 或目标架构，也不能复用另一架构的静态库和生成文件。

建议的产物名称为：

```text
tsf/fcitx5-x86_64.dll
tsf/fcitx5-x86.dll
```

CMake 目标可以继续使用同一逻辑目标名，因为它们位于不同的构建树；部署和打包阶段必须使用明确的架构名称，避免两个 DLL 覆盖。

### x86 工具链

x86 TSF 使用 Windows SDK/ATL 和 LLVM 的 x86 MSVC 目标，单独编译以下内容：

- `win32/tsf` 静态库及其所有 TSF 源码；
- `win32/dll` COM、profile、category 和资源代码；
- `windows_i18n`、`windows_diagnostic` 及 TSF 依赖的 Windows 库；
- x86 版本的资源、图标和 build information。

TSF 继续使用静态 MSVC CRT，避免宿主已经加载的运行库影响 DLL。x86 构建必须通过与 x64 相同的普通/延迟 PE import 审计，不能偷偷链接 x64 运行库或 x64 第三方库。

构建脚本应显式接收 TSF 架构，分别调度两个构建树，并在复制产物前检查 PE machine 字段：

```text
fcitx5-x86_64.dll -> AMD64 (0x8664)
fcitx5-x86.dll    -> i386  (0x014c)
```

检查失败时停止部署，不能通过重命名掩盖错误。Core、Settings 和 x64 TSF 的现有构建流程不应被 x86 cache 污染。

### 源码兼容性审计

共享源码需要按 x86 编译一次，重点检查：

- `size_t`、指针、`ULONG_PTR`、`WPARAM`、`LPARAM` 与协议字段之间的隐式转换；
- 使用 `sizeof(void*)` 或将句柄/指针写入 IPC payload 的代码；
- 依赖结构体默认对齐、未固定宽度整数或 `long` 大小的序列化；
- x86 下的异常、栈大小、TLS、`Interlocked` 和原子类型实现；
- DirectWrite、Direct2D、COM、ATL 和资源编译器的 x86 链接设置。

`win32/ipc/protocol.h` 的线格式必须使用固定宽度字段和有界长度。任何新增字段都要同时验证 x86 TSF 到 x64 Core、x64 TSF 到 x64 Core 的互操作；不得把 C++ 对象、allocator、STL 容器或裸指针放进协议。

## IPC 与 Core 行为

两个架构使用相同的当前用户/session Pipe 名称、ACL、握手和超时策略。Core 不按客户端位数启动第二个服务，也不把 x86 客户端降级为本地引擎。

服务端继续以 Pipe 对端 token、用户 SID、session 和登录身份为准。x86 TSF 的位数只作为诊断元数据使用，不能改变授权、资源限额或输入策略。必要时在 Hello/连接日志中增加固定字符串 `client_arch=x86|x64`，但不记录宿主标题、路径或输入内容。

同时存在 x86 和 x64 TSF 时：

- 每个 TSF 维护自己的 `contextId`、generation、焦点和预编辑状态；
- Core 继续校验 client/context 所有权，迟到的编辑结果不得写入新焦点；
- 输入法配置、快捷键和详细日志开关仍由 Core 作为权威状态；
- 一个宿主断线或退出不得停止另一个宿主的 Pipe 请求；
- Core 仍是唯一的输入引擎进程，不能因为 x86 客户端而复制词库或模型。

## 注册与文件布局

### COM/TSF 注册

x86 和 x64 DLL 使用同一个逻辑 TSF 的 CLSID、语言 profile GUID、描述和 category 集合，并分别独立注册。Windows 会把 x86 COM 注册重定向到 32 位 registry view（例如 `HKCR\Wow6432Node\CLSID`），x64 注册进入 64 位 view；两套 `InprocServer32` 路径必须分别指向正确位数的 DLL。

注册和注销必须使用匹配位数的 `regsvr32`：

| DLL | 注册器 |
| --- | --- |
| x64 TSF | `%WINDIR%\System32\regsvr32.exe` |
| x86 TSF | `%WINDIR%\SysWOW64\regsvr32.exe` |

`RegisterServerAt` 按明确的架构 view 注册 COM server；`HKLM\SOFTWARE\Microsoft\CTF\TIP` 下的 profile/category 则由 x86/x64 共享。两种注册器都可能提供共享 profile 的图标，安装 smoke 和升级校验只接受当前安装包内的 x64 或 x86 DLL；每个 COM view 的 `InprocServer32` 仍必须匹配对应架构的唯一 DLL。注销任一版本时保留仍有 peer COM server 使用的共享元数据，最后一个版本注销时再清理。注册测试必须检查同一 CLSID 的双架构路径、profile 可枚举性、category 双向索引和 DLL 路径归属。

### 安装树

遵循 Windows 对位数文件的目录约定，建议使用以下布局：

```text
%ProgramFiles%\Fcitx5\
  bin\Fcitx5.exe
  tsf\fcitx5-x86_64.dll
  settings\Fcitx5Settings.exe

%ProgramFiles(x86)%\Fcitx5\
  tsf\fcitx5-x86.dll
```

如果最终安装器必须维护单一根目录，也可以把 x86 文件放在该根目录的专用 `tsf\x86` 子目录，但注册表中的路径必须是实际存在的 x86 DLL，不能依赖 WOW64 文件系统重定向。安装器、升级器和卸载器必须把 x86 文件列入同一份所有权和占用清单。

Core 的启动路径、Settings 的运行库和用户配置目录不因 x86 TSF 改变。x86 TSF 不能启动本地 Core，也不能读取另一进程的配置文件来绕过 IPC。

### 升级、回滚与占用

升级事务需要同时处理两个 DLL：

1. 预检 x86 和 x64 DLL 的占用、注册归属及目标路径；
2. 两个文件都准备成功后再发布新版本；
3. 任一架构文件被占用时，保留旧版本并返回可重试/重启所需状态；
4. 回滚时恢复两个架构的文件和对应 registry view；
5. 卸载时分别注销两个 view，只清理安装树拥有的文件。

不能只替换 x64 DLL 后宣称升级完成，否则 x86 宿主可能继续加载旧版本。开发脚本的 `-WhatIf` 仍不能注册表、停止服务或结束应用。

## 测试与验收

### 自动化

新增 x86 变体或参数化现有测试，至少覆盖：

- PE machine、静态/延迟 imports、CRT 和 Windows SDK 依赖审计；
- x86 TSF 的 COM 加载、`ActivateEx`、重复激活/停用和 Pipe 握手；
- x86/x64 两种 TSF 同时连接同一 Core；
- protocol 编解码、长度限制、超时、断线和迟到结果；
- 双 registry view 的注册、profile/category、注销和文件归属；
- 安装、升级、回滚、卸载及只占用其中一个 DLL 的场景；
- x86 应用只加载 x86 DLL，x64 应用只加载 x64 DLL，错误位数加载失败时有明确诊断。

### 真实宿主验收

在 x64 Windows 上至少验证：

| 宿主 | 验收内容 |
| --- | --- |
| 一个普通 x64 文本控件 | 现有输入、候选、快捷键和断线恢复不回归 |
| 一个普通 x86 文本控件 | 激活、拼音预编辑、提交、候选选择、Ctrl+Space 和语言栏 |
| 一个 x86 应用的文本界面 | 启动后产生 `tsf-<PID>-YYYYMMDD.log`，按键进入 `key_result`/Core 请求 |
| x86/x64 应用启动的子进程（分别测试） | 确认实际接收键盘的进程架构、TSF 派发和输入策略 |
| 提权宿主 | Pipe 身份、ACL、Core 连接和拒绝原因 |

成功加载 DLL 或成功注册 profile 不等于真实输入支持。若宿主使用 Raw Input、禁用 TSF、没有文本服务 context、运行在安全输入场景或应用本身不接受 TSF，日志应明确显示未激活/被策略拒绝，而不是把问题归因于 x86 构建。

日志验收按 TSF PID、Core PID、时间和 session 关联。x86 宿主成功时应能看到 `pipe_connect`、`tsf_state`、`context_policy` 和按键对应的 `key_result`；只有 Core 日志而没有宿主 TSF 日志，仍表示 DLL 未加载或未激活。

## 分阶段实施

### 阶段 0：协议与代码审计

- 固定当前 x64 行为基线和真实应用回归日志；
- 完成协议字段、结构体布局、指针/句柄和依赖库审计；
- 确认所有共享状态只通过 Pipe 或用户配置传递；
- 不改变现有 x64 安装和注册行为。

### 阶段 1：独立 x86 TSF 构建

- 增加 x86 toolchain/build preset 和目标架构检查；
- 生成 `fcitx5-x86.dll`，运行免注册加载、Pipe 和静态审计；
- 参数化 TSF/probe/CTest，保留独立 x64 构建；
- 暂不修改生产注册和安装器。

### 阶段 2：双架构注册与真实宿主

- 实现匹配位数的注册/注销和 registry view 测试；
- 在隔离测试机安装双 DLL；
- 验证 x86 应用文本界面，收集 x86 TSF 日志；
- 验证 x86/x64 客户端同时运行以及提权/断线场景。

### 阶段 3：打包与发布

- 修改构建、部署、安装器、升级器、卸载器和运行库审计；
- 为 x86 DLL 建立独立文件清单、校验和、签名及所有权记录；
- CI 至少增加 x86 TSF build/test job，并在 x64 Windows runner 做双架构注册 smoke；
- 更新支持范围、故障诊断和安装文档后再发布。

## 风险与明确边界

- x86 与 x64 TSF 可能同时操作同一 Core 状态，必须依赖现有 client/context 所有权校验，不能引入进程内共享全局状态。
- 同一 CLSID 的双架构注册如果只更新一套 registry view，会出现 profile 可见但 DLL 静默加载失败；注册/注销必须按位数成对测试。
- x86 编译可能暴露现有的指针截断、对齐和 `long` 大小假设；这类问题必须修正协议或类型，不能用条件编译绕过。
- x86 应用界面能否输入与其启动的子进程能否输入是两个验收对象。x86 TSF 只解决 DLL 位数不匹配，不保证所有应用、Raw Input、反作弊或安全输入场景都支持 TSF。
- 真实系统注册、签名、UAC、提权宿主、DPI、现代应用派发和安装升级仍需要在隔离 Windows 环境中验证，免注册 probe 不能替代这些验收。
