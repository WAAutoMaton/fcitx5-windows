# Windows 开发者选项与详细日志设计

## 当前状态

Core、TSF 和 Settings 已接入共享异步结构化日志，Settings 的全局页提供默认关闭的详细日志开关。当前 IPC 为 v12，`windows-ui.conf` 为 v3；日志包括 Settings 窗口/进程退出、操作阶段、异常，以及 Pipe 握手身份和 UAC 关系诊断。

启用方式及构建/测试入口见 [build.md](build.md#developer-logging)。日志主要用于 TSF/IPC、候选位置与渲染、焦点、Core 状态及 Settings 故障；不录制输入内容。

## 配置与生效

Core 是全局配置的唯一写入者，Settings 通过 IPC 读取/提交，普通桌面 TSF 的语言后台读取仍沿用 [UI 国际化设计](windows-ui-localization-design.md)。TSF 的日志开关只从 Core 回复同步，未收到开关前保持关闭。

```ini
[General]
Version=3
Language=system
Revision=2
DetailedLogging=false

[Shortcuts]
0=Control+space
1=Control+Alt+1
2=Control+Alt+2
3=Control+Alt+3
4=Control+Alt+4
5=Control+Alt+5
```

`windows-ui.conf` 位于 `%APPDATA%/Fcitx5/config/fcitx5/conf`。兼容读取 v1/v2 并补齐默认快捷键，v1 加载时 DetailedLogging=false，下次成功保存写 v3。语言、日志和快捷键共享全局 revision，与输入法/引擎设置独立；任一字段变化增加 revision，旧草稿不能覆盖新设置，保存超时不重放。

| 设置 | 生效 |
| --- | --- |
| UI 语言 | Core/Settings 重启服务后生效；原生菜单/About 独立后台刷新 |
| 详细日志 | Core/Settings 应用成功后即时更新；TSF 从下一次 KeyReply 或全局设置回复同步 |
| 断线 | TSF 保留已知日志状态，重连后以 Core 回复覆盖 |
| 关闭日志 | 停止接收新详细事件，后台排空已有队列；保留历史文件 |

Settings 全局页的“常规”tab 包含语言，“开发者选项”包含日志开关、状态、目录、文件数/大小和打开目录按钮。草稿、应用、重置、冲突和重试均包括日志字段；全局重置恢复跟随系统、关闭日志和默认快捷键，应用后才保存。状态反映当前 Settings logger。

“打开日志目录”异步创建/打开目录，不启用日志；即使开关关闭也可检查已有文件。默认关闭时不会因普通输入创建详细日志文件。目录不可用不阻止其他设置保存。

## 组件与线程

`win32/ipc/diagnostic_log.h/.cpp` 分别编译到三个工程，不跨进程或工具链共享 C++ 对象。主要接口为 `setEnabled`、`event`、`status`、`maintain`，以及仅供后台/显式退出路径使用的 `directory` 和有界 `flush`。

生产默认队列上限 4096，事件字段有界；前台接口不等待 worker，不执行文件系统操作。logger 使用按需提交的 Windows thread-pool callback 进行创建目录、写文件、轮转和清理。TSF worker 持有 DLL 引用直到 callback 返回，不在 DllMain 中启动日志 I/O。关闭时后台排空，250 ms 后可丢弃剩余 Debug/Info；警告/错误优先保留，但不保证进程退出前写完。

队列满、分配失败、磁盘满或权限错误不影响输入/编辑/服务操作。丢弃数、目录/轮转/清理和写入失败具有诊断状态/事件；失败限频 OutputDebugString，后续重试。`flush` 也只是尽力等待队列，不保证磁盘持久化或崩溃尾部完整。

## 格式与轮转

文件为 JSON Lines，每条包含 `time`、`level`、`role`、`pid`、`tid`、`session`、`build`、`event` 和结构化字段。时间戳为 UTC ISO-8601；session 是进程创建时间标识，不是 Windows sessionId 或登录 LUID。

```text
%LOCALAPPDATA%/Fcitx5/logs/core-YYYYMMDD.log
%LOCALAPPDATA%/Fcitx5/logs/tsf-PID-YYYYMMDD.log
%LOCALAPPDATA%/Fcitx5/logs/settings-PID-YYYYMMDD.log
```

| 项目 | 当前策略 |
| --- | --- |
| 轮转 | UTC 日期变化或单文件达到 8 MiB；使用 .log.1 等后缀 |
| 期限 | 按文件系统 last_write_time 清理超过 7 天的匹配日志 |
| 容量 | 本进程角色/PID 前缀 64 MiB，目录 512 MiB；优先删除最旧文件 |
| 清理范围 | 仅匹配 Fcitx 自有日志命名的普通文件，不删除其他文件或跟随目录符号链接 |
| 活跃文件 | 当前文件和其他进程受共享模式保护的文件保留；容量清理稍后重试，并非瞬时硬上限 |
| 权限 | 使用当前用户 LocalAppData 目录继承的 ACL，不创建系统共享权限 |

Core 文件按角色/日期命名，TSF 和 Settings 加 PID。多进程同日期或锁竞争可使用编号后缀。后台维护在启动、启用及后续工作中执行；所有进程停用时不承诺恰在第 7 天删除历史。

Sandboxie/AppContainer 中的目录属于该进程的 LocalAppData 视图，可能重定向或不可写。输入入口不转存受限客户端日志，也不因此放宽宿主目录 ACL；实际日志位置和可收集性需在目标应用确认。

## 事件与隐私

| 范围 | 诊断元数据 |
| --- | --- |
| TSF 生命周期/context | 激活 flags、token 状态、sink HRESULT、focus/context 变化和 generation |
| 编辑 | 同步/异步请求、原 context 有效性、队列/代次、耗时、失败/取消/迟到结果 |
| IPC/Core | 连接阶段、服务端 PID、消息类型/长度、耗时、超时/断线、owner 校验、设置 revision |
| Pipe 身份 | `pipe_identity` 的连接序号、peer PID、入口、登录匹配/拒绝原因、elevation 和查询错误码 |
| 候选/UIElement | 数量、索引、candidateGeneration、布局 HRESULT、clipped/caret fallback、文档边界、显示协商与隐藏原因 |
| Popup/绘制 | owner/popup DPI、矩形/工作区、坐标转换、窗口/target 生命周期、device loss 和有限 repaint 重试 |
| Settings | 连接/保存错误、草稿操作阶段、窗口关闭、dispatcher/runtime 退出和异常元数据 |
| logger | 启停、目录/文件创建、轮转、清理、丢弃与错误码 |

允许的字段是固定事件/操作名、错误码、计数、长度和几何等。TSF 的 `key_result` 还包含按下/释放、扫描码、修饰键和消费状态，不包含转换后的字符或 Unicode；扫描码仍会暴露键位活动。其他事件不记录 preedit、commit、候选正文/标签/注释、周边文本、密码、原始 payload、窗口标题、异常消息、配置值或用户路径。输入法 ID 等固定技术标识不作为用户文本输出。

Core 的上游日志由 `src/windowslogging.cpp` 的自定义 streambuf 接入，只转换为严重度和 `message_bytes`，不复制原消息。规则固定为 `*=3,key_trace=0,notimedate`，不启用未审计 addon Debug 或 key_trace。详细开关关闭后，原有低等级 stderr 错误输出仍沿用既有路径。

握手失败时 TSF 可能还未获得日志开关；此时通过限频的 `OutputDebugStringW` 输出固定阶段和错误码。`ipc_probe --diagnose` 可显式显示 SID/session、登录 LUID、elevation 与 linked 关系，这些身份值不写入持久日志。普通桌面与提权客户端的对照方法见 [UAC 诊断](windows-uac-ipc-design.md#诊断设计)。

## Settings 退出与异常

`win32/settings/diagnostics.h` 提供操作关联和异常记录：

- `settings_operation_begin/stage/end` 以 operation_id 关联页面渲染和操作步骤，end 包含耗时及是否处于栈展开。
- `settings_exception` 包含固定操作名、异常类型、数字 HRESULT 和 fatal 标志，不保存 what()/异常消息。
- 窗口打开/关闭、关闭按钮、服务停止、dispatcher 返回和 runtime shutdown 分别记录事件；正常进程出口记录带 reason/exit_code 的 `settings_exit`。
- XAML 未处理异常、C++/WinRT 回调 fail-fast 和 std::terminate 尽力记录，致命 C++ 路径尝试等待日志队列 250 ms，保持原有失败行为。

这些事件也受详细开关控制。`settings_window_closed` 后接 `settings_exit` 可帮助判断清理是否完成；缺失最终事件不能单独证明崩溃。访问违规、栈/堆损坏、外部终止或 logger 阻塞可能没有任何尾部记录，需结合 Windows Application Error/WER。

## 验证入口

| 层级 | 入口与覆盖 |
| --- | --- |
| 单元/模拟 | test_diagnostic_log：轮转/期限/容量/丢弃/失败；test_i18n：v1/v2 迁移、日志字段和共享 revision；test_protocol：v12 编码和非法字段 |
| TSF 免注册 | test_candidate、test_candidate_tsf：生产绘制/几何、UIElement、无 owner/layout、迟到 session 与日志元数据 |
| 真实引擎/IPC | scripts/test-developer-logging.ps1：Core/TSF 开关同步、真实输入、断线与隐私检查 |
| WinUI | scripts/test-ui-localization.ps1 -TestLogging：开发者页、应用/重置/冲突、目录与关闭行为 |
| Settings 退出 | scripts/test-settings-exit-logging.ps1：正常退出、可恢复 HRESULT、真实异步 fail-fast/std::terminate、隐私和日志关闭 |

集成测试使用 `FCITX5_UI_TEST` Core/TSF/probes，Settings 通过 `build-settings.ps1 -UiTest` 构建。测试 runner 设置独立的 `FCITX5_DIAGNOSTIC_TEST_DIRECTORY`；生产构建忽略该环境变量。退出测试只创建专用子进程/日志目录，不连接生产 Core 或停止已有 Settings，致命案例有意终止测试子进程。

命令参数和构建前提见 [开发日志验证](build.md#developer-logging)。

## 现场收集与剩余风险

启用日志后记录复现时间、发生故障的应用/PID，收集同时间段 Core、对应 tsf-PID 和必要的 settings-PID 文件，按 time/PID/TID/进程 session 与事件顺序关联。不要为复现临时加入输入文本或 payload。

真实宿主可能缺少布局/UIElement 通知，日志仅说明 TSF 观察到的结果。受限宿主日志目录、注册后的应用矩阵、物理混合 DPI 和安装行为仍需实测。异步队列、禁用状态和进程终止都可能使最后事件缺失；日志不提供崩溃恢复或输入重放能力。
