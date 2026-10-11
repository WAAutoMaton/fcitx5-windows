# Windows 安装包实现与设计

核对日期：2026-10-11。第一阶段安装实现、双版本打包和 CI/Nightly 发布流程已接入；真实系统安装、升级、卸载和重启清理仍需验收。本文区分当前代码行为、已记录的验证与后续设计，不能将打包成功描述为安装后已可直接使用。

## 当前交付与支持范围

使用 Inno Setup 7 生成 Windows 11 x64 EXE，安装到 Program Files 下的 `Fcitx5`。当前不提供 MSI、x86 TSF 或 ARM64 TSF；ARM64 Core 编译检查不代表原生 ARM64 应用可以加载输入法。

| CI 包名 | 运行库要求 |
| --- | --- |
| `Fcitx5-<run>-x64-setup.exe` | 附带 Microsoft x64 VC++ Redistributable 和 Windows App Runtime 安装程序 |
| `Fcitx5-<run>-x64-no-runtime-setup.exe` | 不附带或执行上述安装程序；须预先满足运行库要求 |

两种包都包含完整 Core、TSF、Settings 和原生安装 helper。Settings 继续使用 framework-dependent Windows App SDK 1.8，要求当前用户有 x64 Framework/DDLM >= `8000.994.2142.0` 和 Visual C++ 运行库。不含运行库版本仍执行 Settings `--runtime-check`，缺少依赖时用户初始化不能成功完成。

