# Windows UAC 提权应用输入修复方案

## 状态与目标

身份校验、提权 TSF 服务控制和握手诊断已接入代码。管理 Pipe 允许同一交互登录的普通权限 Core 接受经过验证的 UAC linked-token x86/x64 桌面客户端；真实提权应用输入按后文矩阵验收。不同用户、session、独立登录和受限输入入口继续隔离。

原连接问题位于管理 Pipe 的登录身份校验。CPU-Z Validation 等普通文本框用于验证真实原生 Win32 提权输入，测试时核对实际接收键盘的进程身份和 DLL 架构。

## 问题来源与当前调用链

当前调用链为：

```text
TSF ActivateEx
  -> PipeClient::connect / Hello
  -> WindowsPipeServer::runClient
  -> authorizedPipeClient
  -> pipeClientIdentity / tokenEnvironment
  -> pipeLoginDecision(hostUacIdentity, caller, peerSession, inputOnly)
     -> ExactLogin / SandboxAnonymous / UacLinkedLogin / Denied
```

`win32/ipc/clientenvironment.h` 的 `sameLogin()` 要求 token 查询成功、用户 SID、Windows sessionId 和 `TOKEN_STATISTICS::AuthenticationId` 全部相同。`win32/ipc/pipeidentity.h` 的 `authorizedPipeClient()` 读取 Pipe 实际对端 token 及内核返回的客户端 session，再交给 `pipeLoginDecision()` 分类判定。

原实现已在同一用户/session 上观察到：普通权限 Core 与提权 CPU-Z、Leigod、Steam 窗口进程的 `AuthenticationId` 不同，但它们的 UAC token 互为 linked token。旧版 `ipc_probe.exe --diagnose` 在管理员权限下于 `Hello read` 失败，错误为 `ERROR_PIPE_NOT_CONNECTED`（233）；通过 Explorer 以普通权限运行时成功连接同一 Core。当前实现增加 linked-token 判定处理此类身份关系；各应用的预编辑、提交和候选显示仍需逐项验证。

服务端拒绝 Hello 时不会进入 `process()`，因此没有对应的 `core_request`。TSF 详细日志开关来自 Core 回复，首次握手失败时可能尚未启用 logger。没有 `tsf-<PID>` 日志不能推导出 DLL 未加载、TIP 未激活或宿主没有 `ITfContext`。Core 日志的 `client` 是连接序号，不是客户端 PID，不能仅按时间关联请求。

## 修复边界

- 保留 `sameLogin()` 的严格语义，新增显式的桌面管理入口身份判断，不让所有调用者隐式接受两种登录身份。
- 保留现有 Pipe 名称、DACL、完整性标签、SQOS、超时、取消和上下文 owner 校验。不新增 Everyone/匿名 ACE，不降低管理 Pipe 的完整性标签。
- 不在 Hello 或其他 payload 中接收客户端声称的 SID、登录 LUID、提权状态或 linked token 句柄。本方案不改变协议线格式或版本；版本以 `win32/ipc/protocol.h` 为准。
- `-input` 入口和 AppContainer 继续使用严格登录匹配及既有 `AuthenticateServer`，不适用 UAC 例外。`verifiedServerHandle()` 的 PID/session、进程对象 owner 和固定安装路径校验保留。
- Sandboxie 匿名 token 的既有管理入口例外独立保留，不因 UAC 扩大到其他 SID 或受限输入入口。Sandboxie 的 HostOnly 和服务端归属检查继续执行。
- Core 保持普通权限，通过 token 身份比较处理跨权限连接，沿用现有 TSF category 和编辑/UI 路径。

## 可信 UAC 关系

### 从本进程 token 建立关系

`UacLoginIdentity` 值类型和 `uacLoginIdentity()` 查询函数包含本 token 的 SID、session、AuthenticationId、elevation type，以及查询成功后 linked token 的对应元数据和查询状态；`validUacLink()` 检查关联有效性。不持有长期 token 句柄，不进入 IPC payload。

由 Core 从自己的进程 primary token 构造此快照，必须在 `ImpersonateNamedPipeClient()` 之前完成。客户端需要执行对称校验时，也从自己的进程 primary token 构造，不从线程可能正在模拟的 token 推导本机身份。

