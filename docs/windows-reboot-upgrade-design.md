# Windows 输入法占用时的延迟升级设计

状态：已接入，默认发行包与 CI/Nightly 均启用（打包时传入 `-RebootUpgrade:$false` 可关闭）。受管理升级完整暂存新版并在等待重启期间保持旧版可用，暂存成功返回 `3010`。自动化已覆盖文件/映像保护、状态机、COM 暂停/恢复、关机服务、注册恢复、卸载清单以及 Inno 向导/重启选择脚本；真实开机时序、完整安装与系统重启需通过可重启 VM 验收。安装总体流程见 [Windows 安装包实现与设计](windows-installer-design.md)。

## 实现入口与打包

维护程序位于 `setup/maintenance-host/Fcitx5Upgrade.exe`，运行文件所有权记录在 `setup/runtime-manifest.json`。同一时刻只允许存在一个 `setup/upgrades/current` 事务，其随机事务 ID 同时绑定开机任务与临时预关机服务。目标清单的输入协议版本必须与新维护程序一致。

```powershell
./scripts/build-installer.ps1 -Prefix ./dist/installer-stage -Output ./dist/installer -SkipRuntimes
./scripts/test-build-installer.ps1
./scripts/test-reboot-upgrade-package.ps1 -TsfBuild ./build/installer/tsf
./scripts/test-installer-wizard.ps1 -IsccPath <Inno-Setup-7/ISCC.exe>
./scripts/test-installer-restart.ps1 -IsccPath <Inno-Setup-7/ISCC.exe>
ctest --test-dir ./build/installer/tsf --output-on-failure
```

安装后可使用普通权限查询升级状态（省略 `--silent` 时显示对话框）：

```powershell
& 'C:/Program Files/Fcitx5/setup/maintenance-host/Fcitx5Upgrade.exe' query-upgrade --prefix 'C:/Program Files/Fcitx5'
```

`cancel-upgrade`、`recover-upgrade` 和 `cleanup-upgrade` 为提权维护操作；静默暂存成功后返回 `3010`，不主动重启 Windows。

## 1. 目标与范围

输入法 DLL 被应用占用时，安装器直接准备升级而无需关闭应用，并在完成后提示：

> 升级已准备完成。重启 Windows 后完成升级；重启前可以继续使用当前版本。

核心约束：

- 等待重启期间，活动安装树的 Core、TSF、Settings、引擎和资源保持完整旧版，旧版服务持续可用。
- 提交时整体更新整套运行树，避免新版 Core 与旧版 TSF DLL 混用。
- 不终止第三方应用、不强制卸载外部进程中的 DLL、不自动注销或重启 Windows。
- 保留用户设置、词典、学习历史、默认输入项、输入法启用状态与登录启动选择。
- 面向 Windows 10 1809+ x64、同安装目录、同 CLSID/profile 的受管理安装升级（不含降级、目录迁移或多版本并存）。
- 首次安装走常规流程；旧版损坏、存在待重启卸载或注册指向其他目录时先修复或完成卸载，不进入延迟升级。

“暂存完成”与“新版已生效”是两个独立结果：若开机提交时文件仍被占用或出错，保留旧版并报告未完成。

## 2. 当前调用链

| 入口 | 职责 |
| --- | --- |
| `installer/fcitx5.iss` / `DetectMode` | 在暂停服务和维护动作前区分 `FreshInstall` 与 `StageUpgrade` |
| `PrepareToInstall` / `[Files]` | 升级调用 `begin-stage` 撤销未提交旧事务，并将新版复制到 `setup/upgrades/current/payload` |
| `NeedRestart` / `CurStepChanged(ssPostInstall)` | 升级在 `NeedRestart` 调用 `seal-stage`；首次安装在 `ssPostInstall` 执行注册与用户配置 |
| `win32/setup/upgrade.*` | 单一事务、状态机、恢复、活动文件摘要与所有权 |
| `win32/setup/upgrade_files.*` | 安装文件锁、句柄保护、备份、文件发布与路径/ACL 检查 |
| `win32/setup/upgrade_system.cpp` | 临时 SYSTEM 开机任务、volatile 重启标识、双视图注册快照/恢复、运行库与卸载入口协调 |
| `win32/setup/upgrade_shutdown.cpp` | 临时 `Fcitx5-UpgradeShutdown` SYSTEM 预关机服务与重启取消恢复 |
| `win32/ipc/installation.h` | `installed` 门控；等待阶段保持旧版启动许可 |
| `scripts/build-installer.ps1` | 生成逐文件运行清单与 Inno 文件列表，独立部署维护程序 |

