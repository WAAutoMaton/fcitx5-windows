# Windows 安装包实现与设计

安装实现、`base`/`full` × 含/不含运行库四种打包、可选登录启动、默认延迟升级和 CI/Nightly 发布流程均已接入。CI 对 `base`/`full` 的不含运行库包运行机器级安装/注册/卸载 smoke；完整用户初始化、含运行库部署、升级与重启清理需干净 VM 验收。

## 交付与支持范围

使用 Inno Setup 7 生成 x64 EXE，安装到 `<ProgramFiles>/Fcitx5`，内置共享同一 x64 Core 的独立 x64/x86 TSF。MSI 与原生 ARM64 TSF 尚未提供。

| CI 包名 | 运行库策略 |
| --- | --- |
| `Fcitx5-<run>-<edition>-x64-setup.exe` | 附带 Microsoft x64 VC++ Redistributable 与 Windows App Runtime 安装程序 |
| `Fcitx5-<run>-<edition>-x64-no-runtime-setup.exe` | 需预先为当前用户安装上述运行库 |

四种安装包均包含 Core、x64/x86 TSF、Settings 及原生安装/升级 helper：

- `base`：内置 `fcitx5-chinese-addons`。
- `full`：包含 `plugins/` 下全部插件（`fcitx5-chinese-addons`、`fcitx5-rime`、`fcitx5-table-extra`、`fcitx5-wenzhounese`、`fcitx5-zhuyin`），额外插件部署到 `plugins/<name>` 并纳入安装/升级所有权清单，见 [Windows 插件](windows-plugins.md)。
- Settings 采用 framework-dependent Windows App SDK 1.8，要求当前用户已安装 x64 Framework/DDLM >= `8000.994.2142.0` 与 VC++ 运行库；不含运行库包在用户阶段仍会执行 `Fcitx5Settings.exe --runtime-check`。