1. 使用 `OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, ...)` 查询基础身份及 `TokenElevationType`。基础身份查询失败仍按既有规则拒绝连接。
2. `TokenElevationTypeDefault` 没有 linked token，保留严格比较，不把“管理员组成员”或完整性等级作为关联证明。
3. 对 `TokenElevationTypeLimited` / `TokenElevationTypeFull`，调用 `GetTokenInformation(TokenLinkedToken)`，仅查询返回 token 的身份元数据。
4. 只有本 token 与 linked token 均可查询、SID/session 相同、均非 AppContainer、均非低完整性、elevation type 为互补的 Limited/Full 时，才发布有效关联。不同 AuthenticationId 必须来自这个 Windows 返回的 linked token。
5. 用 RAII 关闭进程 token 和 linked token；不递归枚举 linked token，不复制/模拟 linked token，不使用它启动进程。不要求 `TOKEN_DUPLICATE`、`TOKEN_ASSIGN_PRIMARY`、SeDebugPrivilege 或 SeTcbPrivilege。

linked token 查询失败只禁用新增兼容分支，不把原来可严格匹配的普通客户端一并拒绝；不得降级为“同 SID/session 即允许”。应先用普通权限 Core 原型验证上述元数据在目标系统可查询，不能通过启用特权解决查询失败。

服务端 `pipeClientIdentity()` 继续以 identification SQOS 下实际模拟得到的 token 为准，不要求从此 token 查询 `TokenLinkedToken`，也不按客户端 PID 打开其高权限进程 token。PID 只用于诊断关联，不能替代实际 Pipe token；这样不会把进程 primary token 与连接时的线程身份混淆。

### 新增判断

`PipeLoginDecision` 返回固定枚举 `ExactLogin`、`UacLinkedLogin`、`SandboxAnonymous`、`Denied`，并提供有界的 `PipeLoginReason` 和查询错误码。`pipeLoginDecision()` 提供纯判定；接收 `UacLoginIdentity` 的 `authorizedPipeClient()` 重载查询实际对端并调用纯判定。原 `ClientEnvironment` 重载不启用 UAC 例外。

`authorizedPipeClient()` 的管理入口按以下顺序判断：

1. 保留基础检查：Core/对端 token 可查询、`GetNamedPipeClientSessionId()` 成功、内核 peer session 等于 Core session；非匿名对端的 token session 也必须匹配。
2. `sameLogin(host, caller)` 成功时沿用当前规则，返回 `ExactLogin`。
3. 严格比较失败时，先检查既有 Sandboxie 管理入口匿名兼容；`inputOnly` 或 AppContainer 不适用该例外。
4. 其余连接仅在非 `inputOnly`、非 AppContainer 的管理入口尝试 `UacLinkedLogin`。本机 Core 必须是已确认的普通桌面环境；本机与对端均非低完整性，且对端 elevation type 查询成功。
5. 对端 SID/session、AuthenticationId 和 elevation type 必须精确匹配本机已验证的 linked token，否则拒绝。

同一 AuthenticationId 下的既有行为不在此次变更中重定义。新增分支不表示能够区分同用户的所有任意代码、token 派生或沙箱实现；既有 DACL、隔离策略和消息权限仍是必要边界。

| 对端关系 | 管理入口 | 受限输入入口 |
| --- | --- | --- |
| 同 SID/session/AuthenticationId | 沿用严格匹配 | 沿用严格匹配和受限认证 |
| 同 SID/session，精确匹配本机 UAC linked token，满足桌面限制 | 新增允许 | 拒绝 UAC 例外 |
| 同 SID/session，不同且无已验证关联的 AuthenticationId | 拒绝 | 拒绝 |
| 不同用户、不同 token session 或不同内核 peer session | 拒绝 | 拒绝 |
| linked 查询失败或 elevation type 不互补 | 只允许原严格匹配 | 只允许原严格匹配 |
| AppContainer/低完整性连接要求跨 UAC LUID 匹配 | 拒绝新增分支 | 拒绝新增分支 |
| token 未知、基础查询失败、解除模拟失败 | 拒绝；解除模拟失败不得复用线程 | 同左 |
| 既有 Sandboxie 匿名连接 | 仅按原条件允许 | 拒绝 |

