# Sandboxie 宿主 Core 共享方案

## 目标与边界

Sandboxie 内的 TSF 只连接当前用户/session 的宿主 Core。输入法列表、引擎选项、词库和学习记录由宿主 Core 管理；Settings 由宿主 Core 在沙箱外启动。连接失败不得启动沙箱 Core 或创建另一套用户配置。

本方案针对 Sandboxie；AppContainer 使用独立的受限输入策略，见 [现代应用支持](windows-modern-apps-design.md)。Sandboxie 共享本身不要求新增 TSF category。普通构建不修改 Sandboxie 配置、注册表或当前服务；实际应用兼容规则需要显式选择目标沙箱。

## 参考与现状

Weasel 的客户端通过 Named Pipe 连接 WeaselServer，Sandboxie 的 Weasel 模板精确放行 `\Device\NamedPipe\%user%\WeaselNamedPipe`。参考：

- https://github.com/rime/weasel/blob/master/WeaselIPC/WeaselClientImpl.cpp
- https://github.com/sandboxie-plus/Sandboxie/blob/master/Sandboxie/install/Templates.ini
- https://sandboxie-plus.github.io/sandboxie-docs/Content/OpenPipePath.html
- https://sandboxie-plus.github.io/sandboxie-docs/Content/SBIEDLLAPI.html

旧实现仅根据 TSF 进程可见的 mutex 和 LocalAppData 状态判断 Core 是否运行，可能在隔离视图中启动另一份 Core/Settings，隐藏宿主配置或使 Windows App SDK bootstrap 失败。当前实现识别沙箱后禁止本地服务操作，直接连接宿主 Pipe，避免创建第二套服务和用户配置。

## 进程策略

共享辅助模块动态查询已加载的 SbieDll API。区分宿主、沙箱和未知状态；模块存在但查询异常时禁止本地服务启动，不把宿主注入模式误判成已确认的沙箱。

沙箱 TSF 使用 HostOnly 策略：不创建 ServiceController，不读取或写入本地服务状态，不启动 Core/Settings/control 子进程。Core 和 Settings 入口在读取用户配置、创建服务对象之前拒绝沙箱或无法确认的环境。

连接使用现有 SID/session Pipe 名称和 ACL。沙箱内客户端检查 Pipe 服务端 PID，使用 Sandboxie API 排除沙箱服务端；无法确认时拒绝连接并记录错误。保留协议握手、有限超时和重连间隔；失败时不消费按键，取消旧预编辑，超时请求不重放。

启用 `UseSecurityMode=y` 时，Sandboxie 内查询到的用户 token 与宿主通过 Pipe 模拟查询到的 token 可能不同：宿主看到匿名 SID `S-1-5-7` 和匿名登录 LUID。因此，原管理 Pipe 在连接已通过现有用户专用 DACL、token 查询成功且客户端不是 AppContainer 时，允许匿名 token 按系统 `GetNamedPipeClientSessionId` 核对实际客户端 session，不要求匿名 token 与 Core 的登录 LUID 相同。普通匿名进程仍不能直接通过该 DACL；不增加匿名 ACE，也不放宽 Pipe 权限。其余客户端和 AppContainer 输入 Pipe 继续要求 `sameLogin`，匿名例外不适用于受限输入入口。

## Sandboxie 兼容规则

辅助脚本默认仅输出当前用户的配置片段，不修改系统；只有显式 `-Apply -BoxName` 才写配置。规则形如：

```ini
OpenPipePath=\Device\NamedPipe\fcitx5-windows-v2-<当前用户SID>-*
```

诊断或初次验收可以精确到当前 session。无需放行用户配置目录、Windows App Runtime 包目录、服务 mutex/event 或所有 IPC。应用规则后需重新打开受影响的沙箱应用，旧的沙箱 Core/Settings 不会被实现自动终止。

放行 Pipe 也允许沙箱程序调用既有设置修改协议，这是共享服务的权限边界。维持当前用户 ACL、session 范围和请求校验，不开放任意命令、可执行路径或文件路径。

## 用户操作手册

### 1. 准备宿主输入法与目标沙箱

