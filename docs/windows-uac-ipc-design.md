# Windows UAC 提权应用输入方案

## 状态与目标

UAC linked-token 身份校验、提权 TSF 宿主服务路由和握手诊断均已接入。桌面管理 Pipe 允许同一交互登录的普通权限 Core 接受经过验证的 UAC linked-token x86/x64 桌面客户端；不同用户、不同 session、独立登录和受限输入入口继续严格隔离。真实提权应用输入按文末矩阵验收。

## 问题背景与调用链

在启用 UAC split-token 的交互登录中，普通权限 Core（`TokenElevationTypeLimited`）与提权桌面应用（`TokenElevationTypeFull`，如以管理员运行的 CPU-Z、Leigod、Steam 等）属于同一用户和 session，但 `TOKEN_STATISTICS::AuthenticationId` 不同，两者通过 Windows UAC linked token 关联。

当前连接授权调用链：

```text
TSF ActivateEx
  -> PipeClient::connect / Hello
  -> WindowsPipeServer::runClient
  -> authorizedPipeClient
  -> pipeClientIdentity / tokenEnvironment
  -> pipeLoginDecision(hostUacIdentity, caller, peerSession, inputOnly)
     -> ExactLogin / SandboxAnonymous / UacLinkedLogin / Denied
```

- `win32/ipc/clientenvironment.h` 的 `sameLogin()` 始终严格校验 token 可查询、用户 SID、session 与 `AuthenticationId` 全部一致。
- `win32/ipc/pipeidentity.h` 的 `authorizedPipeClient()` 结合内核返回的客户端 session 与实际模拟对端 token，经 `pipeLoginDecision()` 判定授权分支。

## 安全边界

- `sameLogin()` 保持严格语义；仅桌面管理入口显式启用 UAC linked-token 分支。
- 保持原有 Pipe 名称、用户专用 DACL、完整性标签、SQOS、超时/取消与 context owner 校验，不添加 Everyone/匿名 ACE，不降低管理 Pipe 完整性。
- 不信任客户端在 Hello 或 payload 中上报的 SID、LUID、提权状态或句柄；UAC 身份校验完全基于本地与内核模拟 token，无需修改协议线格式。
- `-input` 受限入口和 AppContainer 继续使用严格登录匹配及 `AuthenticateServer` 句柄校验，不适用 UAC 例外。
- Sandboxie 安全模式匿名 token 例外独立保留于管理入口，与 UAC 互不扩大。
- Core 保持普通权限运行，沿用现有 TSF category 与编辑/UI 路径。既有 DACL、隔离策略和消息权限仍是管理入口的必要边界。

## 可信 UAC 关系与授权判定

### 构造本机快照

`UacLoginIdentity` 与 `uacLoginIdentity()` 由 Core 在每次连接 `ImpersonateNamedPipeClient()` 之前从自身进程 primary token 构造（对称校验客户端同理，不从模拟线程 token 推导），不持久持有 token 句柄：

1. 通过 `OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, ...)` 获取基础身份与 `TokenElevationType`。
2. `TokenElevationTypeDefault` 无 linked token，仅走严格比较。
3. `TokenElevationTypeLimited` / `TokenElevationTypeFull` 查询 `GetTokenInformation(TokenLinkedToken)` 并只读取返回 token 的身份元数据（RAII 自动关闭句柄，不复制/模拟 linked token，不要求 `SeDebugPrivilege` 或 `SeTcbPrivilege`）。
4. 仅当本机 token 与 linked token 均可查询、SID/session 相同、均非 AppContainer、均非低完整性且 elevation type 互补（Limited ↔ Full）时，`validUacLink()` 才发布有效关联。linked 查询失败只关闭兼容分支，不影响严格匹配的普通客户端。

服务端 `pipeClientIdentity()` 始终以 identification SQOS 下实际模拟的 Pipe token 为准，不按客户端 PID 跨权限打开高权限进程 token。

### 判定顺序

`PipeLoginDecision` 返回 `ExactLogin`、`UacLinkedLogin`、`SandboxAnonymous` 或 `Denied`，附带固定 `PipeLoginReason` 与错误码。管理入口判定顺序：

1. 基础检查：本机/对端 token 可查询、`GetNamedPipeClientSessionId()` 成功、内核 peer session 与非匿名对端 token session 均匹配 Core session。
2. `sameLogin(host, caller)` 成立 → `ExactLogin`。
3. 符合 Sandboxie 安全模式管理入口匿名条件 → `SandboxAnonymous`。
4. 非 `inputOnly`、非 AppContainer，本机为普通桌面环境，本机与对端均非低完整性且对端 elevation type 查询成功，且对端 SID/session、`AuthenticationId` 和 elevation type 精确匹配本机已验证的 linked token → `UacLinkedLogin`。
5. 其余一律 `Denied`（解除模拟失败不得复用线程）。

| 对端关系 | 管理入口 | `-input` 受限入口 |
| --- | --- | --- |
| 同 SID / session / `AuthenticationId` | `ExactLogin` | `ExactLogin` + 受限认证 |
| 同 SID / session，精确匹配本机 UAC linked token（桌面非低完整性） | `UacLinkedLogin` | 拒绝 |
| 同 SID / session，无已验证关联的其他 `AuthenticationId` | 拒绝 | 拒绝 |
| 不同用户、不同 session 或查询/解除模拟失败 | 拒绝 | 拒绝 |
| linked 查询失败或 elevation 不互补 | 仅允许 `ExactLogin` | 仅允许 `ExactLogin` |
| AppContainer / 低完整性跨 UAC LUID | 拒绝 | 拒绝 |
| Sandboxie 安全模式匿名连接 | 仅按原条件允许 | 拒绝 |