### 双向检查与生命周期

`pipeServerIdentity()` 当前主要由 AppContainer probe 调用，不应直接替换其中的 `sameLogin()`。若桌面客户端启用该辅助检查，必须显式传入管理入口上下文，只在已知普通桌面策略下使用相同的本机 linked-token 比较，并保留服务端非 AppContainer、非低完整性的限制；默认调用仍严格匹配。不要把普通客户端必须打开高权限服务端 token 作为修复前提，查询失败不绕过验证。

生产 AppContainer 的服务端验证实际走 `verifiedServerHandle()`，保持现有流程。本次不为普通桌面 PipeClient 额外引入一个跨权限 `OpenProcessToken()` 依赖。

`WindowsPipeServer::runClient()` 在每个连接开始时从 Core primary token 构造本机 UAC 快照，在模拟对端之前完成。每次连接重新查询实际 Pipe token，断线后销毁旧连接的 context 和身份结果。

## 提权 TSF 的服务控制

仅放行握手仍有服务管理风险：`serviceIdentity()` 当前含 AuthenticationId 和登录时间，提权 TSF 按自己的身份调用 `ServiceController` 会看到不同的 mutex/手动停止状态；而 Pipe 名只按 SID/session 区分，可能造成第二份 Core 启动及端点竞争。

此次不重命名服务 mutex/state，也不把两个 token 的持久状态自动合并。增加独立于 Sandboxie/AppContainer 分类的“通过宿主执行服务操作”判断：已确认的 UAC Full 桌面 TSF 使用该路径；不能通过 linked 查询失败退回本地提权启动。桌面 elevation 查询失败也保守使用宿主路径。`serviceclient.h` 的 `routeServiceOperation()` 在执行本地操作前选择路径，`remoteServiceOperation()` 负责连接和单次远端请求；连接失败不会回退本地，也不重放命令。它只约束 TSF/宿主客户端的服务操作，不等同于修改 Core 的环境准入策略。

- 已有 Core：`kEnsureServiceMessage` 只检查 Pipe；设置、用户数据目录、重启和停止请求通过既有 `OpenSettings` / host commands 交给已认证 Core，在 Core 自己的服务身份和权限下执行。
- Pipe 缺失或握手失败：有界重连并给出诊断，不从管理员宿主创建 Core、Settings 或控制子进程。首期要求用户从普通权限会话启动/重启 Core，不新增自动降权启动 broker。
- 用户手动停止后，提权应用获得焦点不得创建另一份 Core 或清除停止状态。只有明确的重启操作可按既有语义解除停止；Core 已停止且无法接收请求时，由普通权限的既有控制入口完成。
- 当前没有自动降权启动 broker；无 split token、其他管理员凭据和独立登录按各自身份规则处理。

复用 `servicecontrol.cpp` 的远端命令实现，但为 UAC 输出独立原因，不沿用“检查 Sandboxie OpenPipePath”的提示。连接已到 Hello 却失败，不能当作 Core 缺失、停止用户服务或按进程名清理应用。

## 诊断设计

详细日志仍遵守 [开发日志设计](windows-developer-logging-design.md)：只记录诊断元数据，不记录输入文本、payload、窗口标题、用户路径、SID 或原始登录 LUID。

- Core 在握手授权阶段记录 `pipe_identity`：连接序号、通过内核 API 查询的 peer PID（可用时）、入口类型、匹配枚举、固定拒绝原因、elevation type、错误码。PID 查询失败不得改变 token 授权结果。
- 客户端记录连接 stage、error、peer PID（已知时）和 elevation type。错误 233 本身并不唯一表示 UAC 问题；只有服务端拒绝原因和身份对照才能确认。
- `ipc_probe --diagnose` 的显式终端输出可沿用现有 SID/session/AuthenticationId 字段，并增加 elevation type、linked 查询状态和同服务端登录关系；不得为诊断授予更多权限。这些身份值不复制到持久日志。
- TSF 尚未收到日志开关时，仅通过限频的 `OutputDebugStringW` 输出固定连接失败阶段和错误码，不擅自启用详细磁盘日志，不等待 Core 回复才允许输出失败元数据。
- 不在模拟对端期间写日志、访问用户路径或操作引擎；先复制有界元数据、解除模拟，再记录。解除模拟失败沿用 fail-fast 处理。

