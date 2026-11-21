# Sandboxie 宿主 Core 共享方案

## 目标与架构

Sandboxie 内的 TSF 只连接当前用户/session 的宿主 Core：输入法列表、引擎选项、词库和学习记录统一由宿主 Core 管理，Settings 由宿主 Core 在沙箱外启动，避免在隔离视图中启动第二份 Core/Settings 或拆分配置。

本方案面向 Sandboxie（无需新增 TSF category）；AppContainer 见 [现代应用支持](windows-modern-apps-design.md)。实现借鉴了 Weasel 的 Sandboxie 放行模板（[Templates.ini](https://github.com/sandboxie-plus/Sandboxie/blob/master/Sandboxie/install/Templates.ini)、[OpenPipePath](https://sandboxie-plus.github.io/sandboxie-docs/Content/OpenPipePath.html)、[SBIEDLLAPI](https://sandboxie-plus.github.io/sandboxie-docs/Content/SBIEDLLAPI.html)）。

## 进程策略

- **环境检测**：共享辅助模块动态查询已加载的 `SbieDll` API，区分宿主、沙箱和未知状态；模块存在但查询异常时禁止本地服务启动。
- **HostOnly 策略**：沙箱 TSF 不创建 `ServiceController`，不读写本地服务状态，不启动 Core/Settings/控制子进程。Core 与 Settings 启动前拒绝沙箱或未知环境。
- **连接与服务端归属**：沿用现有 SID/session 管理 Pipe（`\\.\pipe\fcitx5-windows-v2-<SID>-<session>`，协议 **v15**）和当前用户 ACL。沙箱客户端检查 Pipe 服务端 PID 并通过 Sandboxie API 排除沙箱内服务端，无法确认时拒绝连接。失败不消费按键、取消旧预编辑，超时请求不重放。
- **安全模式匿名 token 兼容**：Sandboxie 启用 `UseSecurityMode=y` 时，宿主通过 Pipe 模拟可能看到匿名 SID（`S-1-5-7`）与匿名登录 LUID。原管理 Pipe 在连接已通过当前用户专用 DACL、token 查询成功且非 AppContainer 时，允许该匿名 token 通过内核 `GetNamedPipeClientSessionId` 核对客户端 session（不增加匿名 ACE，不放宽 ACL）。该例外仅用于管理 Pipe，AppContainer `-input` 入口仍严格要求 `sameLogin`。UAC linked-token 兼容见 [UAC 方案](windows-uac-ipc-design.md)。

## 操作路由

| 操作 | 沙箱内实现 |
| --- | --- |
| 输入、输入法轮换、状态菜单 | 现有宿主 Core IPC |
| 设置页 | `OpenSettings`，由宿主 Core 在沙箱外启动 Settings |
| UI 语言 | `GetGlobalSettings`，使用宿主解析后的语言（不读沙箱配置） |
| 用户数据目录 | `OpenUserDataFolder`，由宿主 Core 打开固定目录 |
| 重启/关闭服务 | `RestartServices` / `StopServices`，由宿主 Core 启动固定路径控制进程 |
| 候选窗、关于 | 应用进程内 TSF 按 UIElement 许可渲染 |

宿主命令仅允许空 payload 与 `contextId = 0`。服务控制先回复请求已入队，再投递到 Core 主线程执行；跨进程不提供 exactly-once 保证。宿主 Core 未运行或已手动停止时，需在沙箱外启动/重启 Core（安装器可选登录启动项可预先拉起宿主 Core，但不解除手动停止状态）。

权限边界：为沙箱放行此 Pipe 后，沙箱内程序也可通过 IPC 修改宿主输入法设置及调用服务控制命令（受当前用户 ACL、session 范围和请求白名单约束，不开放任意命令或文件路径），请仅对需要共享输入法的沙箱启用。

## 用户配置指南

### 1. 准备宿主 Core 与沙箱

1. 先在沙箱外确认 Fcitx5 已安装、已注册，且 Core、TSF DLL 与 Settings 为同一套构建（协议 v15），并能在普通应用正常输入中文。
2. 记下目标沙箱在 `Sandboxie.ini` 中的节名（例如 `[DefaultBox]` 对应 `DefaultBox`，多个沙箱需分别配置）。
3. 在沙箱外以当前用户启动宿主 Core（不要用 Sandboxie 运行或强制重定向它）：

   ```powershell
   Start-Process -FilePath 'C:\Program Files\Fcitx5\bin\Fcitx5.exe'
   ```

   若此前手动关闭过服务，先在沙箱外选择**重启服务**或运行 `Fcitx5.exe --restart-services`。

### 2. 生成并写入 Pipe 放行规则

在仓库根目录、沙箱外 PowerShell 中运行：

```powershell
# 仅打印当前用户的通配 session 规则（推荐日常使用）
.\scripts\sandboxie-template.ps1 -BoxName DefaultBox

# 仅打印精确到当前 session 的规则（排查用；重新登录后需重新生成）
.\scripts\sandboxie-template.ps1 -BoxName DefaultBox -SessionOnly
```

输出形如：

```ini
[DefaultBox]
OpenPipePath=\Device\NamedPipe\fcitx5-windows-v2-S-1-5-21-111111111-222222222-333333333-1001-*
```

无脚本时可运行 `whoami /user` 获取当前用户 SID，手动填入 `OpenPipePath=\Device\NamedPipe\fcitx5-windows-v2-<当前用户SID>-*`（单反斜杠，不加引号，无需开放用户配置目录、`OpenIpcPath=*` 或 Windows App Runtime 目录）。

将规则写入 `Sandboxie.ini`（二选一，修改前先备份）：

- **方法 A（手动编辑）**：在 Sandboxie 管理器中打开 `Sandboxie.ini` 编辑入口，将 `OpenPipePath=...` 添加到目标沙箱节（如 `[DefaultBox]`）内，保存并重新加载配置（参见 [Sandboxie 配置菜单文档](https://sandboxie-plus.github.io/sandboxie-docs/Content/ConfigureMenu.html)）。
- **方法 B（脚本写入）**：先 `-WhatIf` 预览，再调用 `SbieIni.exe` 追加（要求目标沙箱已启用，已有相同规则时不重复追加，支持 1–32 位字母/数字/下划线节名）：

  ```powershell
  .\scripts\sandboxie-template.ps1 -BoxName DefaultBox -Apply -WhatIf
  .\scripts\sandboxie-template.ps1 -BoxName DefaultBox -Apply
  # 若未自动找到 SbieIni.exe，可显式指定路径：
  .\scripts\sandboxie-template.ps1 -BoxName DefaultBox -Apply -SbieIniPath 'D:\Apps\Sandboxie-Plus\SbieIni.exe'
  ```

### 3. 重启沙箱应用并验证

彻底退出受影响的沙箱应用及其驻留后台进程，再从目标沙箱重新启动：

1. 在沙箱应用文本框切换 Fcitx5 与拼音，输入 `nihao` 验证预编辑、候选与提交。
2. 通过菜单打开设置，确认 Settings 在沙箱外运行且列表/配置与沙箱外一致。

### 4. 撤销规则

从目标沙箱配置节中删除该 `OpenPipePath=...` 行（含可选的 session 专用行），保存并重新加载配置，然后彻底重启沙箱应用。

## 诊断与验证

| 现象 | 排查要点 |
| --- | --- |
| 沙箱外也无法输入 | 先确认宿主 Core、TSF 注册与引擎数据正常 |
| 沙箱外正常、沙箱内无法输入 | 检查应用是否位于目标沙箱、规则节名与 SID 是否匹配、应用及后台进程是否已彻底重启 |
| 脚本提示沙箱不存在或未启用 | 确认 `-BoxName` 为配置节名且该节包含 `Enabled=y` |
| 连接提示协议不匹配 | 统一部署同版本 Core/TSF/Settings（协议 v15，Pipe 名保持历史 `v2` 前缀）并重启应用 |
| 沙箱内出现 Core 或 Settings | 清理残留旧版沙箱进程或检查强制沙箱规则 |

工具与自动化回归：

- **`ipc_probe.exe --diagnose`**：在沙箱内外分别运行，对比 Pipe 名称、连接阶段、错误码、服务端 PID、`sandboxState`（`0` 宿主、`1` 沙箱、`2` 未知）、`isolation`、`hostOnly`、SID/session/登录 LUID。错误 `2`/`3` 表示 Pipe 不可见（宿主未运行、规则未生效或应用未重启）；`sandboxState=1`、`hostOnly=1` 但 `Hello read` 返回 `233` 表示已连上 Pipe 后被服务端握手拒绝，应检查协议版本及安全模式匿名 token 兼容。
- **`ipc_probe.exe --check-candidates`**：创建临时 context 输入 `NI` 验证候选链路后取消销毁，不提交文本。
- **详细日志**：开关由宿主 Core 同步；TSF 日志写入当前进程的 LocalAppData 视图（Sandboxie 可能重定向该目录），排查时注意核对实际路径（见 [开发日志设计](windows-developer-logging-design.md)）。
- **默认 CTest**：`test_sandbox`、`test_sandboxie_template`、`test_hostcommands`、`test_isolationpolicy`、`test_pipeidentity`。

待实机验收：普通/隐私沙箱、安全模式、多沙箱并发、宿主停止/重启恢复、协议不匹配及 Pipe 未放行场景，确认仅运行一份宿主 Core、Settings 在沙箱外运行且配置写入宿主目录。