安装器配置为 `SetupArchitecture=x64`、`ArchitecturesAllowed=x64os`、64 位安装模式、`MinVersion=10.0.22000` 和 `PrivilegesRequired=admin`，使用固定 AppId。不声明 Windows 10 或 ARM64 仿真安装支持。x64 TSF DLL 无法加载到 x86 应用，详见 [TSF 64 位平台说明](https://learn.microsoft.com/en-us/windows/win32/tsf/64-bit-platform-considerations)。

## 实现入口

| 位置 | 当前职责 |
| --- | --- |
| `scripts/build-and-deploy.ps1` | 调度 Core/libime/addons、TSF/helper 和独立 MSBuild Settings 工程，测试并部署完整开发运行树 |
| `scripts/build-installer.ps1`、`installer/fcitx5.iss` | 校验打包输入、复制许可证、标记受管理安装、生成双版本 EXE/manifest/SHA256，编排安装与卸载 |
| `cmake/installer-lock.json` | 记录参考 Inno 版本及官方运行库下载地址、版本、SHA256 和发布者 |
| `win32/setup/main.cpp`、`userprofile.cpp` | 注册归属、权限、占用检查、用户输入项备份/恢复、启用/激活、Core 就绪及卸载 launcher |
| `win32/setup/log.h` | 安装 helper 的 UTF-8 诊断日志 |
| `win32/dll/register.cpp`、`main.cpp` | COM/profile/category 注册与注销、失败结果传递 |
| `win32/ipc/installation.h` | 受管理安装识别与服务启动门控 |
| `.github/workflows/ci.yml` | 一次 x64 联合构建，独立测试/打包 job，Nightly 发布 |

`RegisterServerAt()` 已逐步检查注册表调用、可靠关闭句柄并为 REG_SZ 写入终止符；DLL 路径使用 32768 字符缓冲区。`DllUnregisterServer()` 已返回 profile/category 或 COM 清理失败结果。不能继续把旧的 `MAX_PATH`、无效句柄和无条件 `S_OK` 描述为当前代码。

## 安装树与状态

```text
<ProgramFiles>/Fcitx5/
  bin/Fcitx5.exe + Core/MSYS2 运行 DLL
  lib/fcitx5/ + lib/libime/zh_CN.lm
  share/fcitx5/ + share/libime/sc.dict
  tsf/fcitx5-x86_64.dll
  settings/Fcitx5Settings.exe + DLL/PRI/WinMD
  setup/Fcitx5SetupHelper.exe
  setup/managed-install + install-state
  setup/WindowsAppRuntimeInstall-x64.exe  （仅含运行库版）
  licenses/
  unins*.exe + 卸载记录
```

Core 保持独立进程和 MSYS2 Windows GNU 运行库；TSF/helper 使用 Windows SDK/ATL，Settings 使用独立 MSBuild 工程。Core 与 TSF/Settings 只通过 Named Pipe 交换协议数据，不跨进程传递 C++ 对象或分配器。安装路径仍符合 `bin`、`tsf`、`settings` 相邻目录的定位约定。

当前机器状态存于安装树 `setup/install-state` 和 `HKLM\SOFTWARE\Fcitx5\Installer`，不是 ProgramData 清理计划。实际状态值为 `installed`、`maintenance`、`pending`。受管理安装由 `setup/managed-install` 或 HKLM 安装根识别，只有完整有效的 `installed` 文件允许服务启动；状态缺失/损坏时拒绝启动。未标记的隔离开发树保留原有启动行为。

TSF 启动路径及 Core/Settings 主循环检查维护状态。用户级 `.state` 仍只表达 SID/session/logon 的手动关闭意图，不能代替机器安装状态。旧版本已加载 DLL 若不认识维护状态，仍需要关闭相关应用或注销/重启来释放。

用户设置、词库和学习历史位于 `%APPDATA%/Fcitx5`，不由安装器覆盖或删除。用户输入项备份和 helper 日志位于该用户 LocalAppData。当前安装器直接打包给定完整树，尚未生成最小运行文件清单及完整依赖源码/许可清单；正式发行前仍需核对全部捆绑依赖的交付要求。

## Helper 与权限

`Fcitx5SetupHelper.exe` 复用注册与服务控制代码，不要求用户运行开发 PowerShell 脚本。实际命令如下：

| 命令 | 执行身份与行为 |
| --- | --- |
| `preflight-machine` | 检查冲突注册及安装根下待重启删除的文件 |
| `register-machine` / `unregister-machine` | 管理员注册/注销，核对当前 COM DLL 路径归属；DLL 缺失时支持有限注销清理 |
| `configure-user` | 普通权限验证 runtime、备份/启用输入项、首次设置默认并激活、重启 Core 和等待就绪 |
| `rollback-user` / `prepare-uninstall-user` | 普通权限恢复必要的用户偏好、移除输入项并正常停止自己的服务 |
| `pause-user` / `resume-user` | 普通权限执行维护前用户处理及恢复 |
| `set-maintenance` / `clear-maintenance` / `mark-pending` | 管理员写入 `maintenance` / `installed` / `pending` |
| `inspect-locks` | 用 Restart Manager 查询安装树的 EXE/DLL 占用，返回错误或占用状态 |
| `uninstall` | 普通权限处理用户阶段，再提权执行机器注销和原生卸载器；支持 `--silent` |

命令支持 `--prefix` 和无系统变更的 `--what-if`。尚无 `cleanup-pending` 命令、临时 SYSTEM 清理任务或用户代理握手协议。

安装使用 [`ExecAsOriginalUser`](https://jrsoftware.org/ishelp/topic_isxfunc_execasoriginaluser.htm) 调用用户阶段；普通权限 helper 拒绝以管理员身份执行用户配置。普通双击 + UAC 是预期路径，标准用户由其他管理员代输凭据、从已提升终端运行等身份分支仍需实测。`/MACHINEONLY=1` 显式跳过用户初始化，结束提示会说明尚未配置用户；当前没有自动登录初始化。

## 当前安装与升级流程

1. 已有 helper 时执行 `preflight-machine`，拒绝冲突注册或安装根下仍有待重启删除的文件。已有受管理安装时先处理用户服务、写入维护状态并检查占用；占用或检查失败时退出并尝试恢复。
2. 含运行库版安装 VC++ 先决条件，接受成功、待重启及已装其他版本的对应返回码，然后复制完整运行树。安装器设置 `CloseApplications=no`、`RestartApplications=no`，不自动结束第三方应用。
3. 从最终路径注册 TSF，写入 `installed` 状态。含运行库版执行 Windows App Runtime 安装，随后通过原始用户的 `configure-user` 验证实际 bootstrap；必要时在该用户身份下重试运行库安装。
4. `configureUser()` 首次保存旧默认输入项及启用状态，调用 `InstallLayoutOrTip` 启用 Fcitx5；首次初始化才设置默认并激活，后续保留用户后来选择的默认项。已有备份时先读取验证。
5. 正常重启 Core 并轮询 `GetSettings` 成功，记录就绪。此检查不等同于已验证真实应用的中文提交或完整引擎资源。
6. 用户配置或后续步骤失败时尝试 `rollback-user`、注销并标记 pending。现有补偿不等同于完整文件/注册事务，升级失败及中途崩溃的恢复仍需验收。

`InstallLayoutOrTip` 使用既有简体中文 profile 描述，不使用会禁用其他输入项的 `ILOT_CLEANINSTALL`，也不配置登录屏幕。首次激活记录 `ChangeCurrentLanguage` 和 `ActivateProfile` 的实际结果；不能推断非中文系统、每应用输入模式和已有应用窗口必然立即切换。API 参考：[InstallLayoutOrTip](https://learn.microsoft.com/en-us/windows/win32/tsf/installlayoutortip)、[SetDefaultLayoutOrTip](https://learn.microsoft.com/en-us/windows/win32/tsf/setdefaultlayoutortip)、[ActivateProfile](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-activateprofile)。

固定目录升级遇到 DLL 占用会拒绝覆盖；当前没有整套 IPC 端点的重启替换方案。helper 会扫描 `PendingFileRenameOperations` 中安装根下的条目；若被预检发现则返回 `3010`。完整卸载后旧 helper 不再存在时的同路径重装保护仍需系统验收，不能仅凭 helper 单独检查宣称全部重装分支已经安全。

## 卸载与 DLL 占用

开始菜单和 Windows 应用卸载项指向 `Fcitx5SetupHelper.exe uninstall`。普通权限入口处理当前用户输入偏好及服务后再提权；仅在当前默认仍为 Fcitx5 时恢复原项或选择有效替代项。管理员阶段校验安装归属、写维护状态、查询占用、注销，然后运行 Inno 原生卸载器；原生卸载成功或返回 `3010` 后，helper 还会删除安装后动态创建的 `setup/install-state`。

传给 Inno 的参数为 `/NORESTART /HELPER=1`；静默时另加 `/VERYSILENT /SUPPRESSMSGBOXES`。`/HELPER=1` 避免原生卸载器重复维护/注销。文件条目使用 `uninsrestartdelete`，被占用时可能延迟到重启删除。launcher 在占用且原生卸载成功时返回 `ERROR_SUCCESS_REBOOT_REQUIRED`（`3010`），保留该返回码且不弹失败提示。不会调用 `RmShutdown`、结束 Explorer/ctfmon 或按进程名杀用户应用。

**注销登录可以释放当前会话的 DLL，但不会执行重启删除队列；其他会话也可能仍在占用。** 重启清理由 Inno/Windows 负责，真实占用检测、提示和重启后的删除仍需验收。当前没有“只注销即可自动彻底清理”的能力；若需要，须另行设计管理员保护的精确文件计划与临时清理任务。相关机制见 [Restart Manager](https://learn.microsoft.com/en-us/windows/win32/rstmgr/functions) 和 [Inno uninsrestartdelete](https://jrsoftware.org/ishelp/topic_filessection.htm)。

用户词典与学习数据、共享 VC++/Windows App Runtime 保留。开发用 `install.ps1`/`uninstall.ps1` 只注册/注销 DLL，不管理正式安装文件；正式安装应使用上述卸载入口。

## 构建与 CI

本地命令和打包参数见 [构建安装包](build.md#build-installer-packages)。打包使用独立完整暂存树，`build-installer.ps1` 会复制许可证并写入 `setup/managed-install`，不能对正在使用的开发运行树执行。它不注册、不启动服务、不执行运行库安装；`-WhatIf` 只验证必要输入并预览。

依赖锁记录参考 Inno `7.1.0`；脚本和 CI 实际校验主版本为 7，并记录实际版本。CI 当前通过 winget 安装 Inno Setup 7，未使用锁文件的 Inno 安装程序 SHA256 来固定编译器补丁版本。CI 将检查过的完整路径传给两次 `-IsccPath`，避免 PATH 中旧版 6 被误选。

| Job | 当前流程 |
| --- | --- |
| `build-core-arm64` | 仅检查 ARM64 Core 编译 |
| `build-x64` | 统一 Release 构建一次，上传完整运行树、便携 TSF 测试/probes 及独立 Settings artifact |
| `test-x64` | 复用运行树和测试包，执行脚本回归、图标校验、默认 CTest、拼音/TSF/settings probes 及词库验证 |
| `package-installer-x64` | 与测试并行复用运行树，下载并核对运行库 SHA256，执行模拟打包回归，构建两种安装包并核对文件存在 |
| `release` | 等待构建、测试、打包成功，在 master 且提交消息不含 `!release` 时用 GitHub CLI 更新 `latest` 预发布 |

`Fcitx5-installer-x64` artifact 包含两种 EXE、各自 SHA256 和 manifest，共六个文件。Nightly 只发布两个 EXE 和各自 SHA256，共四个附件；不发布 manifest、Core 开发包或内部运行树/测试 artifact。PR 只上传 artifact，不发布 Nightly。

当前 CI 没有代码签名步骤。`installer-smoke-x64` 在全新的 Windows runner 上执行不含运行库包的静默机器级安装/卸载，核对安装文件、HKLM 安装状态和卸载清理；它不安装 VC++/Windows App Runtime，也不执行原始用户配置。含运行库包、完整用户初始化、升级及重启后的延迟删除仍需干净 Windows VM 或 self-hosted runner 验证，不能把 smoke job 作为这些场景的验收。完整产物流向见 [CI 构建与产物](build.md#ci-build-and-artifact-flow)。

## 已记录的验证

- 两种包已由本机 Inno Setup 7.1.0 实际编译，清单和 SHA256 核对通过；不含运行库版的预处理/编译日志确认没有运行库文件条目及安装步骤。产物和日志记录在 `dist/installer-variants`、`build/installer-variants` 及 `build/ci-inno7-validation`。
- `scripts/test-build-installer.ps1` 使用模拟编译器覆盖双版本、缺失依赖、版本选择、独立元数据、失败返回和无副作用预览，不执行系统安装。
- `test_setup` 覆盖用户输入项和安装维护相关回归；`test_uninstall` 运行生产入口和 launcher，通过模拟系统调用覆盖普通/提权、占用、`3010`、失败、取消及静默路径。当前默认 CTest 共 13 项，历史较少测试的通过记录仍只代表当时集合。
- helper 日志位于执行用户的 `%LOCALAPPDATA%/fcitx5/installer-helper.log`，包含本地时间、PID/TID、命令/安装根、runtime-check、启用/默认设置、语言切换、激活原始 HRESULT 和 Core 就绪步骤。日志失败不改变返回码并恢复 `GetLastError()`；用于定位已记录的用户配置 `0x80004005`，具体失败调用仍待复现确认。

以上是既有验证记录，本次文档核对未执行真实安装、注册/注销、用户切换或服务控制。

## 待验收与后续设计

| 场景 | 待验证结果 |
| --- | --- |
| 干净 Windows 11 x64，无开发工具/runtime | 含运行库包完成依赖、注册、用户启用；真实应用中文提交并打开 Settings |
| 不含运行库包 | 已装依赖时成功，缺失依赖时准确报错并补偿 |
| 普通管理员双击、标准用户由另一管理员代输 UAC | 原用户获得初始化，Core 保持普通权限；已提升终端及机器阶段分支明确 |
| 非中文系统、每应用输入模式、已有应用窗口 | 记录默认/激活行为及需重开或重新登录的实际限制 |
| 重装、升级、取消、配置失败、中途崩溃 | 无半套 IPC 文件，注册/偏好/服务恢复可诊断，pending 后同路径重装受保护 |
| DLL 被应用占用，多用户/RDP 会话 | 不结束用户应用；注销归属正确，延迟删除登记和重启后清理可核对 |
| 用户改变默认、已有词库及学习历史 | 升级/卸载保留后续选择与用户数据 |
| 静默、SYSTEM/无交互身份 | 退出码区分失败、待用户初始化及待重启，无弹窗阻塞 |
| x86 TSF（后续实现） | 32/64 位真实应用均加载正确 DLL 并提交中文 |

后续应补充隔离 runner/VM 的真实安装测试、签名及完整发行依赖/源码清单。GitHub hosted runner 的开发运行库环境不能替代干净交互 VM，无法重启时也只能报告延迟删除登记。未完成这些验收前，不将说明改为“安装后可直接使用”。
