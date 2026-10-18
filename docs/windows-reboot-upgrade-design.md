# Windows 输入法占用时的延迟升级设计

状态：延迟升级实现已接入，默认发行包启用，CI/Nightly 使用相同默认值。本文规定“完整暂存新版，重启前继续使用旧版”的实现方案；打包时可使用 `scripts/build-installer.ps1 -RebootUpgrade:$false` 关闭受管理升级的暂存分支。文件/映像保护、状态机、恢复、卸载清单和真实 Inno 编译有自动化覆盖；SYSTEM 开机时序、完整安装与系统重启仍需 VM 验收，不能据此声明已经通过产品验收。现有支持范围见 [Windows 安装包实现与设计](windows-installer-design.md)。

## 实现入口与试验包

维护程序为 `setup/maintenance-host/Fcitx5Upgrade.exe`，运行时文件所有权位于 `setup/runtime-manifest.json`。一次只允许一个 `setup/upgrades/current` 事务，其随机 ID 同时绑定开机任务。输入协议不变，旧运行清单允许记录历史协议版本，目标清单必须与新维护程序的协议版本相符。

```powershell
./scripts/build-installer.ps1 -Prefix ./dist/installer-stage -Output ./dist/installer -SkipRuntimes
./scripts/test-build-installer.ps1
./scripts/test-reboot-upgrade-package.ps1 -TsfBuild ./build/installer/tsf
ctest --test-dir ./build/installer/tsf --output-on-failure
```

`test-reboot-upgrade-package.ps1` 仅编译带测试载荷的两种默认安装包和显式关闭延迟升级的安装包，不执行安装程序。安装后的只读查询可使用以下入口；不带 `--silent` 时显示状态对话框：

```powershell
& 'C:/Program Files/Fcitx5/setup/maintenance-host/Fcitx5Upgrade.exe' query-upgrade --prefix 'C:/Program Files/Fcitx5'
```

`cancel-upgrade`、`recover-upgrade` 和 `cleanup-upgrade` 为提权维护操作。恢复不绕过文件保护，手动调用也不能跳过等待完整重启的限制。试验包不会在静默暂存成功后主动重启 Windows。

## 1. 目标与范围

输入法 DLL 被应用加载时，安装器允许准备升级，不要求用户先关闭所有应用。准备成功后提示：

> 升级已准备完成。重启 Windows 后完成升级；重启前可以继续使用当前版本。

基本约束：

- 等待重启期间，活动安装树的 Core、TSF、Settings、引擎和资源保持旧版，旧版服务不停止、不进入维护状态。
- 提交时整体更新运行树；不将新版 Core 与仍在运行的旧版 TSF DLL 组合使用。
- 不结束第三方应用，不强制卸载其他进程中的 DLL，不自动注销或重启 Windows。
- 保留用户设置、词典、学习历史、默认输入项、输入法启用状态及登录启动选择。
- 初版面向 Windows 11 x64、同一安装目录、相同 CLSID/profile 的受管理安装升级。不承诺降级、迁移目录或多版本同时运行。
- 首次安装继续走现有流程。旧版已损坏、处于卸载待重启状态、注册属于其他目录时，先修复或完成卸载，不进入延迟升级。

“准备完成”与“新版已生效”是两个独立结果。正常目标是一次 Windows 重启后完成；若开机时仍被占用或出现其他错误，必须报告未完成，不能仅凭已经发生重启宣称成功。

## 2. 当前实现与需要调整的边界

| 入口 | 当前行为 | 本方案要求 |
| --- | --- | --- |
| `installer/fcitx5.iss` / `PrepareToInstall` | `pause-user`、写维护状态、检查占用，占用时退出 | 在任何暂停或维护动作之前决定升级路径；延迟路径只暂存 |
| `installer/fcitx5.iss` / `[Files]` | 直接写 `{app}` | 延迟路径不能执行活动文件复制，也不能提前更新旧卸载记录 |
| `CurStepChanged(ssPostInstall)` | 注册 DLL、恢复安装状态、配置用户并启动 Core | 延迟路径不执行这些步骤；机器提交移至重启阶段 |
| `win32/setup/main.cpp` | helper 命令、Restart Manager 检查、注册及卸载 | 增加升级协调入口和安装级互斥；检查错误与确实占用分开处理 |
| `win32/ipc/installation.h` | `install-state` 为 `installed` 才允许受管理安装启动 | 保留旧状态格式；新增升级事务状态，等待重启时不改变启动许可 |
| `win32/setup/userprofile.cpp` | 用户初始化会启用输入项并重启 Core；暂停还会调整输入项 | 升级不复用 `pause-user` / `configure-user` 来表达文件更新 |
| `scripts/build-installer.ps1` | 包级 manifest、SHA256 与完整树打包 | 另生成运行文件清单、升级控制程序和可恢复的卸载所有权清单 |