## 实施文件

| 文件 | 当前职责 |
| --- | --- |
| `win32/ipc/clientenvironment.h` | 提供可选 UAC 元数据查询；基础 `tokenKnown` 不依赖 linked 查询，`sameLogin` 保持严格 |
| `win32/ipc/pipeidentity.h` | 新增本机可信 linked 关系和显式管理入口判定；保留 AppContainer/Sandboxie 分支边界 |
| `src/windowsfrontend.cpp` | 在模拟前准备 Core 本机身份，接入管理入口判定及握手阶段诊断 |
| `win32/tsf/pipeclient.cpp` | 补充首次握手失败诊断，保留不消费输入及不重放超时请求 |
| `win32/tsf/servicecontrol.cpp` | 提权 TSF 使用宿主服务命令，禁止自动启动另一权限身份的 Core/Settings |
| `win32/ipc/serviceclient.h` | 服务操作路由及可注入连接的远端命令执行，不依赖 linked 查询成功 |
| `win32/tests/ipc_probe.cpp` | 增加身份关系诊断元数据，不自动启动服务或修改配置 |
| `win32/tests/test_isolationpolicy.cpp`、`test_pipeidentity.cpp` | 覆盖严格匹配不变、UAC 放行和所有拒绝分支 |
| `win32/tests/test_hostcommands.cpp`、`test_service.cpp` | 验证提权客户端操作使用宿主身份，不绕过手动停止、不产生本地启动副作用 |
| `win32/tests/test_uacidentity.cpp`、`test_uactoken.cpp` | 纯 UAC 判定、真实 primary/linked 元数据、句柄生命周期及模拟状态下不回退线程身份 |
| `win32/tests/test_serviceclient.cpp` | 提权/未知 elevation 路由、宿主缺失/停止/握手失败、不重放超时请求及无本地回退 |
| `win32/tests/uac_pipe_probe.cpp` | 独立用户 DACL 测试 Pipe；默认同权限自测，显式参数供独立启动的普通/提权进程对照 |

token 查询辅助代码由各工程分别编译/包含，保持现有 C++ ABI 和运行库边界。

## 测试与验收

### 自动化测试

1. 纯判定测试构造可信本机 UAC 快照和对端事实，覆盖上表，包括只伪造相同 SID/session、任意第三个 LUID、非互补 elevation、linked 查询失败、token session 与内核 session 不同、未知 token、低完整性和 AppContainer。严格 `sameLogin()` 对不同 LUID 仍必须失败。
2. 真实 token 提取测试只查询当前 token；存在 linked token 时检查 SID/session、类型和句柄关闭。没有 split token 的 CI 将 UAC 特有测试报告为 skip。
3. 使用唯一测试 Pipe，启动同一交互登录的一对普通/提权 probe，验证真实 identification SQOS 的 Pipe 模拟 token，而非只比较两个进程 token。两个方向分别报告；面向用户的最低要求是普通 Core 接受提权客户端。
4. 真实 Pipe 测试保留不同用户/session/独立登录拒绝、模拟身份与 primary token 不一致、Sandboxie 匿名例外及解除模拟失败路径。查询/授权失败后不得泄漏句柄或影响下一连接。
5. 对 x86/x64 独立构建运行受影响 CTest，继续执行跨架构 Pipe 并发、context owner 和断线测试。测试版 Core/probe 使用隔离 Pipe、服务对象及配置目录，不抢占当前生产端点。
6. 通过模拟 `ServiceController`/进程启动入口验证提权 TSF 只调用宿主命令；覆盖 Core 缺失、已停止、握手失败、命令超时、重启/停止和 Settings 打开，无按进程名终止行为。