本方案沿用现有 Named Pipe 协议与三个工程的工具链/运行库边界。

## 3. 方案选择

采用“受保护暂存目录 + 临时 SYSTEM 预关机服务与开机任务 + 原生提交程序”。

不使用逐文件 `restartreplace` 或 `PendingFileRenameOperations` 排入整套升级，因为系统共享队列缺乏整套原子提交/回滚语义且难以与取消事务协调（卸载清理仍沿用 `uninsrestartdelete`）。

由于开机计划任务可能晚于部分登录程序启动，本方案在重启前先暂停双视图 COM 加载入口，并在开机提交时强制要求三项前提：

1. 原子写入 `maintenance` 门控并验证成功。
2. 确认所有受管运行文件均无活动映像或阻止替换的引用。
3. 持有覆盖整个检查到发布期间的 `FileGuard` 独占写保护。

任一条件未满足且尚未改动活动文件时，释放保护、恢复旧版 COM 入口与 `installed`，保留暂存并报告仍需重启。

## 4. 目录与文件所有权

活动目录布局维持不变，控制目录结构如下：

```text
<ProgramFiles>/Fcitx5/
  bin/ lib/ share/ tsf/ settings/ licenses/ plugins/   # 活动版本
  setup/
    Fcitx5SetupHelper.exe                             # 活动 helper
    managed-install
    install-state                                     # installed / maintenance / pending
    runtime-manifest.json                             # 当前活动文件所有权与摘要
    maintenance-host/
      Fcitx5Upgrade.exe                               # 独立升级/恢复维护程序（不覆盖自身）
    upgrades/current/                                 # plan.json 记录随机事务 ID
      plan.json                                       # 不可变计划
      target-manifest.json                            # 目标包与逐文件摘要
      journal.json                                    # 可恢复进度
      inventory.json                                  # 旧文件状态与备份摘要
      activation.json                                 # COM 暂停前的双视图 InprocServer32 快照
      payload/                                        # 完整新版运行树
      backup/                                         # 提交前生成的旧版备份
      retired/                                        # 替换时移出的旧文件与待发布文件
      dependencies/                                   # 可选运行库安装程序
    upgrade-result.json                               # 持久化的最终结果
  unins*.exe + 对应 Inno 卸载数据                      # 保留原有卸载基础
```

`Fcitx5Upgrade.exe` 由 `win32` 工具链构建，静态链接 MSVC CRT，仅依赖系统 DLL。文件所有权分三类管理：运行 manifest 管理的活动文件、Inno 管理的安装基础文件、升级器管理的事务文件；`%APPDATA%` 用户数据与共享运行库不列入删除清单。

## 5. 清单、计划与安全约束

### 5.1 运行文件清单

UTF-8 JSON（`schemaVersion = 1`），记录包标识、四段版本、`architecture = x64`、输入协议版本，以及运行文件与可选运行库安装程序的相对路径、大小和 SHA256。构建阶段排除 `install-state`、事务目录、动态日志、Inno 卸载程序及临时文件。

- 旧安装缺少运行清单时，按新版清单备份并替换对应既有产品文件，保留清单外的旧文件。
- 已有运行清单时，若新版新增路径已存在且不属于旧所有权清单，报冲突并保留旧版。

### 5.2 事务计划与日志