1. 先在沙箱外确认 Fcitx5 已安装、已注册，并能在普通 x64 应用中输入中文。Core、TSF DLL 和 Settings 应来自同一套构建，使用一致的协议 v10；仅添加 Sandboxie 规则不能代替输入法安装或注册。
2. 在 Sandboxie 管理器中确认要配置的沙箱已启用，并记下它在 `Sandboxie.ini` 中的节名，例如 `[DefaultBox]` 对应 `DefaultBox`。下文命令中的 `DefaultBox` 都要替换为实际节名，而非自定义显示名称。多个沙箱需要分别配置。
3. 在沙箱外，以平时使用输入法的 Windows 用户启动已部署的 `bin/Fcitx5.exe`。例如安装目录为 `C:\Fcitx5` 时，在沙箱外 PowerShell 中运行：

   ```powershell
   Start-Process -FilePath 'C:\Fcitx5\bin\Fcitx5.exe'
   ```

   请替换为实际部署路径，保留相邻的 `share`、`tsf` 和 `settings` 目录。不要从 Sandboxie 的“在沙箱中运行”入口启动 Core，也不要让强制沙箱规则把它重定向到沙箱内。若之前手动关闭过服务，需要在沙箱外使用“重启服务”，或运行同一路径的 `Fcitx5.exe --restart-services` 解除停止状态。

### 2. 生成当前用户的规则

在沙箱外、当前用户的 PowerShell 中切换到本仓库根目录，运行：

```powershell
.\scripts\sandboxie-template.ps1 -BoxName DefaultBox
```

脚本只显示配置片段，不会修改 Sandboxie。输出类似：

```ini
[DefaultBox]
OpenPipePath=\Device\NamedPipe\fcitx5-windows-v2-S-1-5-21-111111111-222222222-333333333-1001-*
```

上面的 SID 仅为示例，应使用脚本实际输出的整行。没有仓库脚本时，可以在沙箱外运行 `whoami /user`，将显示的当前用户 SID 填入下面的规则；不要保留尖括号，也不要照抄其他用户的 SID：

```ini
OpenPipePath=\Device\NamedPipe\fcitx5-windows-v2-<当前用户SID>-*
```

末尾 `*` 用于匹配该用户的 session 编号，实际连接仍受当前用户/session 的 Pipe 名称和 ACL 限制。首次排查时可给脚本加上 `-SessionOnly`，生成只匹配当前 session 的规则；注销再登录后 session 编号可能变化，需要重新生成。通常日常使用选择默认的 `-*` 规则。

### 3. 将规则加入 Sandboxie

以下两种方法任选一种。修改前先备份现有 `Sandboxie.ini`。规则只对选定沙箱放行 Fcitx5 Pipe；该沙箱中的程序也因此可以通过现有 IPC 修改宿主输入法设置和调用服务控制操作，请只为需要共享输入法的沙箱启用。

#### 方法 A：手动编辑配置

1. 打开 Sandboxie 管理器的 `Sandboxie.ini` 编辑入口。Sandboxie Classic 为“配置（Configure）→ 编辑配置（Edit Configuration）”；Sandboxie Plus 中找到编辑 `Sandboxie.ini` 的入口，具体菜单名称可能随版本或界面语言变化。
2. 在配置中找到目标沙箱已有的节，例如 `[DefaultBox]`，将第 2 步生成的 `OpenPipePath=...` 整行添加到该节内、下一个 `[节名]` 之前。保留该节原有配置和其他 `OpenPipePath` 行，不要重复添加 `[DefaultBox]`，也不要放入 `[GlobalSettings]` 或其他沙箱节。如果编辑器仅显示当前沙箱的配置，只添加规则行即可。
3. 保存配置并让 Sandboxie 重新加载。Classic 从“编辑配置”关闭编辑器时会自动重新加载；必要时使用“配置 → 重新加载配置（Reload Configuration）”。回到配置中确认该规则仍在目标节下。

例如，目标沙箱原有 `Enabled=y` 时，添加后的局部内容应类似下列形式（SID 仍须替换）：

```ini
[DefaultBox]
Enabled=y
OpenPipePath=\Device\NamedPipe\fcitx5-windows-v2-S-1-5-21-111111111-222222222-333333333-1001-*
```

