# Windows x86 应用支持设计与实现

## 当前状态

x64 Windows（Windows 10 1809 及以上）使用独立的 x64/x86 TSF 构建树，两个前端共享同一 x64 Core 和 Settings。构建、部署、打包、双 registry view 注册/注销及延迟升级快照/恢复均已接入；CI 使用固定 LLVM `22.1.8` 运行 x86 CTest、跨架构 Pipe probe、双架构部署审计与机器级双架构注册 smoke。真实 x86 应用输入、代码签名与完整升级/重启卸载待系统验收。

| 宿主进程 | 加载的 TSF | Core |
| --- | --- | --- |
| x64 | `tsf/fcitx5-x86_64.dll` | 共享 x64 Core |
| x86 | `tsf/x86/fcitx5-x86.dll` | 共享 x64 Core |

TSF 作为宿主进程内 COM DLL，位数必须匹配宿主；`ITfContext`、edit session 与文本存储留在各自进程。Core 通过定宽字段与有界长度的 Named Pipe 协议处理所有前端请求，按连接隔离 context、generation 与焦点。32 位 Windows 与原生 ARM64 TSF 需另行适配。参见微软 [TSF 64-bit Considerations](https://learn.microsoft.com/zh-cn/windows/win32/tsf/64-bit-platform-considerations)。

## 构建与部署

统一构建脚本默认同时构建 x64 与 x86 前端：

```powershell
./scripts/build-and-deploy.ps1 -Prefix ./dist/dual-tsf -BuildRoot ./build/dual-tsf
```

x64 使用 `BuildRoot/tsf`，x86 使用 `BuildRoot/tsf-x86`。开发阶段可传入 `-TsfArchitectures x64` 仅构建 x64（已有双架构部署树要求同步更新双前端）。独立构建命令：

```powershell
./scripts/build-tsf.ps1 -Architecture x64 -BuildRoot ./build/tsf-x64 -ModernApps
./scripts/build-tsf.ps1 -Architecture x86 -BuildRoot ./build/tsf-x86 -ModernApps
ctest --test-dir ./build/tsf-x64 --output-on-failure
ctest --test-dir ./build/tsf-x86 --output-on-failure
```

脚本自动初始化匹配架构的 VS/Windows SDK/ATL 环境，采用 `cmake/tsf-x64.cmake` 或 `cmake/tsf-x86.cmake` 并核对 `TSF_ARCH`。两个前端分别生成 `fcitx5-x86_64.dll`（AMD64 `0x8664`）和 `fcitx5-x86.dll`（i386 `0x014c`），均静态链接 MSVC CRT，各自编译翻译、日志、资源与测试，并在部署前审计 PE machine 与普通/延迟 imports。

安装采用单根目录：

```text
<ProgramFiles>/Fcitx5/
  bin/Fcitx5.exe
  tsf/fcitx5-x86_64.dll
  tsf/x86/fcitx5-x86.dll
  settings/Fcitx5Settings.exe
  share/locale/...
  setup/...
```

`installationRootForAddress()` 兼容两处 TSF 路径，两个前端共用同一 Core、Settings、翻译目录与安装状态，无需 x86 Core 或额外 VC++ 运行库。提权 TSF 通过已运行宿主 Core 执行服务操作（见 [UAC 方案](windows-uac-ipc-design.md)）；Sandboxie 与 AppContainer 沿用各自宿主策略。

## 注册与升级

两个 DLL 共享相同的 CLSID、语言 profile GUID 与 category 集合。COM `InprocServer32` 分别写入 64/32 位 registry view 指向对应 DLL；CTF profile/category 为共享元数据。

| DLL | 注册器 |
| --- | --- |
| x64 | `%WINDIR%/System32/regsvr32.exe` |
| x86 | `%WINDIR%/SysWOW64/regsvr32.exe` |

开发脚本的 `-DllPath` 按 PE machine 自动选择注册器（见 [build.md](build.md#registration-and-dll-management)）；正式安装由 helper 注册 x64 并通过 `SysWOW64/regsvr32.exe` 注册 x86。注销时若另一架构 COM server 仍在则保留共享元数据，最后一个前端注销时一并清理。

安装包校验双 DLL 位数并将其纳入运行文件摘要、占用检查与所有权清单；延迟升级的 COM 暂停、快照、提交与卸载均同时处理双视图（详见 [安装器](windows-installer-design.md) 与 [延迟升级](windows-reboot-upgrade-design.md)）。

## 自动化验证

| 入口 | 覆盖范围 |
| --- | --- |
| 各架构默认 CTest | DLL/COM 激活、协议、编辑/候选、状态菜单、PE imports 审计、服务/身份与升级测试 |
| `test_registration_views` | 双视图路径、共享元数据与注册归属逻辑 |
| `scripts/test-pinyin.ps1 -Architecture x86` | x86 IPC/Settings/TSF probe 对接隔离 x64 Core |
| `scripts/test-cross-arch.ps1` | x86/x64 并发客户端、同 Core PID 与对端断线隔离 |
| `scripts/test-status-menu.ps1 -PeerTsfBuild` | x86/x64 状态菜单动作与并发 Pipe 验证 |
| `scripts/test-deploy-tsf.ps1` | 双架构部署、PE machine 与错误位数拒绝 |
| `scripts/test-installer.ps1 -ModernApps` | CI 机器级双架构 COM/profile/category 安装与卸载 |

在无现有用户 Core 时可运行跨架构回归：

```powershell
./scripts/test-pinyin.ps1 -Prefix ./dist/dual-tsf -TsfBuild ./build/tsf-x86 -Architecture x86 -PeerTsfBuild ./build/tsf-x64
```

## 真实宿主验收与诊断

| 场景 | 验收内容 |
| --- | --- |
| 普通 x64/x86 文本应用 | 正确 DLL 激活、拼音预编辑/提交、候选、全局快捷键与语言栏 |
| 应用子进程 | 核对实际接收键盘的进程 PID、架构与 TSF context |
| 提权 x86/x64 宿主 | 普通 Core 的 linked-token 授权、服务命令、停止状态与重连 |
| 多宿主与断线 | 并发输入、单宿主退出、Core 重启后焦点与输入法恢复 |
| 升级与卸载 | 单独占用任一 DLL、双视图回滚与重启后清理 |
| 现代应用、混合 DPI/主题 | 系统派发、候选 UIElement/窗口定位与渲染 |

排查时启用详细日志并按宿主 PID 收集 `tsf-PID-YYYYMMDD.log`，关联 `pipe_connect`、`tsf_state`、`context_policy`、`key_result` 及 Core 的 `pipe_identity`（`peer_pid` 对应宿主 PID，`client` 为连接序号）。握手早期失败可结合 `ipc_probe --diagnose` 与限频 `OutputDebugString` 定位。应用若使用 Raw Input、禁用 TSF 或无文本服务 context，需先核对输入入口与激活状态。