- `plan.json`：schema、随机事务 ID、目标清单摘要、安装器父进程 PID/创建时间，以及来源 Core/TSF/Settings/helper 摘要。
- `journal.json` 与 `inventory.json`：记录阶段、错误码和整批文件的旧存在状态及备份摘要。完整备份、注册快照与 inventory 落盘后才写入 `applying` 并修改活动文件；元数据使用同目录临时文件 + 刷盘原子替换，恢复时同时核对实际文件摘要。
- 准备阶段以安装器父进程 PID + 创建时间保护事务所有权，防止原安装器复制期间被其他进程接管或取消。

### 5.3 安全约束

- 安装根、维护入口、暂存、备份、JSON 与计划任务仅允许 SYSTEM、Administrators 或 TrustedInstaller 写入；普通用户仅可读取状态。
- 提交前重新校验 ACL、安装归属、文件摘要与路径。
- 限定本地 NTFS、同卷暂存与提交；拒绝 UNC、可移动介质、reparse point、路径逃逸（`..`）、ADS、设备名、大小写碰撞和硬链接越界；通过已验证句柄执行文件操作。
- 计划不含任意 shell 命令；SYSTEM 阶段不执行 PowerShell、不访问用户配置、不启动 Core/Settings。
- 校验磁盘剩余空间（预留旧版备份与发布临时空间）、文件数及大小上限；安装、升级、恢复与卸载统一通过 `InstallationLock` 协调。

## 6. 状态模型

现有 `install-state` 仅使用 `installed`/`maintenance`/`pending`，避免旧版门控把待升级状态误判为不可用。

| 事务状态 | `install-state` | 活动版本 | 允许的下一步 |
| --- | --- | --- | --- |
| 无事务 | `installed` | 当前版 | 安装、升级或卸载 |
| `preparing` | `installed` | 旧版 | 完成校验，或取消并清理 |
| `staged` | `installed` | 旧版 | 等待重启、取消、查询 |
| `suspending` | `installed` 或 `maintenance` | 完整旧版，COM 入口暂停中 | 完成暂停、恢复入口、取消 |
| `suspended` | `maintenance` | 完整旧版，COM 入口已暂停 | 重启后提交、恢复入口、取消 |
| `cancelling` | `installed` | 旧版 | 幂等撤销任务、服务、入口与暂存 |
| `validating` | `maintenance` | 完整旧版 | 取得替换条件，或恢复旧版并等待 |
| `applying` | `maintenance` | 变更中 | 仅允许继续提交或恢复 |
| `verifying` | `maintenance` | 新版已写入 | 校验、注册或回滚 |
| `committed` | `installed` | 完整新版 | 清理事务 |
| `rolling-back` | `maintenance` | 恢复中 | 恢复完整旧版 |
| `rolled-back` | `installed` | 完整旧版 | 展示原因，允许重新准备 |
| `recovery-required` | `pending` | 未确认完整 | 仅允许修复/恢复 |

## 7. 准备阶段与 Inno 集成

1. 新安装器提取自身载荷中的新版维护程序，获取安装锁并预检冲突注册、旧事务和待重启删除项。
2. 受管理升级在 `pause-user` / `set-maintenance` 之前进入 `StageUpgrade` 模式：校验清单、协议、架构、版本与空间；若已有 `staged`/`suspending`/`suspended`/`cancelling` 事务（或所属进程已退出的 `preparing` 残留），自动撤销旧任务/服务并恢复入口后重建新事务；正在 `validating`/`applying`/`verifying`/恢复中的事务阻止新安装。
3. 完整解包新版到事务 `payload`，校验摘要和依赖，不修改活动文件、用户偏好、`DisplayVersion` 或旧 `unins*.dat`。
4. 在 `NeedRestart` 中调用 `seal-stage`：全量复核载荷，部署维护入口、卸载协调入口、开机任务、预关机服务与本次开机 volatile 标识，发布 `staged` 并返回 `3010`。

Inno 脚本细节：

