# Windows x86 应用支持设计与实现

## 当前状态

x64 Windows 使用独立 x64/x86 TSF 构建树，两个前端共享 x64 Core 和 Settings。构建、部署、打包、双 registry view 注册/注销及升级快照/恢复已接入；CI 运行 x86 CTest、跨架构 Pipe probe 和双架构机器级注册 smoke。真实 x86 应用输入、签名部署及完整升级/重启卸载列入后文验收矩阵。

| 宿主进程 | 加载的 TSF | Core |
| --- | --- | --- |
| x64 | `tsf/fcitx5-x86_64.dll` | 共享 x64 Core |
| x86 | `tsf/x86/fcitx5-x86.dll` | 共享 x64 Core |

TSF 是宿主进程内的 COM DLL，位数必须匹配宿主；`ITfContext`、edit session 和文本存储留在各自进程。Core 通过 Named Pipe 处理引擎请求，协议使用固定宽度字段和有界长度。不同前端各自维护 context、generation 和焦点，Core 校验连接归属；一个宿主退出后，其他连接继续工作。

目标平台为 x64 Windows 11。32 位 Windows 和原生 ARM64 TSF 需另行适配。微软的双架构 TSF 说明见 [64-bit Considerations](https://learn.microsoft.com/zh-cn/windows/win32/tsf/64-bit-platform-considerations)。

## 构建与部署

统一入口默认构建两个架构：

```powershell
./scripts/build-and-deploy.ps1 -Prefix ./dist/dual-tsf -BuildRoot ./build/dual-tsf
```

x64 使用 `BuildRoot/tsf`，x86 使用 `BuildRoot/tsf-x86`。`-TsfArchitectures x64` 可生成仅含 x64 前端的开发树；已有双架构树要求同时更新两个前端。独立构建使用：

```powershell
./scripts/build-tsf.ps1 -Architecture x64 -BuildRoot ./build/tsf-x64 -ModernApps
./scripts/build-tsf.ps1 -Architecture x86 -BuildRoot ./build/tsf-x86 -ModernApps
ctest --test-dir ./build/tsf-x64 --output-on-failure
ctest --test-dir ./build/tsf-x86 --output-on-failure
```

脚本初始化匹配架构的 VS/Windows SDK/ATL 环境，使用 `cmake/tsf-x64.cmake` 或 `cmake/tsf-x86.cmake`。CMake 根据编译器探测架构并核对 `TSF_ARCH`；两个构建树分别生成 `fcitx5-x86_64` 和 `fcitx5-x86` 目标。两端均静态链接 MSVC CRT，各自编译翻译、日志、资源和测试。部署前审计 PE machine 及普通/延迟 imports：x64 为 AMD64 `0x8664`，x86 为 i386 `0x014c`。

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

`installationRootForAddress()` 支持两种 TSF 路径，使前端定位同一 Core、Settings、翻译目录和安装状态。x86 前端可按现有桌面策略请求启动 x64 Core，无需 x86 Core 或额外 VC++ 运行库。提权 TSF 的服务操作通过已运行宿主执行，详见 [UAC 身份与服务控制](windows-uac-ipc-design.md)。Sandboxie/AppContainer 继续使用各自宿主策略。

## 注册与升级

两个 DLL 使用相同 CLSID、语言 profile GUID 和 category 集合。COM 的 `InprocServer32` 分别进入 64/32 位 registry view，精确指向对应 DLL；CTF 的 profile/category 为共享元数据。

| DLL | 注册器 |
| --- | --- |
| x64 | `%WINDIR%/System32/regsvr32.exe` |
| x86 | `%WINDIR%/SysWOW64/regsvr32.exe` |

开发注册脚本的 `-DllPath` 根据 PE machine 选择注册器，操作说明见 [注册与 DLL 管理](build.md#registration-and-dll-management)。正式安装由 helper 注册 x64，并通过匹配位数的注册器处理 x86。profile 图标允许指向同一安装中的任一架构 DLL。注销时，如果 peer COM server 仍存在则保留共享元数据，最后一个前端注销时清理。

打包要求两个 DLL 位数正确，并把它们纳入运行文件摘要、占用和所有权清单。延迟升级整体暂存与提交，注册快照/恢复同时处理双 view；任一前端占用会影响整套提交。卸载分别处理两个 view，并按安装路径校验归属。详见 [安装器](windows-installer-design.md) 与 [重启升级](windows-reboot-upgrade-design.md)。

## 自动化验证

| 入口 | 覆盖 |
| --- | --- |
| 各架构默认 CTest | DLL/COM 激活、协议、编辑/候选、运行库 imports、服务/身份和升级测试 |
| `test_registration_views` | 双 view 路径、共享元数据和注册归属逻辑 |
| `scripts/test-pinyin.ps1 -Architecture x86` | x86 IPC/Settings/TSF probe 对接隔离配置的 x64 Core |
| `scripts/test-cross-arch.ps1` | x86/x64 客户端并发、同一 Core PID、对端断线隔离 |
| `scripts/test-deploy-tsf.ps1` | 双架构部署、PE machine 与错误位数拒绝 |
| `scripts/test-installer.ps1 -ModernApps` | 专用 CI runner 中机器级双架构 COM/profile/category 安装与卸载 |

例如，在没有用户 Core 运行时执行：

```powershell
./scripts/test-pinyin.ps1 -Prefix ./dist/dual-tsf -TsfBuild ./build/tsf-x86 -Architecture x86 -PeerTsfBuild ./build/tsf-x64
```

该 runner 调用跨架构并发测试，仅停止自己启动的 Core。TSF probe 使用实际 DLL、Windows TSF context 和显式适配器回调；真实系统派发按下表另行验收。

## 真实宿主验收与诊断

| 场景 | 验收内容 |
| --- | --- |
| 普通 x64/x86 文本应用 | 正确 DLL 激活、拼音预编辑/提交、候选、全局快捷键和语言栏 |
| 应用启动的子进程 | 核对实际接收键盘的 PID、架构及 TSF context |
| 提权 x86/x64 宿主 | 普通 Core 的 linked-token 授权、服务命令、停止状态与重连 |
| 多宿主与断线 | 并发输入、单宿主退出、Core 重启后焦点/输入法恢复 |
| 升级与卸载 | 单独占用任一 DLL、双 view 回滚、重启后清理 |
| 现代应用、混合 DPI/主题 | 系统派发、候选 UIElement/窗口定位与显示 |

启用详细日志后，按真实宿主 PID 收集 `tsf-PID-YYYYMMDD.log`，关联 `pipe_connect`、`tsf_state`、`context_policy`、`key_result` 和 Core 的 `pipe_identity`。Core 的 `client` 是连接序号，授权事件的 `peer_pid` 可帮助对应宿主。日志开关依赖 Core 回复；握手失败、日志关闭或目录不可写时可能没有 TSF 文件，应结合 `ipc_probe --diagnose` 和限频的 OutputDebugString 诊断。

应用使用 Raw Input、禁用 TSF 或没有文本服务 context 时，应先核对输入入口和激活状态。x86 文件部署正确后，仍需按实际控件逐项验证输入行为。