Core、TSF 和 Settings 仍通过现有 Named Pipe 协议通信。本方案不修改输入协议，不引入新旧版本的协议兼容承诺，也不改变三个工程的工具链和运行库边界。

## 3. 方案选择

采用“受保护暂存目录 + 临时 SYSTEM 开机任务 + 原生提交程序”。开机程序先建立维护门控并确认活动文件可替换，再提交完整新版，最后验证和注册。

暂存程序不对活动运行文件登记逐文件 `restartreplace`，也不向 `PendingFileRenameOperations` 排入整套升级。原因是逐文件队列没有整套成功或回滚语义；部分登记失败、部分执行失败以及撤销时与其他软件共享队列的协调，都需要额外恢复机制。现有 `uninsrestartdelete` 可继续用于卸载清理，但不能代替本方案的升级事务。

开机计划任务不是“早于所有登录程序运行”的保证。任务执行较晚时，旧 Core 或旧 TSF DLL 可能已经重新加载。因此本方案将以下条件作为提交前提：

1. 已成功建立维护门控；门控写入失败时不修改运行文件。
2. 已确认所有会变更的运行文件没有活动映像或阻止替换的引用。
3. 已取得覆盖检查到提交期间的文件保护，不能在一次 Restart Manager 查询后直接开始复制。

任一条件不成立且尚未修改运行文件时，恢复旧版 `installed`，保留暂存并返回“本次未完成，仍需重启”。不强行取得进程控制权。

这个选择优先保证旧版完整和失败可恢复。能否在典型登录顺序下稳定地一次重启完成，是第 13 节原型阶段的发布门槛；未通过时不能仅靠增加固定 `Sleep` 交付。

## 4. 目录与文件所有权

活动布局继续保持 `bin`、`tsf`、`settings`、`lib` 和 `share` 相邻，不增加运行时版本目录或 junction。新增控制目录示意：

```text
<ProgramFiles>/Fcitx5/
  bin/ lib/ share/ tsf/ settings/ licenses/   # 当前活动版本
  setup/
    Fcitx5SetupHelper.exe                    # 当前活动 helper
    managed-install
    install-state                           # 继续使用现有三种值
    runtime-manifest.json                    # 当前活动文件所有权与摘要
    maintenance-host/
      Fcitx5Upgrade.exe                      # 独立升级/恢复入口
    upgrades/<transaction-id>/
      plan.json                             # 不可变计划
      journal.json                          # 可恢复进度
      payload/                              # 完整新版运行树
      backup/                               # 提交前完成的旧版备份
      retired/                              # 替换时保留的旧文件对象与准备文件
      dependencies/                         # 可选官方运行库安装程序
      result.json
  unins*.exe + 对应 Inno 卸载数据             # 不作为运行 payload 替换
```

`Fcitx5Upgrade.exe` 从现有 `win32` 原生工具链构建，静态链接 MSVC CRT，只依赖系统 DLL。它运行于活动 payload 之外，提交时不覆盖自身。首版可复用 helper 的日志、注册和权限检查代码，但独立入口用于避免替换正在运行的 helper。

维护入口有独立版本与 schema 兼容范围。尚有未完成事务时不能覆盖它；更新维护入口必须在事务结束后或使用新的受保护路径，不能从命令行指定任意可执行文件作为 SYSTEM 任务动作。

文件所有权分为三类：运行 manifest 管理的活动文件、Inno 管理的安装基础文件、升级器管理的事务文件。三者清单分别保存。`%APPDATA%`、用户备份和共享运行库不属于这些删除清单。

## 5. 清单、计划与权限

### 5.1 运行文件清单

使用带 `schemaVersion` 的 UTF-8 JSON，通过结构化解析器读取，不用字符串切分解析路径和数据。原生解析器优先复用工程已有依赖，分别在原生目标内构建。

运行清单至少包含包标识、四段版本、架构、输入协议版本、支持的维护 schema，以及每个文件的相对路径、大小、SHA256 和组件类别。输入协议版本用于校验整套产物，不用于允许旧端点连接新版 Core。

包级 `.manifest.txt` 和安装包 `.sha256` 保持现有发布用途，不把它们误作逐文件运行清单。构建阶段排除 `install-state`、事务目录、动态日志、Inno 卸载程序及临时文件。