- `Uninstallable` 和 `CreateUninstallRegKey` 仅在 `FreshInstall` 返回 True；`[Files]` 目标在升级模式指向事务 `payload`，普通 `[Icons]`/`[Registry]`/运行库安装/用户后置步骤均仅在首次安装执行。
- 封存在 `NeedRestart` 中只尝试一次，失败则取消未封存事务、在完成页显示错误并返回非零码。完成页文案在 `CurPageChanged(wpFinished)` 设置；勾选“立即重启”且可见时才在点击完成时调用 `suspend-upgrade`（`/NORESTART` 隐藏选项时保持旧版可用）。
- 自动化使用 `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /RESTARTEXITCODE=3010`。`scripts/test-installer-wizard.ps1` 与 `scripts/test-installer-restart.ps1` 已用 Inno Setup 7 验证模式切换、封存失败、完成页文案、立即/稍后/静默重启路由与退出码。

## 8. 重启前暂停与重启后提交

### 8.1 重启前暂停 COM 入口

- 完成页选择立即重启并点击完成时，安装器同步调用 `suspend-upgrade`（失败则停留在完成页报错）。选择稍后重启或静默暂存时保持旧版可用，由临时 `Fcitx5-UpgradeShutdown` 服务在 `SERVICE_CONTROL_PRESHUTDOWN`（60 秒预算）触发相同暂停。
- `Fcitx5-UpgradeShutdown` 仅在待升级期间注册为 LocalSystem 自动启动服务（DACL 仅允许 SYSTEM/Administrators 控制，崩溃最多重启两次），绑定安装根、受保护维护程序路径与事务 ID；取消或提交/回滚后删除。
- `suspend-upgrade` 发布 `suspending` → 保存双视图 `InprocServer32` 快照至 `activation.json` → 清除双视图默认 DLL 路径并刷新注册表（不删除 TSF profiles/categories、ThreadingModel 或用户输入项）→ 写 `maintenance` 门控并发布 `suspended`。
- 若立即重启被取消，服务在暂停约 60 秒且系统未关机时于同次开机内调用 `resume-upgrade` 恢复旧入口、`installed` 与 `staged`。异常断电跳过预关机通知时仍由开机文件保护兜底；快速启动关机不等价于完整重启，产品文案要求使用 Windows“重启”。

### 8.2 开机任务与重启识别

- 通过 Task Scheduler COM API 注册固定路径的 SYSTEM 非交互开机任务（BootTrigger 无延迟，单实例，有界重试），仅调用受保护 `Fcitx5Upgrade.exe complete-upgrade --transaction <id>`。
- 准备阶段在受保护 HKLM 子键创建 `REG_OPTION_VOLATILE` 事务标识：同次开机内标识存在时误触发只返回待重启；完整重启后标识消失且持久计划匹配才进入提交。恢复中断的 `applying` 事务依据恢复日志执行，不受同次开机限制。

### 8.3 取得提交条件与多步发布

1. 取得 `InstallationLock`，校验事务、版本、安装根身份、载荷摘要及第三方待重启操作。
2. 原子写入 `maintenance` 门控并验证。
3. 对新旧清单全部活动文件逐一取得 `FileGuard` 独占写保护；任一文件受占用时释放保护、恢复 COM 入口与 `installed`、重建本次开机标识并返回 `staged` 等待下次重启。
4. 持有保护后复核来源摘要，生成旧版备份与合并了原 COM 入口的注册快照。
5. 发布阶段不原地覆盖旧文件（兼容 Explorer 等只读数据映射）：在 `retired` 准备并刷盘新文件，通过已持有句柄将旧文件移入 `retired`，再将新文件发布到最终路径（新增文件用 `CREATE_NEW`），并保留原文件 DACL（确保 AppContainer 读取权限不丢失）；废弃受管文件按旧清单移除，未知文件保留。
6. 全量校验活动文件大小与摘要，使用新版 DLL 最终路径更新双视图 COM/profile/category 并校验双向注册、图标路径与 threading model。
7. 更新 `runtime-manifest.json`、卸载所有权与机器版本元数据并写入持久提交记录，最后发布 `installed`、标记 `committed`、写入 `upgrade-result.json` 并清理任务、服务与暂存（旧对象延迟释放时留在 `retired` 由后续重试清理；事务目录改名为 `completed-<id>` 后删除）。