TokenLinkedToken 的 identification 句柄只用于查询。不能为了生成测试客户端而启用 SeTcbPrivilege/SeDebugPrivilege，或把仅可查询的 linked token 当成可用于 `ImpersonateLoggedOnUser` / `CreateProcessWithTokenW` 的 primary token；真实提权/普通客户端应从各自交互会话启动。

`test_uactoken` 在没有 split token 的环境中以 CTest skip 报告 UAC 特有验证。默认 `uac_pipe_probe` 验证同权限真实 Pipe 的 `ExactLogin`。跨权限对照由普通权限终端先启动服务端，再在同一交互登录的提权终端运行客户端；两边使用相同且唯一的 suffix，例如：

```powershell
# 普通权限终端；服务端最多等待 30 秒，不启动生产 Core。
./build/tsf-x64/tests/uac_pipe_probe.exe --server uac-check-unique

# 同一交互登录的提权终端；可分别使用 x64 和 x86 probe。
./build/tsf-x86/tests/uac_pipe_probe.exe --client uac-check-unique
```

路径按实际独立构建目录调整。两端均应输出 `match=2`（`UacLinkedLogin`）；`match=1` 是 `ExactLogin`，`match=0` 是拒绝，不能把它们记作 linked 分支通过。反向测试需独立运行并报告，不以普通客户端能够打开高完整性 Pipe 为最低要求。probe 不自行提权或降权，不修改注册、配置和用户服务；不同用户可能在打开 Pipe 时已由 DACL 拒绝。默认 CTest 不启动跨权限进程。

### 真实系统与应用

| 验收对象 | 必须观察的结果 |
| --- | --- |
| 原生普通 x86/x64 编辑框 | 注册后的正确 DLL 派发、真实引擎预编辑、提交、候选选择和模式切换 |
| CPU-Z x86/x64 Validation 的 Name 等普通字段 | 普通权限 Core 下的提权输入；不提交验证表单，不把只读主页作为输入测试 |
| Leigod、Steam 的实际窗口/文本框 | 按实际输入进程 PID/架构核对 token、匹配分支和完整输入链路，不能以启动器位数代替 |
| SamsungMagician | 先补验实际窗口 token，再独立验证普通文本输入，不能只凭程序类型归因 |
| 已手动停止、Core 缺失、Core 重启后重连 | 提权应用不启动第二份服务；显式重启和重连后恢复输入，旧预编辑不跨焦点保留 |
| 不同用户凭据提权、同用户独立登录、其他 session | 不使用 UAC 例外跨越身份边界；失败透传且诊断明确 |
| Sandboxie/AppContainer | 原有连接策略、受限消息权限和候选行为不回归；不把提权兼容作为受限环境绕过路径 |

验收记录 OS、应用版本、真实输入宿主架构/elevation、DLL build、Core PID 和授权分支；按握手、context、按键、编辑、候选显示逐层确认。Hello 成功、免注册测试、候选 probe 和真实应用中文提交分别报告；涉及真实注册、安装或用户服务控制的步骤须按仓库规则取得明确授权。

## 构建、部署与回退

x86/x64 独立构建及命令见 [x86 TSF](windows-x86-tsf-design.md#构建与部署)。优先运行 `test_uacidentity`、`test_uactoken`、`test_pipeidentity`、`test_serviceclient`、`test_service` 和 `uac_pipe_probe`，再完成跨权限 Pipe 对照及真实应用矩阵。部署同一套 Core、TSF 和 Settings，重开已加载旧 DLL 的目标应用；该身份修复沿用现有 category/profile。

回退使用原有完整部署版本并重开受影响应用，不自动迁移/删除 `.state` 或合并历史服务身份。不要把同时运行的两份不同权限 Core 按进程名结束；确认具体 PID/安装路径后，仍按明确授权执行既有服务控制。

## API 依据

- [TOKEN_ELEVATION_TYPE](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ne-winnt-token_elevation_type)：区分 Default、Full、Limited。
- [TOKEN_LINKED_TOKEN](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-token_linked_token)：查询 Windows 返回的 linked token，使用完毕关闭句柄。
- [GetTokenInformation](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-gettokeninformation)：按信息类别查询 token，处理查询权限和失败返回。