旧包没有运行清单时，实验实现面向标准、未手工改动的受管理安装：备份并替换新版清单对应的既有产品文件，不自动删除清单之外的旧文件。无法仅凭旧 Inno 安装识别与新版产品路径重名的手工文件；此类安装树不在初版迁移支持范围，正式发布前需核对历史包与显式迁移规则。有运行清单时，新版新增路径若已存在且不属于旧所有权清单，报冲突并保留旧版。

### 5.2 事务计划与日志

`plan.json` 至少记录：

- 随机事务 ID、schema、安装根标识、来源包与目标包标识、清单摘要。
- 源/目标架构与版本、支持的旧版范围、固定的替换和移除相对路径。
- 依赖策略、维护入口版本、发起用户 SID、是否 machine-only。
- 原安装注册归属、安装基础信息和卸载入口的必要快照。

`journal.json` 记录阶段和错误码，`inventory.json` 固定整批文件的旧存在状态与备份摘要。完整备份、注册快照和 inventory 持久化后，才写入 `applying` 并修改第一个活动文件；未到提交点的中断统一恢复整批，不依赖逐文件完成计数。元数据写入使用同目录临时文件加替换并刷盘，恢复同时验证实际摘要，不能仅凭阶段名称推断操作已经落盘。

准备阶段记录安装器父进程 PID 和创建时间，跨 helper 调用保留事务所有权；原安装器仍在复制时，其他进程不能取消或接管该事务。单次命令仍持有安装锁，PID 重用不作为原安装器仍存活的证据。

### 5.3 安全约束

- 安装根、维护入口、暂存、备份、JSON 和计划任务仅允许 SYSTEM、Administrators 或 Windows TrustedInstaller 服务写入；普通用户只读必要状态，不能修改任务或计划。
- 提交前重新验证 ACL、安装归属、文件摘要和路径。SHA256 用于完整性校验，不单独构成发布者认证；正式签名策略仍按发行要求实施。
- 限定本地 NTFS、同卷暂存与提交。拒绝 UNC、可移动目标、不受保护目录和路径组件中的 reparse point；处理硬链接与文件身份，不能通过链接写出安装根。
- 相对路径拒绝绝对路径、`..`、ADS、设备名、大小写碰撞和重复项。操作通过已验证的目录/文件句柄执行，不能在检查后重新跟随可变链接。
- 计划中不保存任意 shell 命令。SYSTEM 阶段不执行用户提供的 PowerShell、不访问用户配置、不启动 Core 或 Settings。
- 设置磁盘空间、文件数量、单文件及累计大小上限；同时为完整旧版备份和提交临时文件预留空间。
- 安装、升级、恢复和卸载共用带受保护 ACL 的机器级安装互斥。状态与文件句柄仍是正确性依据，不能只依赖命名互斥避免竞争。

## 6. 状态模型

不将 `upgrade-staged` 等新字符串写入现有 `install-state`。旧版门控只接受 `installed`，直接扩展此文件会导致暂存成功后旧版立即不可用。

| 事务状态 | `install-state` | 活动版本 | 允许的下一步 |
| --- | --- | --- | --- |
| 无事务 | `installed` | 当前版 | 安装、升级或卸载 |
| `preparing` | `installed` | 旧版 | 完成校验，或取消并清理 |
| `staged` | `installed` | 旧版 | 等待重启、取消、查询 |
| `cancelling` | `installed` | 旧版 | 幂等撤销任务、控制入口及暂存 |
| `validating` | `maintenance` | 完整旧版 | 取得替换条件，或恢复旧版并等待 |
| `applying` | `maintenance` | 可能处于变更中 | 只允许继续提交或恢复 |
| `verifying` | `maintenance` | 新版文件已写入 | 校验、注册，或回滚 |
| `committed` | `installed` | 完整新版 | 清理事务 |
| `rolling-back` | `maintenance` | 可能处于变更中 | 恢复完整旧版 |
| `rolled-back` | `installed` | 完整旧版 | 展示失败原因，允许重新准备 |
| `recovery-required` | `pending` | 未确认完整 | 仅允许修复/恢复 |

`staged` 是独立升级状态，不复用卸载/失败含义的 `pending`。注册表的安装 `State` 镜像按现有约定同步；启动许可仍以实际门控文件为准。恢复必须处理文件与注册表尚未同时更新的情况。

旧版服务不知道升级状态也没有关系：等待期间它看到的仍是旧文件和 `installed`。一旦开始提交，必须一直保持门控关闭，直到一整套文件、注册和文件所有权记录全部验证成功。