若在发布 `installed` 后、写入 `committed` 前中断，恢复流程核对提交记录、活动 manifest 与注册后补记提交并清理；提交前中断则完整回滚文件、注册与元数据。SYSTEM 阶段不启动 Core 或调用 `configure-user`。

## 9. 用户状态与运行库

- 同 CLSID/profile 升级自动保留既有用户输入法配置、语言、日志、快捷键、手动停止意图与登录启动选择。
- 含运行库包将已验证的官方运行库暂存在事务 `dependencies/`，提交阶段在维护门控下安装机器依赖（若依赖另需重启则保持待完成状态，不提前发布 `installed`）；现有用户的 Settings bootstrap 保持在普通权限检查。不含运行库包缺少必要机器依赖时在改动活动文件前报错。共享运行库不随回滚卸载。

## 10. 失败、取消、重复安装与卸载协调

| 场景 | 处理 |
| --- | --- |
| 解包、摘要、空间或任务注册失败 | 撤销控制改动并清理，旧版继续运行 |
| `staged` 时取消 | 持锁禁用任务/服务、确认无提交中实例，恢复入口与控制项并清理事务 |
| 重复运行或新版本到来 | 校验新包后自动撤销未开始提交的旧事务并重建；提交/恢复中拒绝并发 |
| 开机后仍有占用 | 未改动活动文件时恢复旧版、COM 入口与 `staged`，等待下次明确重启 |
| 文件或注册提交失败 | 保持 `maintenance`，按日志完整回滚旧版与注册，成功才恢复 `installed` |
| 回滚受阻 | 标记 `recovery-required` / `pending`，保留备份与日志供下次开机或提权修复 |
| 断电或进程崩溃 | 按日志与实际摘要幂等续跑或回滚，不放行半套文件 |
| 提交完成但清理失败 | 新版保持可用，后续重试清理归属本事务的残留 |

卸载协调：准备阶段将卸载入口指向受保护 `Fcitx5Upgrade.exe`（保留旧值供取消/回滚，不改 `DisplayVersion`）。待升级期间卸载先持锁取消事务，再按“普通用户阶段 + 提权机器阶段 + Inno 原生卸载器”执行；维护入口在运行原生卸载器前复制独立的运行文件所有权清单，补充清理升级新增的受管文件、事务文件与临时任务，不修改 Inno 私有 `unins*.dat`。

## 11. 维护接口与代码归属

所有变更命令均校验身份、安装归属与事务状态，并支持 `--what-if`；`query-upgrade` 支持普通权限只读查询。

| 接口 / 模块 | 责任 |
| --- | --- |
| `begin-stage` / `seal-stage` | 管理员创建受保护暂存目录；复制后全量校验并注册控制入口、任务、预关机服务与 `staged` |
| `suspend-upgrade` / `resume-upgrade` | 管理员或 SYSTEM 暂停双视图 COM 入口；同次开机重启取消时恢复入口与 `staged` |
| `upgrade-shutdown-service` | SCM 启动的临时 SYSTEM 预关机服务，监听 `PRESHUTDOWN` 并处理重启取消恢复 |
| `query-upgrade` | 返回活动/目标版本、状态、错误码与待重启标记（不泄露用户路径） |
| `cancel-upgrade` | 管理员在可取消状态撤销任务/服务/暂存并恢复控制入口 |
| `complete-upgrade` | SYSTEM 开机提交；同次开机误触发仅返回待重启 |
| `recover-upgrade` | 管理员或 SYSTEM 按日志恢复（仍强制校验文件保护） |
| `cleanup-upgrade` | 幂等清理已完成事务与残留任务 |
| `win32/setup/upgrade.*` | 计划、schema、状态机、日志与恢复策略 |
| `win32/setup/upgrade_files.*` | `FileGuard` 句柄保护、路径/ACL 校验、摘要、备份与发布 |
| `win32/setup/upgrade_system.cpp` | Task Scheduler、volatile 开机标识、双视图注册快照、依赖与控制入口 |
| `win32/setup/upgrade_shutdown.cpp` | 临时预关机服务与重启取消恢复 |
| `win32/setup/CMakeLists.txt` | 独立维护目标、静态 CRT 与 PE imports 审计 |
| `installer/fcitx5.iss`、`scripts/build-installer.ps1` | 向导分支、载荷清单、退出码与旧安装兼容 |