安装器配置为 `SetupArchitecture=x64`、`ArchitecturesAllowed=x64os`、64 位安装模式、`MinVersion=10.0.17763`（Windows 10 1809 x64，与锁定的 Windows App Runtime 1.8.12 MSIX 一致）、`PrivilegesRequired=admin` 与固定 AppId。x64 TSF 由原生 helper 注册，x86 TSF 通过 `SysWOW64/regsvr32.exe` 注册，两者共享 CLSID/profile 并使用独立 registry view（参见 [TSF 64 位平台说明](https://learn.microsoft.com/en-us/windows/win32/tsf/64-bit-platform-considerations)）。

## 实现入口

| 位置 | 职责 |
| --- | --- |
| `scripts/build-and-deploy.ps1` | 调度 Core/libime/addons、双架构 TSF/helper 与 Settings，测试并部署运行树 |
| `scripts/build-installer.ps1`、`installer/fcitx5.iss` | 校验打包输入、复制许可证、写入 `managed-install`、生成运行清单与 EXE/SHA256/manifest |
| `cmake/installer-lock.json` | 锁定参考 Inno 版本及官方运行库 URL、版本、SHA256 与发布者 |
| `win32/setup/main.cpp`、`userprofile.cpp` | 注册归属、权限与占用检查、用户输入项备份/恢复、启用/激活、Core 就绪与卸载入口 |
| `win32/setup/startup.h` | 当前用户可选登录启动项（固定 `--login-start` 命令与安装路径归属检查） |
| `win32/setup/log.h` | 安装/维护 helper 的 UTF-8 诊断日志 |
| `win32/setup/upgrade*.{h,cpp}` | 延迟升级：暂存、COM 暂停与预关机服务、SYSTEM 开机提交、文件保护、恢复与卸载所有权 |
| `win32/dll/register.cpp`、`main.cpp` | COM/profile/category 注册与注销（使用 32768 字符 DLL 路径缓冲并返回失败结果） |
| `win32/ipc/installation.h` | 受管理安装识别与服务启动门控 |
| `.github/workflows/ci.yml` | 联合构建、并行测试/打包、安装 smoke 与 Nightly 发布 |

## 安装树与状态

```text
<ProgramFiles>/Fcitx5/
  bin/Fcitx5.exe + Core/MSYS2 运行 DLL
  lib/fcitx5/ + lib/libime/zh_CN.lm
  share/fcitx5/ + share/libime/sc.dict
  share/locale/ （Windows 与上游简体/繁体翻译）
  tsf/fcitx5-x86_64.dll
  tsf/x86/fcitx5-x86.dll
  settings/Fcitx5Settings.exe + DLL/PRI/WinMD
  setup/Fcitx5SetupHelper.exe
  setup/maintenance-host/Fcitx5Upgrade.exe
  setup/runtime-manifest.json
  setup/managed-install + install-state
  setup/WindowsAppRuntimeInstall-x64.exe  （仅含运行库版）
  licenses/
  plugins/<name>/bin + lib + share + licenses  （full 额外插件）
  unins*.exe + 卸载记录
```

- **机器状态**：存于 `setup/install-state` 与 `HKLM\SOFTWARE\Fcitx5\Installer`（取值为 `installed`、`maintenance`、`pending`）。受管理安装由 `setup/managed-install` 或 HKLM 键识别，仅在 `install-state` 有效且为 `installed` 时允许服务启动；未标记的开发树保留原有启动行为。升级事务状态独立存于 `setup/upgrades/current`，等待重启期间不修改旧版 `installed`；最终结果持久化在 `setup/upgrade-result.json`。
- **可选登录启动**：向导中的 `loginstart` 任务默认不勾选，以普通原始用户身份写入 HKCU Run 的 `Fcitx5.exe --login-start`，启动前检查自动启动许可并遵守本次登录的手动停止状态；卸载仅清理归属本安装树的启动项。
- **用户数据保留**：`%APPDATA%/Fcitx5` 中的配置、词库和学习历史不受安装/卸载影响；用户输入项备份与 helper 日志位于用户 LocalAppData。

## Helper 与权限

`Fcitx5SetupHelper.exe` 封装注册、用户配置与服务控制（所有变更命令支持 `--prefix` 和无副作用 `--what-if`）：

| 命令 | 执行身份与行为 |
| --- | --- |
| `preflight-machine` | 检查冲突注册及安装根下待重启删除项 |
| `register-machine` / `unregister-machine` | 管理员注册/注销并核对 COM DLL 路径归属（支持 DLL 缺失时的有限清理） |
| `configure-user` | 普通用户验证 runtime、备份/启用输入项、首次设默认并激活、重启 Core 并等待就绪 |
| `rollback-user` / `prepare-uninstall-user` | 普通用户恢复必要偏好、移除输入项并优雅停止服务 |
| `pause-user` / `resume-user` | 普通用户执行维护前暂停与恢复 |
| `enable-login-start` / `disable-login-start` | 普通原始用户管理本安装路径的 HKCU Run 项（不覆盖其他路径） |
| `set-maintenance` / `clear-maintenance` / `mark-pending` | 管理员写入 `maintenance` / `installed` / `pending` |
| `inspect-locks` | 通过 Restart Manager 查询安装树 EXE/DLL 占用 |
| `uninstall` | 普通权限先处理用户阶段，再提权执行机器注销与 Inno 卸载（支持 `--silent`） |

`Fcitx5Upgrade.exe` 另提供 `query-upgrade`、`cancel-upgrade`、`recover-upgrade`、`cleanup-upgrade`、`suspend-upgrade`、`resume-upgrade` 等接口，详见 [维护接口](windows-reboot-upgrade-design.md#11-维护接口与代码归属)。

安装器通过 [`ExecAsOriginalUser`](https://jrsoftware.org/ishelp/topic_isxfunc_execasoriginaluser.htm) 运行用户阶段；普通权限命令拒绝以管理员身份执行用户配置。`/MACHINEONLY=1` 跳过用户初始化并在结束页提示。

## 安装与升级流程

### 首次安装（及关闭延迟升级时的覆盖路径）

1. 已有 helper 时执行 `preflight-machine`，拒绝冲突注册或安装根下待重启删除项（发现待删项返回 `3010`）。已有受管理安装且关闭延迟升级时，暂停服务、写入 `maintenance` 并检查占用，发现占用则退出并恢复。
2. 含运行库版先安装 VC++ 运行库，再复制完整运行树。安装器设置 `CloseApplications=no`、`RestartApplications=no`，不终止第三方应用。
3. 从最终路径注册双架构 TSF 并写入 `installed`。含运行库版安装 Windows App Runtime，再以原始用户执行 `configure-user` 验证 bootstrap（必要时在该用户下重试运行库安装）。
4. `configure-user` 备份旧默认项，调用 `InstallLayoutOrTip`（不使用 `ILOT_CLEANINSTALL`，不改登录屏幕）启用 Fcitx5；仅首次初始化时设置默认并激活，随后重启 Core 并轮询就绪。
5. 若用户配置或后续步骤失败，执行补偿回滚（`rollback-user`、注销并标记 `pending`）。

### 默认延迟升级

默认安装包（含 CI/Nightly）启用延迟升级（可通过 `-RebootUpgrade:$false` 关闭）：受管理升级完整暂存新版并保持旧版可用，返回 `3010`；重启前通过 `suspend-upgrade` 或临时 SYSTEM 预关机服务（`Fcitx5-UpgradeShutdown`）暂停双视图 COM 入口，重启后由临时 SYSTEM 开机任务在维护门控下整体提交与恢复。完整机制见 [延迟升级设计](windows-reboot-upgrade-design.md)。

## 卸载与 DLL 占用

开始菜单与 Windows 已安装应用卸载入口指向 `setup/maintenance-host/Fcitx5Upgrade.exe uninstall --prefix <root>`：

- 普通权限阶段处理当前用户输入项恢复（仅当当前默认仍为 Fcitx5 时恢复原项或有效替代项）并优雅停止服务。
- 提权阶段协调待升级事务、写 `maintenance`、查询占用、注销双视图 TSF 并调用 Inno 原生卸载器（`/NORESTART /HELPER=1`，静默时加 `/VERYSILENT /SUPPRESSMSGBOXES`），最后清理动态状态及升级所有权清单中的新增文件。
- 文件使用 `uninsrestartdelete`：若存在 DLL 占用且原生卸载成功，返回 `ERROR_SUCCESS_REBOOT_REQUIRED`（`3010`），不调用 `RmShutdown` 或按进程名强杀应用。
- 保留 `%APPDATA%/Fcitx5` 用户数据及共享 VC++/Windows App Runtime。正式安装请使用上述卸载入口，而非开发用 PowerShell 脚本。

## 构建、CI 与自动化验证

本地打包命令见 [build.md](build.md#build-installer-packages)。打包会向 `-Prefix` 写入 `setup/managed-install` 并复制许可证，应始终使用独立 staging 目录。CI 通过 winget 安装 Inno Setup 7 并校验主版本为 7。

| Job | 流程 |
| --- | --- |
| `build-x64` | 使用固定 LLVM `22.1.8` 构建 `base` 并复用 SDK 构建 `full` 全部插件；验证温州话、额外码表和注音提交及注音模型保存/重载，上传两套运行树与共享测试/Settings artifact |
| `test-x64` | 运行脚本与模拟注册检查回归、图标校验、默认 CTest、拼音/TSF/Settings/五笔/输入法列表、引擎翻译迁移与词库验证 |
| `test-x86` | 使用固定 LLVM `22.1.8` 构建/测试 x86 TSF，运行 x86 探针、双架构并发 Pipe 与部署审计 |
| `package-installer-x64` | `base`/`full` × 含/不含运行库四路矩阵；核对运行库 SHA256，运行 `test-build-installer.ps1`、`test-installer-wizard.ps1`、`test-installer-restart.ps1`，生成安装包并核对 `full`/`base` 插件清单 |
| `installer-smoke-x64` | 对 `base`/`full` 不含运行库包执行 `/VERYSILENT /MACHINEONLY=1` 静默安装、双架构 COM/profile/现代 category 双向检查与静默卸载清理 |
| `release` | 全部上游 job 通过且在 `master`（提交信息不含 `!release`）时发布四个 EXE 与对应 SHA256（共 8 个附件）至 `latest` 预发布 |

当前 CI 不执行代码签名。CI/Nightly 安装包均声明 `UIELEMENTENABLED` 和 `IMMERSIVESUPPORT`，smoke 缺失现代声明会阻止发布。

已有自动化回归：

- `scripts/test-build-installer.ps1`、`scripts/test-installer-registration.ps1`、`scripts/test-reboot-upgrade-package.ps1`、`scripts/test-installer-wizard.ps1`、`scripts/test-installer-restart.ps1`。
- CTest：`test_setup`、`test_uninstall`、`test_startup`、`test_upgrade`、`test_upgrade_files`、`test_upgrade_registration`、`test_upgrade_activation`、`test_upgrade_shutdown`、`test_registration_views`。
- helper 日志写入执行用户的 `%LOCALAPPDATA%/fcitx5/installer-helper.log`，记录时间、PID/TID、命令/安装根、runtime-check、启用/默认设置、语言切换、激活 HRESULT 与 Core 就绪步骤。

## 待验收矩阵

| 场景 | 待验证结果 |
| --- | --- |
| Windows 10 1809 / 22H2 与干净 Windows 11 x64 | 含运行库的 `base`/`full` 包完成依赖安装、双架构注册、用户启用；Settings 正常打开，真实应用提交中文 |
| 不含运行库包 | 已装依赖时成功；缺失依赖时报错并补偿回滚 |
| 权限分支（管理员双击、标准用户经另一管理员 UAC、已提升终端、静默/SYSTEM） | 原始用户获得配置且 Core 保持普通权限；退出码准确区分失败、待初始化与 `3010` |
| 非中文系统、每应用输入模式、已有应用窗口 | 记录默认/激活与重开限制 |
| 重装、延迟升级、取消、配置失败、中途崩溃与 DLL 占用 / 多用户 RDP | 不强杀应用，无半套 IPC 文件，注册/偏好/延迟删除及重启后清理正确 |
| 用户更改默认输入法、已有词库与学习历史 | 升级/卸载保留用户选择与数据 |
| 32/64 位真实应用与代码签名 | 双架构加载正确 DLL；后续补充代码签名及完整发行源码/许可清单 |