## 7. 准备阶段

1. 新安装器从自己的可信载荷启动新版维护程序，取得安装互斥。预检冲突注册、旧事务和安装根下其他待重启删除/替换条目，不依赖活动旧 helper 是否存在或是否认识新命令。
2. 区分首次安装、受管理升级及修复。升级的路径选择发生在 `pause-user` 和 `set-maintenance` 之前。确认第三方占用时进入延迟路径；占用查询失败是错误，不能当作无占用继续。
3. 初版受管理版本升级统一走暂存路径，以免为了探测即时升级而先中断旧版。若后续增加即时路径，也必须复用相同的提交保护与恢复机制，不能在复制一半后临时降级为暂存。
4. 完整解包新版到事务目录，校验清单、文件、空间和依赖。此时不执行暂存的 Core、TSF 或 Settings，不加载暂存 DLL 做注册。
5. 记录原有用户和机器选择，但不调用 `pause-user`、`configure-user`、`rollback-user`、输入项启用/移除或服务重启。升级期间用户继续修改的偏好也必须保留，不能在重启后回写准备时的旧快照。
6. 安装/验证固定维护入口及开机任务，建立受保护的本次开机标识，发布 `staged`。只有载荷完整、恢复入口可执行、任务可查询且计划已持久化，才能返回准备成功。
7. 显示待重启提示并返回 `3010`。推迟重启、再次打开 Settings、启动新应用或旧 Core 正常重启，均继续使用完整旧版。

准备阶段可新增维护目录及卸载协调入口，但不更新 `DisplayVersion`、COM/profile、活动 helper 和原生 Inno 卸载数据。界面可单独显示待安装版本；已安装版本继续代表当前活动版本。

### 7.1 Inno 集成

沿用单个公开安装 EXE，在脚本中设置固定的 `FreshInstall` / `StageUpgrade` 模式。模式在系统变更前，根据已验证安装根确定，后续不允许自动切换。首次安装保持现有活动目录复制与用户配置；受管理升级使用以下分支：

- `Uninstallable` 和 `CreateUninstallRegKey` 使用 scripted boolean expression，仅首次安装返回 True，避免生成或合并卸载日志以及自动更新产品版本号。不是通过伪造另一个 AppId 隔离。
- 从安装包临时提取原生维护程序，调用内部 `begin-stage` 预检并创建受保护事务目录。`[Files]` 的目标由脚本函数返回，升级时只指向该事务的 `payload`，首次安装才指向 `{app}`；两种目标不可混用。
- 普通 `[Icons]`、`[Registry]`、运行库安装及用户 post-install 步骤增加首次安装条件。待升级卸载入口的必要改动由维护程序按日志单独管理。
- 文件复制完成后调用内部 `seal-stage` 再次全量校验，发布维护入口、任务和 `staged`。`begin-stage` 成功不代表升级准备成功；复制失败、取消或缺少 seal 时按 `preparing` 残留恢复。
- `DeinitializeSetup` 不能在延迟路径调用旧 `ResumeInstallation`，也不能清理已经 seal 的事务。Inno 对未完成安装的自动清理与维护程序显式清理必须幂等，且只影响本事务。