规则使用单个反斜杠，无需加引号。无需额外添加 `OpenIpcPath=*`、放行全部 Named Pipe、开放用户配置目录或 Windows App Runtime 目录。手动编辑和重新加载配置的说明见 [Sandboxie 官方配置菜单文档](https://sandboxie-plus.github.io/sandboxie-docs/Content/ConfigureMenu.html)，规则类型见 [OpenPipePath 文档](https://sandboxie-plus.github.io/sandboxie-docs/Content/OpenPipePath.html)。

#### 方法 B：用仓库脚本写入

在仓库根目录、沙箱外的 PowerShell 中先预览，再执行写入：

```powershell
.\scripts\sandboxie-template.ps1 -BoxName DefaultBox -Apply -WhatIf
.\scripts\sandboxie-template.ps1 -BoxName DefaultBox -Apply
```

第一条只预览；第二条通过 `SbieIni.exe` 向指定沙箱追加规则，并检查是否保存成功。已有完全相同的规则时不会重复追加。脚本不创建沙箱，也不启动 Core 或关闭应用。脚本支持的沙箱节名为 1–32 个英文字母、数字或下划线；其他名称可使用手动编辑方式。

若提示找不到 `SbieIni.exe`，指定实际安装路径，例如：

```powershell
.\scripts\sandboxie-template.ps1 -BoxName DefaultBox -Apply -SbieIniPath 'D:\Apps\Sandboxie-Plus\SbieIni.exe'
```

### 4. 重新打开应用并确认输入

1. 保存沙箱应用中的工作，正常退出受影响的应用及其驻留后台进程，再从目标沙箱重新打开。仅打开一个新标签页或新窗口可能仍复用旧进程，不能保证规则已重新加载。
2. 在沙箱应用的普通文本框中，通过 Windows 输入法切换选择 Fcitx5，再通过语言栏或 `Ctrl+Space` 选中拼音条目。输入 `nihao`，确认有预编辑和候选，并可按空格提交中文；测试其他引擎时使用该引擎对应的编码。
3. 从 Fcitx5 菜单打开设置，确认 Settings 在沙箱外运行，输入法列表和选项与沙箱外一致。配置和学习记录由宿主 Core 共享，不会因沙箱不同而各自隔离。

宿主 Core 必须保持运行。若在沙箱内关闭了服务，需要回到沙箱外重新启动服务；沙箱内 TSF 不会自动拉起 Core。旧版本遗留的沙箱 Core/Settings 也不会自动退出，应保存工作后自行关闭。

### 5. 常见问题与诊断

| 现象 | 检查方法 |
| --- | --- |
| 沙箱外也无法输入 | 先检查宿主 Core、输入法注册和引擎数据，确认沙箱外普通 x64 应用可用后再排查规则。 |
| 沙箱外正常，沙箱内无法输入 | 确认应用实际位于配置的沙箱，规则在正确节内，SID 属于当前用户，并已彻底退出、重新打开应用。若使用 `-SessionOnly`，重新生成当前 session 的规则。 |
| 脚本提示沙箱不存在或未启用 | 检查 `-BoxName` 是否为配置节名，以及目标节的 `Enabled=y`；脚本不会新建或启用沙箱。 |
| 规则无法保存或脚本写入失败 | 检查 Sandboxie 配置锁定、管理权限及 `SbieIni.exe` 路径，处理具体报错后重新确认规则已保存。 |
| 连接后提示协议不匹配 | 使用同一套构建的 Core、TSF DLL 和 Settings，并重新打开加载旧 DLL 的应用。Pipe 名中的 `v2` 不是 IPC 协议版本，不要改成当前 framing 版本 `v10`。 |
| Settings 在沙箱内启动或出现另一份 Core | 检查是否仍在使用旧构建、残留沙箱进程或强制沙箱规则；当前实现要求 Core 和 Settings 在沙箱外运行。 |

若已有构建产物，可在沙箱外和目标沙箱内分别运行 `ipc_probe.exe --diagnose`（使用实际文件路径），比较 Pipe 名称、连接阶段、Windows 错误码和服务端 PID。该命令不启动服务或修改配置。错误 `2/3` 只说明当前 Pipe 不可见，可能是宿主未启动、规则未生效或应用未重启；不能仅凭错误码判断原因。诊断成功只证明 IPC 连接，真实输入和候选窗仍需按上面的应用步骤验证。

诊断输出同时包含 `sandboxState`（`0` 宿主、`1` 沙箱、`2` 未知）、`isolation`、`tokenKnown`、AppContainer/完整性状态、SID、session 和登录 LUID。`sandboxState=1`、`hostOnly=1` 但在 `Hello read` 返回错误 `233`，表示已经识别沙箱并打开 Pipe，随后被服务端断开；应检查协议和握手身份校验，不能据此判断为沙箱检测失败。安全模式下尤其需要确认宿主 Core 包含匿名 token 兼容逻辑。

### 6. 撤销规则

在目标沙箱的配置节中删除本次添加的 Fcitx5 `OpenPipePath=...` 行；若同时添加过 session 专用规则，也一并删除。保存、重新加载配置，并正常退出后重新打开受影响的沙箱应用。保留其他应用的规则。撤销后该沙箱将无法通过此规则连接宿主 Core，不影响沙箱外的 Fcitx5 安装和用户数据。

## 操作路由

| 操作 | 沙箱内实现 |
| --- | --- |
| 输入、输入法轮换 | 现有宿主 Core IPC |
| 设置页 | 现有 OpenSettings，由宿主 Core 启动 Settings |
| UI 语言 | GetGlobalSettings，使用宿主解析后的语言，不读沙箱配置 |
| 用户数据目录 | OpenUserDataFolder，由宿主 Core 定位和打开固定目录 |
| 重启/关闭服务 | RestartServices/StopServices，由宿主 Core 启动自身固定路径的控制进程 |
| 候选窗、关于 | 由应用进程中的 TSF 按 UIElement 显示许可处理 |

宿主操作只允许空 payload 和 contextId=0。Core、TSF、Settings 必须使用同版协议。服务控制先写出回复，再投递到 Core 主线程启动既有 `--restart-services` / `--stop-services` 控制程序；不在 Pipe 线程操作引擎，也不等待自身退出。回复只代表请求已接受，后续启动失败记录诊断，不承诺 exactly-once。

宿主 Core 未运行、被手动关闭或规则未放行时，沙箱 TSF 只重试连接并提供故障信息。用户需在沙箱外启动/重启 Core。安装器现有可选登录启动项可预先启动宿主 Core，但不解除手动停止状态；没有常驻唤醒代理。

## 验证与交付

代码已接入 HostOnly 策略、服务端归属检查、UI 语言 IPC 和宿主操作。现代应用支持将协议统一升级至 v10，Sandboxie endpoint 名和管理行为保持不变。生成规则：

```powershell
./scripts/sandboxie-template.ps1 -BoxName DefaultBox -SessionOnly
./scripts/sandboxie-template.ps1 -BoxName DefaultBox
./scripts/sandboxie-template.ps1 -BoxName DefaultBox -Apply
```

前两个命令只向标准输出写入片段；最后一个命令通过 Sandboxie 的 `SbieIni.exe append` 将规则写入指定沙箱。`-Apply` 是显式配置操作，必须提供 `-BoxName`。应用规则后重新打开应用。首次使用需先在沙箱外启动 Core；手动关闭后同样需在沙箱外重启。Core、TSF、Settings 必须使用一致的协议 v10 构建。

`-Apply -WhatIf` 不执行 Sandboxie 命令；应用前检查目标沙箱已启用，写入后确认规则存在，已有相同规则时不重复追加。脚本不会关闭应用或终止进程。

诊断可在沙箱内外分别运行 `ipc_probe.exe --diagnose`，比较 Pipe 名称、连接阶段、Windows 错误码和服务端 PID。该模式不创建输入 context、不修改配置、不启动服务。错误 2/3 表示当前 Pipe 不可见，可能是 Core 未运行，也可能是缺少规则或应用尚未重启；它本身不能证明是哪一种。沙箱外诊断成功而沙箱内失败时优先核对目标沙箱的有效规则。

`ipc_probe.exe --check-candidates` 创建临时输入 context，以当前可用中文条目输入 `NI`，检查预编辑和候选后取消并销毁 context，不提交文字或保存配置。它用于验证候选数据链路，不验证真实应用中的候选窗显示；码表等条目对 `NI` 不一定产生候选，需结合当前引擎判断失败。

自动化回归包括 `test_sandbox`、`test_sandboxie_template`、`test_hostcommands`、`test_isolationpolicy` 和 `test_pipeidentity`，覆盖查询状态、启动禁止、服务端归属、控制请求/回复后执行、模板及语言来源。`test_pipeidentity` 使用真实 Pipe 检查 peer session/login 及管理入口匿名例外；模拟身份通过不代表真实 Sandboxie 安全模式已经验收。

详细日志开关由宿主 Core 回复同步。诊断文件位于 TSF 进程的 LocalAppData 视图，Sandboxie 可能重定向该目录；比较沙箱内外日志时确认实际位置，不能假定全部文件都在宿主目录。日志不包含输入文本，详见 [开发日志设计](windows-developer-logging-design.md)。

完整构建使用隔离 Prefix/BuildRoot，重建 Core、TSF、Settings。真实验收包括普通/隐私沙箱、多个沙箱、宿主缺失/停止/重启、协议不匹配和 Pipe 未放行。确认只运行一份宿主 Core、Settings 未加载沙箱 DLL、配置写入宿主目录、预编辑不跨输入框/沙箱保存。自动化模拟和免注册 probe 不代替真实 Sandboxie/应用验证。
