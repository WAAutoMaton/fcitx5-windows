# Windows 开发者选项与详细日志设计

## 当前状态

Core、TSF 和 Settings 已接入共享异步结构化日志，同时 Core 默认持久化纯文本 Fcitx5 引擎日志。Settings 全局设置提供默认关闭的详细日志开关。当前 IPC 为 **v17**，`windows-ui.conf` 为 **v4**。

- **引擎日志（`fcitx5-YYYYMMDD.log`）**：始终记录，默认阈值为 `Info`，开启详细日志后即时切换到 `Debug`（`key_trace` 保持关闭）。
- **详细结构化日志（`core-*`、`tsf-*`、`settings-*`）**：仅开启详细日志时写入，记录 TSF/IPC、Pipe 握手与 UAC 身份、候选位置与渲染、焦点、Core 状态，以及 Settings 操作阶段、窗口/进程退出和异常元数据。

启用方式及测试命令见 [build.md](build.md#developer-logging)。

## 配置与生效

Core 是全局配置 `conf/windows-ui.conf`（位于 `%APPDATA%/Fcitx5/config/fcitx5/conf`）的唯一写入者，Settings 通过 IPC 读写；普通桌面 TSF 的语言后台读取见 [UI 国际化设计](windows-ui-localization-design.md)。TSF 的详细日志开关仅从 Core 回复同步，收到回复前保持关闭。

```ini
[General]
Version=4
Language=system
Revision=2
DetailedLogging=false
CandidateLayout=vertical

[Shortcuts]
0=Control+space
1=Control+Alt+1
2=Control+Alt+2
3=Control+Alt+3
4=Control+Alt+4
5=Control+Alt+5
```

旧版本迁移规则：v1 加载时 `DetailedLogging=false`，v1/v2 补齐默认快捷键，v1–v3 默认 `CandidateLayout=vertical`，下次成功保存统一写入 v4。语言、详细日志、全局快捷键和候选框布局共享全局 revision（与输入法/引擎设置独立），任一字段变化递增 revision，旧草稿不能覆盖新配置，保存超时不重放。

| 行为 | 生效方式 |
| --- | --- |
| UI 语言 | Core/Settings 重启服务后生效；原生菜单/关于在后台刷新 |
| 候选框布局 | Core 在下一次 `KeyReply` 中传递，TSF 立即应用 |
| 详细日志开关 | Core/Settings 应用成功后即时更新；引擎日志同步切换 `Info`/`Debug`；TSF 从下一次 `KeyReply` 或全局设置回复同步 |
| 断线 | TSF 保留当前已知日志状态，重连后以 Core 回复覆盖 |
| 关闭详细日志 | 停止接收新详细事件并在后台排空已有队列；保留历史文件；引擎日志回落到 `Info` |

Settings 全局页的**常规**分页包含 UI 语言和候选框布局，**开发者选项**分页包含详细日志开关、当前 Settings logger 状态、日志目录、文件数/总大小及**打开日志目录**按钮。全局重置将草稿恢复为跟随系统语言、竖排候选框、关闭详细日志和默认快捷键，点击**应用**后保存。**打开日志目录**异步创建并打开目录，关闭详细日志时也可查看已有文件；目录不可用不影响其他设置保存。

## 组件与线程

`win32/ipc/diagnostic_log.h/.cpp` 分别编译到 Core、TSF 和 Settings 三个工程，不跨进程或工具链共享 C++ 对象。主要接口为 `setEnabled`、`event`、`engineMessage`、`status`、`maintain`，以及供后台和显式退出路径使用的 `directory` 与有界 `flush`。

- 默认队列上限 4096，事件字段有界；前台调用不等待 worker，也不执行文件系统操作。
- 后台按需提交 Windows thread-pool callback 执行目录创建、写入、轮转和清理；TSF worker 持有 DLL 引用直至回调返回，避免在 `DllMain` 中执行日志 I/O。
- 关闭时后台排空队列，250 ms 后可丢弃剩余 Debug/Info 事件，优先保留警告与错误。
- 队列满、内存不足、磁盘满或权限错误均不影响输入、编辑与服务控制；丢弃计数与写入错误通过状态/事件和限频 `OutputDebugString` 暴露并在后续重试。`flush` 尽力排空内存队列，不保证极端崩溃时的尾部持久化。

## 格式与轮转

详细日志采用 JSON Lines，每条包含 `time`（UTC ISO-8601）、`level`、`role`、`pid`、`tid`、`session`（进程创建时间标识）、`build`、`event` 及结构化字段。引擎日志采用纯文本，每行以 UTC 时间戳开头。

```text
%LOCALAPPDATA%/Fcitx5/logs/fcitx5-YYYYMMDD.log
%LOCALAPPDATA%/Fcitx5/logs/core-YYYYMMDD.log
%LOCALAPPDATA%/Fcitx5/logs/tsf-PID-YYYYMMDD.log
%LOCALAPPDATA%/Fcitx5/logs/settings-PID-YYYYMMDD.log
```

| 项目 | 策略 |
| --- | --- |
| 轮转 | UTC 日期变化或单文件达到 8 MiB（使用 `.log.1` 等编号后缀） |
| 保留期限 | 按文件 `last_write_time` 清理超过 7 天的匹配日志 |
| 容量限额 | 引擎日志与详细日志共享目录 512 MiB 配额，优先删除最旧文件；详细日志另有每角色/PID 前缀 64 MiB 配额 |
| 清理范围 | 仅处理符合 Fcitx 日志命名的普通文件，跳过其他文件且不跟随目录符号链接 |
| 活跃文件 | 当前打开及受其他进程共享锁保护的文件保留到后续维护清理 |
| 权限 | 继承当前用户 LocalAppData 目录 ACL |

Core 启动及运行期间每小时执行一次清理，写入操作也会触发清理。Sandboxie/AppContainer 进程写入各自 LocalAppData 视图（可能被重定向或不可写），输入入口不代存受限客户端日志。

## 事件与隐私边界

| 范围 | 记录的诊断元数据 |
| --- | --- |
| TSF 生命周期 / context | 激活 flags、token 状态、sink HRESULT、focus/context 变化与 generation |
| 文档编辑 | 同步/异步请求、原 context 有效性、队列/代次、耗时、失败/取消/迟到结果 |
| IPC / Core | 连接阶段、服务端 PID、消息类型/长度、耗时、超时/断线、owner 校验、设置 revision |
| Pipe 身份 | `pipe_identity` 连接序号、peer PID、入口类型、登录匹配/拒绝原因、elevation 与错误码 |
| 候选 / UIElement | 数量、索引、`candidateGeneration`、布局 HRESULT、clipped/point/caret 回退、文档边界、显示协商与隐藏原因 |
| Popup / 绘制 | owner/popup DPI、矩形/工作区、坐标转换、窗口/target 生命周期、软件 DC 回退、device loss 与重试 |
| Settings | 连接/保存错误、草稿操作阶段、窗口关闭、dispatcher/runtime 退出与异常元数据 |
| logger | 启停、目录/文件创建、轮转、清理、丢弃与错误码 |

隐私与内容边界：

- **结构化日志**：仅记录固定事件/操作名、错误码、计数、长度和几何坐标；TSF 的 `key_result` 记录按下/释放、扫描码、修饰键和消费状态，不记录转换后的字符或 Unicode（注意扫描码仍会反映键位活动）。不记录 preedit、commit、候选文本/标签/注释、周边文本、密码、原始 payload、窗口标题、异常消息、配置值或用户路径。
- **纯文本引擎日志**：Core 通过 `src/windowslogging.cpp` 收集上游 Fcitx5 日志流，详细日志开关实时在 `*=4,key_trace=0` 与 `*=5,key_trace=0` 间切换（`key_trace` 始终关闭）。单条消息超过 64 KiB 时截断并标记。纯文本日志保留上游 addon 输出的原文（可能含路径或其他上下文信息）；详细日志侧仅记录带严重度与 `message_bytes` 的 `fcitx_log` 元数据。详细日志关闭时，上游消息同时输出到 `stderr`。
- **握手早期诊断**：TSF 尚未收到 Core 日志开关前的握手失败通过限频 `OutputDebugStringW` 输出固定阶段和错误码。`ipc_probe --diagnose` 仅在显式终端输出中显示 SID/session、登录 LUID、elevation 与 linked 关系，不写入持久日志（见 [UAC 诊断](windows-uac-ipc-design.md#诊断设计)）。

## Settings 退出与异常诊断

`win32/settings/diagnostics.h` 在开启详细日志时记录 Settings 生命周期与异常：

- `settings_operation_begin` / `stage` / `end` 通过 `operation_id` 关联页面渲染和操作步骤，`end` 记录耗时及是否处于栈展开。
- `settings_exception` 记录固定操作名、异常类型、数字 HRESULT 和 `fatal` 标志，不记录 `what()` 文本。
- 分别记录窗口打开/关闭、关闭按钮、服务停止、dispatcher 返回、runtime shutdown，以及带 `reason`/`exit_code` 的 `settings_exit`。
- XAML 未处理异常、C++/WinRT 回调 fail-fast 和 `std::terminate` 尽力在 250 ms 内排空队列并保持原有失败行为。

`settings_window_closed` 后紧跟 `settings_exit` 表示清理流程完整结束。若发生访问违规、栈/堆损坏、外部终止或 logger 阻塞导致尾部事件缺失，需结合 Windows Application Error/WER 排查。

## 验证入口

| 层级 | 入口与覆盖范围 |
| --- | --- |
| 单元 / 模拟 | `test_diagnostic_log`（轮转/期限/容量/丢弃/失败/引擎日志）；`test_i18n`（v1–v4 迁移、布局、日志字段与共享 revision）；`test_protocol`（v17 编解码与非法字段） |
| TSF 免注册 | `test_candidate`、`test_candidate_tsf`（绘制/几何、UIElement、迟到 session 与日志元数据） |
| 真实引擎 / IPC | `scripts/test-developer-logging.ps1`（引擎 Info/Debug 即时切换与重启恢复、Core/TSF 开关同步、真实输入、断线与隐私检查） |
| WinUI 开发者页 | `scripts/test-ui-localization.ps1 -TestLogging`（开发者页、应用/重置/冲突、目录与关闭行为） |
| Settings 退出 | `scripts/test-settings-exit-logging.ps1`（正常退出、可恢复 HRESULT、异步 fail-fast/`std::terminate`、隐私与日志关闭） |

集成测试需使用 `FCITX5_UI_TEST` 构建的 Core/TSF/probes 和 `build-settings.ps1 -UiTest` 构建的 Settings，runner 通过 `FCITX5_DIAGNOSTIC_TEST_DIRECTORY` 隔离日志目录（生产构建忽略该变量）。退出测试在专用子进程中运行，致命用例有意终止测试子进程。命令详见 [build.md](build.md#developer-logging)。

## 现场收集与待验收项

排查问题时，请记录复现时间与故障应用 PID，收集同一时间段的 `fcitx5-*.log`、`core-*.log`、对应 `tsf-<PID>-*.log` 及必要的 `settings-<PID>-*.log`，按时间戳、PID/TID、进程 session 和事件顺序关联。

受限宿主（Sandboxie/AppContainer）的实际日志目录行为、已注册应用矩阵、物理混合 DPI 和正式安装环境仍需实测。