Inno 支持上述两个设置使用脚本布尔表达式，见 [Uninstallable](https://jrsoftware.org/ishelp/topic_setup_uninstallable.htm) 和 [CreateUninstallRegKey](https://jrsoftware.org/ishelp/topic_setup_createuninstallregkey.htm)。仍需真实包验证：不会生成第二个产品卸载项、不会提前合并旧 `unins*.dat`、不会把事务目录当作活动安装目录。不能用 `PrepareToInstall` 返回错误字符串来表示暂存成功。

通过 `NeedRestart` 表达准备成功后的重启需求，并显式处理退出码：普通成功为 `0`，暂存成功为 `3010`，预检/准备失败保留非成功码。不能假定 Inno 默认在所有参数组合下返回 `3010`；需结合 `GetCustomSetupExitCode` 和 `/RESTARTEXITCODE=3010` 做真实包测试。

自动化统一使用 `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /RESTARTEXITCODE=3010`。交互界面仅在用户明确选择立即重启后触发重启；维护程序自身不发起重启。见 [Inno 命令行参数](https://jrsoftware.org/ishelp/topic_setupcmdline.htm) 与 [脚本事件](https://jrsoftware.org/ishelp/topic_scriptevents.htm)。

## 8. 重启后的提交

### 8.1 任务与重启识别

任务使用 Task Scheduler COM API 注册，固定任务目录/名称，以 SYSTEM、非交互方式执行，BootTrigger 不配置人为延迟，也不依赖网络、空闲或交流电。启用有界重试和单实例运行；任务权限、超时及失败重试均在测试中核对。

任务动作只调用受保护的 `Fcitx5Upgrade.exe complete-upgrade --transaction <id>`；安装根和载荷位置从受保护计划读取。任务不要求用户选中“登录启动 Fcitx5”，不因此新增长期 Core 登录启动项。

准备时在受保护的 HKLM 专用子键创建 `REG_OPTION_VOLATILE` 的事务标识。标识还在时，本次开机内的误触发、计划任务重试或手动调用只返回待重启，不提交。标识消失且持久计划匹配时才进入启动后的校验；读取失败不等于标识不存在。恢复中断的 `applying` 事务则以恢复日志为依据，不受准备阶段的同次开机限制。

volatile 标识不使用墙上时间或 uptime 猜测重启。休眠、睡眠、注销和快速启动关机不承诺等价于完整重启，产品提示统一要求使用 Windows 的“重启”。注册表文档说明 volatile 数据在 hive 卸载时消失，并指出快速启动关机的区别；实际行为必须通过 VM 验证，见 [RegCreateKeyExW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexw)。

### 8.2 取得提交条件

1. 取得安装互斥，检查事务、版本、安装根身份、载荷摘要及第三方待重启操作。
2. 将活动 `install-state` 原子写为 `maintenance` 并验证；写入失败立即退出。现有 Core/Settings 门控协助退出，但不假定 DLL 因此卸载，也不向用户服务写手动停止状态。
3. 使用扩展后的 `inspect-locks` 检查所有相关 EXE/DLL，排除维护程序自身；按精确安装路径识别所有会话中的占用。对加载了本安装 DLL 的用户应用只报告，不操作其生命周期。
4. 有界等待后仍有旧映像或阻止替换的引用，且未修改运行文件时，恢复旧版 `installed`，重新建立本次开机标识，回到 `staged`。本次开机内不继续反复中断旧服务。
5. 对变更/移除的活动文件取得并持续持有提交保护，再复核映像与文件身份；保护取得不完整时释放全部保护并按上一步处理。

Restart Manager 结果只提供占用信息，不提供锁。提交保护需要通过 Windows 文件句柄验证实际的写入/重命名能力，并阻止新的冲突打开；应使用现有系统 API，而不是“先查询，再关闭句柄，再按路径覆盖”。`CreateFileW` 的独占共享模式及 `SetFileInformationByHandle` 可用于原型，但“获得句柄”不能自动推导为不存在已有 image section。映像占用、句柄内重命名、目录身份与后续发布的组合必须单独 probe。

### 8.3 文件、注册与提交点

1. 在维护门控关闭且替换条件成立后，完成旧版受管文件备份和注册元数据快照。运行资源可能在准备到重启之间变化，因此旧版备份在此时生成；不备份或回滚用户数据。
2. 使用同卷 `payload` 中已校验的文件作为源，inventory 记录整批替换/新增/移除意图。持有全部既有目标的独占写句柄，在 `retired` 准备并刷盘完整新文件，通过句柄将旧文件移入该目录，再把新文件发布到最终路径。新增目标使用 `CREATE_NEW`。保存/恢复文件 DACL，避免从暂存目录带入缺少 AppContainer 读取权限的 ACL。
3. 替换 Core、TSF、Settings、引擎、翻译、资源和活动 helper。废弃文件按旧所有权清单或显式迁移清单移除，未知文件保留。
4. 新版文件发布时必须保持完整的“尚未允许运行”条件。若原有 `InstallationGate` 只在部分入口检查，需补齐新版进程入口、TSF COM 激活和服务启动入口；不能指望轮询延迟阻止加载期间的全部副作用。
5. 全量验证活动文件大小和摘要，确认没有剩余旧运行映像。维护程序不再依赖旧 helper，使用已校验新版 DLL 的最终路径更新 COM/profile/category，并验证双向注册、图标路径与 threading model。
6. 更新活动 manifest、卸载所有权及机器版本元数据，写入可恢复的提交记录。`installed` 是最后发布的运行许可；中途出错继续保持维护状态。
7. 发布 `installed`，标记 `committed`，写结果并清理任务及暂存。清理失败不回滚已成功升级的版本，记录为可重试的清理工作。

写入保护会拒绝已加载的 PE image section，但只读数据映射不一定阻止取得写句柄。因此不原地覆盖旧文件：Explorer 图标等既有数据映射继续引用完整旧对象，新路径发布完整新版。旧对象无法立即删除时保留在 `retired`，保持终态 journal 和任务，重启后重试清理。重数据清理完成后才撤销任务，再将事务目录整体改名为 `completed-<id>` 后删除；中断留下的完成目录可独立回收，不能把它重新当作待提交事务。

这是带门控和日志的多步提交，不是文件系统/注册表的整体原子事务。每一个发布步骤都要有恢复依据；只在进程内捕获异常并复制回文件不算断电恢复。

若在发布 `installed` 后、写入 `committed` 前中断，恢复先核对已持久化的提交记录、活动 manifest 和注册。确认完整新版已被放行时，补记提交并清理，不能因 journal 尚停在 `verifying` 就回滚可能已经运行的新版。提交前的失败恢复则必须同时恢复文件、注册、manifest、版本元数据和卸载所有权。

SYSTEM 阶段只做机器安装，不启动 SYSTEM Core，不调用用户 `configure-user`。机器提交成功表示文件和注册就绪，不等同于真实应用输入验收。

## 9. 用户状态与运行库

同 CLSID/profile 升级保留原有用户初始化结果，无需重新启用、激活输入法或恢复默认项。正常登录或 TSF 激活继续按既有逻辑启动新版 Core；未选登录启动的用户不新增 Run 项，machine-only 也不创建用户初始化任务。

本次升级不改变语言、日志 revision、用户输入法顺序或已有手动停止意图。若后续版本确实需要用户态迁移，需另定义幂等普通权限迁移，不能在 SYSTEM 下用某个 HKCU 冒充原用户。

含运行库包在暂存目录保存已验证的官方安装程序。准备阶段可进行只读依赖检查；初版不为文件暂存提前运行共享运行库安装。提交阶段在维护门控下安装必要机器依赖，接受经过明确分类的返回码；额外要求重启时保留待完成状态，不提前发布新版运行许可。

SYSTEM 下安装共享运行库不证明每个用户已具备 WinUI bootstrap 条件。现有用户的 Settings 运行库检查保持普通权限；缺失时提示依赖修复，不回滚已经验证成功的 Core/TSF。若新版引入必须的用户注册步骤，则该版本不能直接套用本方案，必须补充用户迁移设计和验收。

不含运行库包不下载或安装运行库。缺少提交所必需的机器依赖时，在修改活动运行文件前报告失败；共享 VC++/Windows App Runtime 不作为产品回滚时的卸载对象。

## 10. 失败、取消、重复安装与卸载

| 场景 | 处理 |
| --- | --- |
| 解包、摘要、空间或任务注册失败 | 不发布 `staged`；撤销本次控制改动并清理，旧版照常运行 |
| `staged` 时取消 | 取得安装互斥，禁用并确认任务无正在提交实例，再撤销计划/volatile 标识，清理本事务 |
| 相同包再次运行 | 核验同一事务后复用暂存并返回 `3010`；不创建第二个任务 |
| 另一个目标版本到来 | 初版要求先取消旧 `staged`，再准备新包；不静默覆盖目标 |
| 已进入 `validating` / `applying` | 新安装器、取消和卸载等待或明确报告忙；不并行改变运行树 |
| 开机后仍有占用 | 未修改运行文件时恢复完整旧版，保留 `staged` 和错误信息，等待下一次明确重启 |
| 文件/注册提交失败 | 保持维护状态，按日志恢复完整旧版及其注册；恢复成功才发布 `installed` |
| 回滚本身受占用或写入错误阻止 | 标记 `recovery-required` / `pending`，保留任务、备份和日志；下次开机或提权修复继续 |
| 断电或进程崩溃 | 根据意图日志和实际摘要幂等继续或回滚；不因旧 `installed` 镜像或部分结果而直接放行 |
| 提交完成，清理失败 | 新版保持可用；只清理已证明归本事务所有的残留 |

提交前失败可以恢复旧版继续使用；提交后只有完整回滚成功才能作此承诺。进入 `recovery-required` 时明确提示输入法暂不可用，可先使用其他输入法，不能显示普通待重启成功文案。

### 10.1 卸载入口与 Inno 数据

待升级期间必须让正常卸载先经过支持事务的维护入口。旧 helper 不认识新事务，不能原样作为唯一协调者。准备阶段安装维护入口并切换产品卸载命令及开始菜单卸载入口；保留旧值用于取消/回滚，显示版本仍保持旧版。此改动属于安装控制信息，不改变旧版输入运行文件。

`staged` 时卸载先在互斥内取消事务，再按现有“普通用户阶段 + 提权机器阶段 + Inno 原生卸载器”流程执行。SYSTEM 入口不能替代当前用户阶段。直接手工运行旧 `unins*.exe` 绕过协调器不作为支持入口；实施前必须核对现有 UI 和脚本没有留下绕过路径。

自定义文件提交不会自动更新旧 `unins*.dat`。因此需要独立、持久的运行文件所有权清单：原生 Inno 卸载器负责其已有清单，维护入口补充删除升级新增的受管文件、事务文件和临时任务。清单应在调用原生卸载器前复制到受保护的独立执行位置，避免 helper/清单被先删除后无法继续。

不编辑 Inno 私有卸载日志，不仅修改 `DisplayVersion` 就认为卸载已支持新版。保留与当前维护 schema 兼容的 Inno 卸载基础；若升级要求更换该基础，必须走专门迁移，不能直接覆盖 `unins*.exe/.dat`。

卸载与同目录重装仍检查系统待重启队列。未知/其他事务对安装根的待删除操作必须阻止重装；本方案不清空或重写其他软件的 `PendingFileRenameOperations`。

## 11. 拟新增接口与代码归属

以下接口已接入原生维护程序。所有变更命令检查身份、安装归属及事务状态，并支持无系统副作用的 `--what-if`；只读查询支持普通权限。

| 接口/模块 | 责任 |
| --- | --- |
| `begin-stage` / `seal-stage` | 管理员准备受保护目录，复制后全量校验，再注册控制入口/任务并发布 `staged` |
| `query-upgrade` | 返回活动/目标版本、状态、错误码与是否需要重启，不泄露用户路径 |
| `cancel-upgrade` | 管理员在可取消状态撤销任务和暂存，恢复安装控制入口 |
| `complete-upgrade` | SYSTEM 开机提交；同次开机误触发只返回待重启 |
| `recover-upgrade` | 管理员或 SYSTEM 根据日志恢复；不能绕过文件保护强制提交 |
| `cleanup-upgrade` | 清理完成事务和残留任务，保证可重复执行 |
| `win32/setup/upgrade.*` | 计划、schema、状态机、日志及恢复策略 |
| `win32/setup/upgrade_files.*` | 句柄保护、路径/ACL 验证、摘要、备份与文件发布 |
| `win32/setup/upgrade_system.cpp` | 原生系统适配：Task Scheduler、开机标识、注册快照、依赖和安装控制入口 |
| `win32/setup/CMakeLists.txt` | 独立维护目标，静态 CRT 与 PE imports 审计 |
| `installer/fcitx5.iss`、打包脚本 | UI/分支、载荷清单、暂存退出码、旧安装兼容 |
| `win32/ipc/installation.h` 与必要入口 | 保持等待阶段旧版可用；补齐提交期门控 |
| `win32/setup/main.cpp`、卸载协调器 | 共用互斥、预检、所有权与新增文件卸载 |

升级诊断使用受保护事务元数据与 `setup/upgrade-result.json`，只包含事务 ID、版本、阶段、受管文件相对路径和错误码，不包含输入文本、候选、配置内容或用户路径。SYSTEM 升级命令不复用用户 profile 下的 helper 日志；普通用户只读状态查询，不需要读取 SYSTEM profile。

安装器失败文案指向维护入口的 `query-upgrade`，该命令显示阶段、目标版本和错误码。Settings 的状态展示留作后续 UI 接入，不增加 Core IPC 协议；旧版 Settings 不会自动获得这项 UI。查询成功不等于升级成功，结果不随暂存清理丢失，下一次明确操作才更新。

## 12. 验证与验收

### 12.1 自动化测试与隔离 probe

- 状态机：非法转移、重复执行、准备失败、取消、重复包、并发卸载及每个写前/写后断点恢复。
- 文件层：清单篡改、空间不足、相对路径逃逸、reparse point、硬链接、重复/大小写冲突、ACL 错误和摘要不符。
- 真实映像占用 probe：独立进程加载测试 DLL/运行测试 EXE；确认替换保护识别已有映像，并阻止检查之后的并发加载；同时覆盖仅有映像映射而没有原始打开句柄的情况。
- 只读数据映射 probe：关闭原始文件句柄后继续持有映射，升级期间映射始终读取完整旧内容，最终路径读取完整新内容；映射延迟释放或清理任务删除失败时，不回滚已提交版本。
- 文件提交 probe：在隔离目录覆盖替换、新增、移除及部分发布，进程中止后逐步恢复，结果只能是完整源版本、完整目标版本或明确门控关闭的恢复状态。
- 打包回归：暂存不写活动运行文件、不改用户偏好、不启动服务、不新增产品卸载项；含/不含运行库包、真实 Inno 退出码与 `-WhatIf` 行为。
- 模拟系统调用：任务身份/ACL/动作固定、任务注册失败、同次开机误运行、查询错误与占用不同、维护写入失败，以及清理重试。
- 卸载回归：升级新增文件能清理；旧 Inno 清单与新所有权清单共存；待升级卸载不会在下次开机重新安装产品。

这些测试不等于真实系统派发、系统重启或应用中文输入验证。

### 12.2 Windows VM 系统验收

| 场景 | 必须观察的结果 |
| --- | --- |
| 应用加载旧 TSF DLL 后准备升级 | 安装准备成功；旧应用继续输入，新开应用及 Settings 仍使用旧版 |
| 普通重启、快速登录/自动登录 | 正常一次重启提交；记录任务与 ctfmon/应用加载顺序，不能只看任务返回码 |
| 故意延迟开机任务，让旧 DLL 先加载 | 保留完整旧版，不强行覆盖；提示未完成并保留可重试计划 |
| 准备后注销、休眠、睡眠、快速启动关机 | 不错误地当作满足重启提交条件；旧版仍可用或提示明确重启 |
| 多用户/RDP、AppContainer/Sandboxie 占用 | 不结束会话/应用；不混用版本；受限客户端不启动本地 Core |
| 开机中断电、提交进程被中止、磁盘写入失败 | 重启后继续恢复或明确阻止输入法启动，不能运行半套文件 |
| 准备后用户修改默认输入项/登录启动/配置 | 提交及回滚保留用户后来的选择 |
| 旧版本无 runtime manifest | 显式迁移路径正确；未知文件保留；冲突不被静默覆盖 |
| 取消、再次安装、卸载及卸载待重启重装 | 无任务复活、无旧队列误删、无新增受管文件残留 |
| 含/不含运行库包、依赖额外要求重启 | 正确区分依赖未满足、升级待重启与机器提交成功 |
| 标准用户由其他管理员代输 UAC、machine-only | 不修改错误账户的输入偏好；SYSTEM 不启动输入服务 |
| 完成升级及回滚后真实应用输入 | Core/TSF/Settings 版本一致；中文提交、候选、设置和现代 category 注册实际核验 |

## 13. 实施顺序与发布门槛

1. 先做隔离文件/映像 probe 和 VM 开机时序原型。确认保护机制能覆盖已有映像及并发加载，并验证典型一次重启完成率。若任务时序持续被旧 DLL 抢先、或不能证明提交期间无旧映像复活，停止产品接入，重新评审启动协调方式；不能删除占用检查或依赖固定等待绕过。
2. 实现清单、计划、互斥、日志、独立维护入口及模拟状态机。先验证准备和取消不会影响旧版输入。
3. 实现门控下的文件提交、注册、全断点恢复及卸载所有权；在隔离目录与模拟注册环境验证。
4. 接入 Inno 分支、任务、运行库策略、结果查询及升级/卸载入口迁移；保持既有首次安装 smoke。
5. 使用可重启 Windows VM 完成系统矩阵。CI 增加无系统副作用的计划/打包回归；真实重启验收使用独立 VM/self-hosted runner。

发布前必须完成：占用准备时旧版持续可用、正常一次重启升级、抢先加载时安全保留旧版、断电恢复、待升级卸载，以及旧包首次迁移。Task Scheduler 不保证任务先于登录程序运行是本方案的主要剩余风险，原型结果决定方案能否进入产品。

目前代码和隔离测试已接入，默认包已启用延迟升级，尚未执行本机真实安装、任务注册或系统重启验收。默认启用不代表 VM 矩阵已通过，不能把免注册测试描述成真实应用升级验收。

## 参考

- [Task Scheduler 开机触发示例](https://learn.microsoft.com/en-us/windows/win32/taskschd/boot-trigger-example--xml-)：开机任务的触发机制，不提供先于全部用户应用的排序保证。
- [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew) 与 [SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)：共享模式及基于句柄的文件操作；具体提交保护组合以 probe 为准。
- [MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)：延迟操作写入系统共享队列，登记成功不等于重启操作成功，因此不作为本方案的整套提交机制。
- [Inno Files](https://jrsoftware.org/ishelp/topic_filessection.htm)：`restartreplace` 与 `uninsrestartdelete` 的适用范围。