升级结果持久化在 `setup/upgrade-result.json`（不随暂存清理丢失）；执行日志按运行身份写入各自 LocalAppData 的 `installer-helper.log`。Settings 内置升级状态页尚待接入。

## 12. 自动化覆盖与 VM 系统验收

### 已覆盖的自动化测试

- **`test_upgrade`**：状态机合法/非法转移、可注入断点恢复、取消、重复包、并发卸载及模拟机器操作。
- **`test_upgrade_files`**：真实临时文件、DLL/EXE 映像占用与并发加载阻止、只读数据映射、路径逃逸/reparse/硬链接/ACL/摘要校验及句柄内替换发布。
- **`test_upgrade_registration`**：双视图注册快照与恢复。
- **`test_upgrade_activation`**：双视图 `InprocServer32` 暂停、合并快照与恢复。
- **`test_upgrade_shutdown`**：预关机服务控制、固定操作绑定与重启取消恢复。
- **`test_uninstall`**：升级新增文件所有权清理、待升级卸载取消与 `3010` 返回。
- **`scripts/test-reboot-upgrade-package.ps1`**：使用测试载荷编译默认双包与 `-RebootUpgrade:$false` 包，检查 Inno 分支与清单。
- **`scripts/test-installer-wizard.ps1` / `test-installer-restart.ps1`**：使用真实 Inno Setup 7 验证模式判定、向导任务可见性、封存失败、完成页文案、立即/稍后/静默重启路由与退出码。

### 待执行的 Windows VM 系统验收

| 场景 | 验收要点 |
| --- | --- |
| 应用加载旧 TSF DLL 后准备升级 | 准备成功返回 `3010`；旧应用、新开应用与 Settings 继续稳定使用旧版 |
| 立即重启 / 稍后重启 / 快速登录与自动登录 | 预关机服务与开机任务配合一次重启完成提交；记录任务与 ctfmon/应用加载时序 |
| 立即重启被取消、延迟开机任务致旧 DLL 抢先加载 | 取消重启后自动恢复旧入口；抢先加载时保留完整旧版并提示待重启 |
| 注销、休眠、睡眠、快速启动关机 | 不误触发提交，旧版继续可用 |
| 多用户/RDP、AppContainer/Sandboxie 占用 | 不结束会话/应用，不混用版本 |
| 开机断电、提交进程中止、磁盘写入失败 | 再次启动后正确续跑/回滚或保持门控关闭 |
| 准备后修改用户偏好、旧包无 runtime manifest 迁移 | 保留用户后续修改；旧安装平滑迁移且未知文件保留 |
| 取消、重装、待升级卸载、含/不含运行库及跨 UAC 凭据 | 无任务复活、无新增受管文件残留；完成后真实应用验证中文字体/候选/现代 category |

## 参考

- [Task Scheduler BootTrigger](https://learn.microsoft.com/en-us/windows/win32/taskschd/boot-trigger-example--xml-)
- [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew) 与 [SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)
- [RegCreateKeyExW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexw)（`REG_OPTION_VOLATILE`）
- [Inno Files](https://jrsoftware.org/ishelp/topic_filessection.htm) 与 [Script Events](https://jrsoftware.org/ishelp/topic_scriptevents.htm)
