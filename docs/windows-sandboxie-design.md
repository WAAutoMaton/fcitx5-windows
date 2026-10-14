# Sandboxie 宿主 Core 共享方案

## 目标与边界

Sandboxie 内的 TSF 只连接当前用户/session 的宿主 Core。输入法列表、引擎选项、词库和学习记录由宿主 Core 管理；Settings 由宿主 Core 在沙箱外启动。连接失败不得启动沙箱 Core 或创建另一套用户配置。

本方案针对 Sandboxie，不声明 UWP/AppContainer 兼容能力，不修改 TSF 注册 category。实现与构建不修改 Sandboxie 配置、注册表或当前服务；实际启用兼容规则需要显式选择目标沙箱。

## 参考与现状

Weasel 的客户端通过 Named Pipe 连接 WeaselServer，Sandboxie 的 Weasel 模板精确放行 `\Device\NamedPipe\%user%\WeaselNamedPipe`。参考：

- https://github.com/rime/weasel/blob/master/WeaselIPC/WeaselClientImpl.cpp
- https://github.com/sandboxie-plus/Sandboxie/blob/master/Sandboxie/install/Templates.ini
- https://sandboxie-plus.github.io/sandboxie-docs/Content/OpenPipePath.html
- https://sandboxie-plus.github.io/sandboxie-docs/Content/SBIEDLLAPI.html

Fcitx5 当前在 TSF 进程中根据命名 mutex 和 LocalAppData 状态文件判断 Core 是否运行。Sandboxie 隔离这些对象后，TSF 会启动沙箱内 Core；其文件视图又可能隐藏宿主配置。Settings 随沙箱 Core 在沙箱内启动，Windows App SDK bootstrap 初始化也可能失败。

## 进程策略

共享辅助模块动态查询已加载的 SbieDll API。区分宿主、沙箱和未知状态；模块存在但查询异常时禁止本地服务启动，不把宿主注入模式误判成已确认的沙箱。

沙箱 TSF 使用 HostOnly 策略：不创建 ServiceController，不读取或写入本地服务状态，不启动 Core/Settings/control 子进程。Core 和 Settings 入口在读取用户配置、创建服务对象之前拒绝沙箱或无法确认的环境。

连接使用现有 SID/session Pipe 名称和 ACL。沙箱内客户端检查 Pipe 服务端 PID，使用 Sandboxie API 排除沙箱服务端；无法确认时拒绝连接并记录错误。保留协议握手、有限超时和重连间隔；失败时不消费按键，取消旧预编辑，超时请求不重放。

## Sandboxie 兼容规则

辅助脚本仅输出当前用户的配置片段，默认不修改系统。规则形如：

```ini
OpenPipePath=\Device\NamedPipe\fcitx5-windows-v2-<当前用户SID>-*
```

诊断或初次验收可以精确到当前 session。无需放行用户配置目录、Windows App Runtime 包目录、服务 mutex/event 或所有 IPC。应用规则后需重新打开受影响的沙箱应用，旧的沙箱 Core/Settings 不会被实现自动终止。

放行 Pipe 也允许沙箱程序调用既有设置修改协议，这是共享服务的权限边界。维持当前用户 ACL、session 范围和请求校验，不开放任意命令、可执行路径或文件路径。

## 操作路由

| 操作 | 沙箱内实现 |
| --- | --- |
| 输入、输入法轮换 | 现有宿主 Core IPC |
| 设置页 | 现有 OpenSettings，由宿主 Core 启动 Settings |
| UI 语言 | GetGlobalSettings，使用宿主解析后的语言，不读沙箱配置 |
| 用户数据目录 | 新 IPC，由宿主 Core 定位和打开固定目录 |
| 重启/关闭服务 | 新 IPC，由宿主 Core 启动自身固定路径的控制进程 |
| 候选窗、关于 | 继续由应用进程中的 TSF 显示 |

新增操作只允许空 payload 和 contextId=0。协议版本同步更新 Core、TSF、Settings。服务控制先写出回复，再投递到 Core 主线程启动既有 `--restart-services` / `--stop-services` 控制程序；不在 Pipe 线程操作引擎，也不等待自身退出。回复只代表请求已接受，后续启动失败应记录诊断，不承诺 exactly-once。

宿主 Core 未运行、被手动关闭或规则未放行时，沙箱 TSF 只重试连接并提供故障信息。用户需在沙箱外启动/重启 Core。可选登录启动和常驻唤醒代理不属于本轮实现。

## 验证与交付

代码已接入 HostOnly 策略、服务端归属检查、UI 语言 IPC 和协议 v8 的宿主操作。生成规则：

```powershell
./scripts/sandboxie-template.ps1 -BoxName DefaultBox -SessionOnly
./scripts/sandboxie-template.ps1 -BoxName DefaultBox
./scripts/sandboxie-template.ps1 -BoxName DefaultBox -Apply
```

前两个命令只向标准输出写入片段；最后一个命令通过 Sandboxie 的 `SbieIni.exe append` 将规则写入指定沙箱。`-Apply` 是显式配置操作，必须提供 `-BoxName`。应用规则后重新打开应用。首次使用需先在沙箱外启动 Core；手动关闭后同样需在沙箱外重启。Core、TSF、Settings 必须使用一致的协议 v8 构建。

`-Apply -WhatIf` 不执行 Sandboxie 命令；应用前检查目标沙箱已启用，写入后确认规则存在，已有相同规则时不重复追加。脚本不会关闭应用或终止进程。

诊断可在沙箱内外分别运行 `ipc_probe.exe --diagnose`，比较 Pipe 名称、连接阶段、Windows 错误码和服务端 PID。该模式不创建输入 context、不修改配置、不启动服务。错误 2/3 表示当前 Pipe 不可见，可能是 Core 未运行，也可能是缺少规则或应用尚未重启；它本身不能证明是哪一种。沙箱外诊断成功而沙箱内失败时优先核对目标沙箱的有效规则。

`ipc_probe.exe --check-candidates` 创建临时输入 context，以当前可用中文条目输入 `NI`，检查预编辑和候选后取消并销毁 context，不提交文字或保存配置。它用于验证候选数据链路，不验证真实应用中的候选窗显示；码表等条目对 `NI` 不一定产生候选，需结合当前引擎判断失败。

自动化回归覆盖 Sandboxie 查询的宿主/沙箱/异常状态、启动禁止、服务端归属、控制请求校验、回复后执行、模板输出和 UI 语言来源；保留桌面服务生命周期、IPC 超时和 TSF 编辑回归。

完整构建使用隔离 Prefix/BuildRoot，重建 Core、TSF、Settings。真实验收包括普通/隐私沙箱、多个沙箱、宿主缺失/停止/重启、协议不匹配和 Pipe 未放行。确认只运行一份宿主 Core、Settings 未加载沙箱 DLL、配置写入宿主目录、预编辑不跨输入框/沙箱保存。自动化模拟和免注册 probe 不代替真实 Sandboxie/应用验证。