`pipeServerIdentity()` 默认保持严格匹配；生产 AppContainer 继续走 `verifiedServerHandle()`，普通桌面 `PipeClient` 无需额外跨权限 `OpenProcessToken()`。

## 提权 TSF 的服务控制

因 `serviceIdentity()` 包含 `AuthenticationId` 与登录时间，若提权 TSF 直接在本地调用 `ServiceController`，会看到与普通桌面 Core 不同的 mutex 与手动停止状态，导致重复拉起提权 Core 或绕过手动停止。

因此，`win32/ipc/serviceclient.h` 的 `routeServiceOperation()` 对已确认的 UAC Full 桌面 TSF（以及 elevation 查询失败时的保守分支）统一采用宿主路由（`remoteServiceOperation()`）：

- **Core 已运行**：`kEnsureServiceMessage` 仅探测 Pipe；设置、用户数据目录、重启与停止请求通过 `OpenSettings` 和宿主命令交由已认证的普通权限 Core 在自身身份下执行。
- **Core 缺失、已手动停止或握手失败**：执行有界重连并记录独立 UAC 诊断原因（不回退到本地提权启动，也不重放超时命令）。需从普通桌面会话启动或重启 Core（当前无自动降权 broker）。

## 诊断设计

遵循 [开发日志设计](windows-developer-logging-design.md) 的隐私边界，持久日志不记录输入文本、payload、窗口标题、用户路径、SID 或原始登录 LUID：

- **Core `pipe_identity` 事件**：记录连接序号、内核 peer PID（可用时）、入口类型、匹配枚举、固定拒绝原因、elevation type 与错误码（在复制元数据并解除模拟后再写日志）。
- **TSF 早期诊断**：尚未收到 Core 详细日志开关前的握手失败通过限频 `OutputDebugStringW` 输出阶段与错误码；开启详细日志后记录连接 stage、error、peer PID 与 elevation type。
- **`ipc_probe --diagnose`**：显式终端输出 SID/session、`AuthenticationId`、elevation type、linked 查询状态及与服务端的登录匹配关系，不写入持久日志。注意错误 `233` 需结合服务端 `pipe_identity` 拒绝原因判断。

## 实施文件

| 文件 | 职责 |
| --- | --- |
| `win32/ipc/clientenvironment.h` | 可选 UAC 元数据查询；`sameLogin()` 保持严格 |
| `win32/ipc/pipeidentity.h` | 本机可信 linked 关系与显式管理入口判定 |
| `src/windowsfrontend.cpp` | 模拟前构造本机 UAC 快照、接入判定与 `pipe_identity` 诊断 |
| `win32/tsf/pipeclient.cpp`、`servicecontrol.cpp` | 首次握手诊断与提权 TSF 宿主服务命令路由 |
| `win32/ipc/serviceclient.h` | 服务操作路由与远端命令单次执行（无本地提权回退） |
| `win32/tests/ipc_probe.cpp` | 终端身份关系诊断 |
| `win32/tests/test_uacidentity.cpp`、`test_uactoken.cpp`、`test_serviceclient.cpp` | 纯 UAC 判定、真实 token 查询、提权/未知 elevation 路由 |
| `win32/tests/test_isolationpolicy.cpp`、`test_pipeidentity.cpp`、`test_hostcommands.cpp`、`test_service.cpp` | 权限边界、匿名例外与宿主命令回归 |
| `win32/tests/uac_pipe_probe.cpp` | 真实 Pipe 同权/跨权对照探针 |

## 验证与验收

### 自动化与跨权限探针

- 默认 CTest：`test_uacidentity`、`test_uactoken`（无 split token 环境返回 CTest skip）、`test_pipeidentity`、`test_serviceclient`、`test_service`、`test_hostcommands`、`uac_pipe_probe`（默认同权限自测 `ExactLogin`）。
- 跨权限真实 Pipe 对照（同一交互登录下分别启动普通与提权终端，使用相同唯一 suffix）：

```powershell
# 普通权限终端（最多等待 30 秒，不启动生产 Core）
./build/tsf-x64/tests/uac_pipe_probe.exe --server uac-check-unique

# 同一交互登录的提权终端（可分别测试 x64 与 x86）
./build/tsf-x86/tests/uac_pipe_probe.exe --client uac-check-unique
```

两端均输出 `match=2`（`UacLinkedLogin`；`match=1` 为 `ExactLogin`，`match=0` 为拒绝）。

### 真实系统与应用验收矩阵

| 验收对象 | 验收要点 |
| --- | --- |
| 原生普通 x86/x64 文本框 | 正确 DLL 派发、预编辑/提交、候选选择与模式切换 |
| CPU-Z x86/x64 Validation 普通字段 | 普通权限 Core 下的提权输入 |
| Leigod、Steam、SamsungMagician 等实际窗口 | 核对实际接收键盘进程的 PID、架构、token 匹配分支与完整输入链路 |
| Core 缺失、手动停止、重启与重连 | 提权应用不启动第二份服务；普通桌面重启后自动恢复输入 |
| 不同用户凭据、同用户独立登录、其他 session | 严格拒绝跨边界连接，诊断明确 |
| Sandboxie / AppContainer | 既有隔离与消息限制无回归 |

部署与回退：同步部署同版本 Core、x64/x86 TSF 与 Settings 并重开加载旧 DLL 的应用（沿用现有注册元数据，无需重新注册）；详见 [x86 TSF 构建与部署](windows-x86-tsf-design.md#构建与部署)。

## API 参考

- [TOKEN_ELEVATION_TYPE](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ne-winnt-token_elevation_type)
- [TOKEN_LINKED_TOKEN](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-token_linked_token)
- [GetTokenInformation](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-gettokeninformation)
