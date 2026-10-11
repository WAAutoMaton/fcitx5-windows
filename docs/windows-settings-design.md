# Windows 输入法设置设计与实现

更新日期：2026-10-11。当前协议为 v6。设置窗口使用左侧有序输入法列表、右侧共享设置面板；拼音页还包含只读“词库”tab。词库目录、热重载和验证见 [pinyin.md](pinyin.md#third-party-dictionaries)。本文说明当前实现和仍需实测的范围。

## 当前方案

使用语言栏输入模式项提供右键菜单，包含“输入法设置”“用户数据文件夹”“重启服务”“关闭服务”“关于”。设置界面使用独立的 `Fcitx5Settings.exe`，采用 WinUI 3、C++/WinRT 和 XAML；通过用户/session 级 Named Pipe 与 Core 通信。当前目标是 Windows 11 和已验证的 AMD64 部署。

默认列表为英文键盘和拼音。用户可以添加已安装的 chinese-addons 输入法、删除和上下调整顺序，至少保留一个可用输入法。列表修改即时保存；页面选项在“应用”时保存，成功后窗口保持打开。切换页面时有未应用草稿会询问保存、放弃或取消；关闭窗口放弃未应用草稿。Core 是配置的唯一写入者和运行时状态的管理者。

## 实现位置

- `win32/tsf/langbaritem.cpp`：实现 `ITfLangBarItemButton`，显示当前输入法图标/标签和名称，左键轮换输入法；原生右键菜单与 `InitMenu` / `OnMenuSelect` 共用五项命令。
- `win32/tsf/aboutwindow.cpp`：实现“关于”窗口，显示架构、构建信息、许可证链接和仓库主页。
- `win32/tsf/pollstate.cpp`、`servicecontrol.cpp`：TSF 消息调度、100 ms 状态轮询和后台服务/目录操作。
- `win32/ipc/protocol.h`、`inputmethodprotocol.h`：v6 framing、输入法目录、选项描述、设置消息、词库状态及运行时代次；全局请求与 context 请求分别校验。
- `src/main.cpp`：初始化 Core、预热拼音并创建 Windows 设置管理器；完整内存输入法组由 `src/windowsinputmethods.cpp` 按已安装可用条目建立。
- `src/windowssettings.cpp`、`windowsfrontend.cpp`：权威配置保存、真实 profile 应用、中文 entry 切换与 context 同步。
- `src/windowsinputmethods.cpp`：枚举输入法、声明共享面板、校验配置、列表轮换与删除保护。
- `src/windowsdictionaries.cpp`：扫描第三方词库目录，在 Core 主事件循环触发上游重载并查询实际加载状态。
- `win32/settings/SettingsWindow.cpp`：通过 `XamlReader` 创建有序输入法列表、共享设置面板和拼音词库 tab，后台执行 IPC，支持应用选项、草稿处理和连接重试。
- `chinese-addons/im/pinyin/`：复用上游 `setUseShuangpin()`、`ShuangpinProfile`、`setConfig()` 和已安装的 `shuangpin.conf`。

子模块版本保持固定；词库加载状态查询扩展记录在主仓库 `patches/chinese-addons-windows.patch`，不创建本地子模块提交。

## UI 框架依据

| 方案 | 与本项目的关系 | 结论 |
| --- | --- | --- |
| WinUI 3 / Windows App SDK | 微软原生 Fluent UI；XAML 支持缩放、主题、键盘导航；需要独立运行时和 XAML 构建工具 | 已采用 |
| 系统 UWP XAML / WinUI 2 / XAML Islands | 可以使用系统 XAML，但 WinUI 2 本身也是 NuGet 依赖；宿主集成增加窗口、线程和部署复杂度 | 不作为本次首选 |
| Win32 / Common Controls + DWM | 主要依赖系统组件，部署较轻；现代控件外观与 DPI 布局需要更多手动工作 | 仅在禁止附带 UI 运行时时作为备选 |
| WPF | 支持成熟的数据绑定和 DPI，但引入 .NET，Windows 11 风格需要额外选择与调整 | 本项目已有 C++，优先 WinUI 3 C++/WinRT |

WinUI 3 是 Windows App SDK 的 UI 部分，与操作系统和 Windows SDK 分开发布。只支持 Windows 11 也不能假定目标机器安装了匹配版本的 Windows App SDK runtime。

按用户选择采用 unpackaged + framework-dependent 文件夹部署。WinUI NuGet 固定为 `1.8.260803003`，运行时元数据固定为 `1.8.260921001`；需要当前用户安装 x64 Framework 和 DDLM 包 `8000.994.2142.0` 或更新兼容版本，以及 Visual C++ 运行库。应用启动时显式调用 bootstrap；缺少运行时显示错误，不自动安装。设置程序无需注册 MSIX。

统一安装器已提供含/不含外部运行库的两种 x64 包；含运行库版本在安装阶段部署依赖并检查发起用户的 Settings runtime，不含版本要求预先准备依赖。构建/部署脚本仍不安装系统运行库。安装流程及真实系统验证边界见 [Windows 安装实现](windows-installer-design.md)。

独立 MSBuild `.vcxproj` 使用 MSVC、C++/WinRT 和 NuGet，由 `scripts/build-settings.ps1` 调用。界面通过 WinUI `XamlReader` 加载 XAML，复用系统控件和元数据提供者，不要求 UWP 工具工作负载。Core 的 MSYS2/Clang GNU 工具链和 TSF 的 Clang/Ninja 工程继续独立构建。

参考：

- [WinUI 3 的定位](https://learn.microsoft.com/en-ca/windows/apps/winui/winui3/)
- [Windows App SDK 部署概览](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/deploy-overview)
- [框架依赖应用的运行时初始化](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/use-windows-app-sdk-run-time)
- [C++/WinRT 的 MSBuild 与 XAML 工具](https://learn.microsoft.com/en-us/windows/apps/develop/cpp-winrt/intro-to-using-cpp-with-winrt)

## 进程与生命周期

```mermaid
flowchart LR
    T[应用进程中的 TSF DLL] -->|右键菜单 / OpenSettings| C[独立 Core 进程]
    C -->|固定绝对路径启动| U[独立 WinUI 3 设置程序]
    U -->|输入法列表 / 引擎选项 / GetDictionaries| C
    C --> P[Pinyin addon / libime]
    C --> E[Table addon / libime]
    C --> F[用户配置文件]
    T -->|PollState / 设置代次| C
```

- TSF DLL 只增加轻量的菜单与打开设置请求。WinUI、XAML 初始化和窗口消息循环位于独立进程，设置程序关闭或崩溃不影响应用中的 TSF 生命周期。
- Core 根据自己的安装树定位 `settings/Fcitx5Settings.exe`，使用固定绝对路径启动，不搜索 PATH，不接受客户端传入可执行文件路径。
- 不修改 Core 的进程级 DLL 搜索环境。设置程序拥有自己的运行库部署目录，两端只传递协议数据，不跨进程传递引擎对象或分配器。
- 设置程序使用用户/session 级 named mutex 和 event 保证单实例；重复点击激活已有窗口，不依赖 AppLifecycle 的额外服务包。
- 打开设置不自动改变当前输入法。窗口是普通可激活的顶层窗口，不把用户正在编辑的应用作为模态 owner。
- TSF 激活时在工作线程后台启动安装树的 Core，Settings 按需启动。协议不匹配、设置程序缺失或启动失败时显示错误。DLL 应部署在安装树的 `tsf` 目录，固定定位相邻 `bin/Fcitx5.exe`。
- 重启服务正常停止并重启/启动 Core，原本打开的 Settings 在 Core 就绪后重新打开；关闭服务正常停止两者，并把当前登录会话的自动启动暂停状态保存到 `%LOCALAPPDATA%/fcitx5`。当前用户/session/logon LUID 级互斥防止重复启动，失败时共享 10 秒重试间隔。
- Core 在主事件循环处理退出信号，Settings 在 UI 线程关闭。TSF 所在进程本身是 Settings 时，以独立的 `Fcitx5 --restart-services/--stop-services` 短暂控制进程等待其退出，避免宿主退出等待自身。正常关闭超时返回错误，不强制杀进程。
- `OpenSettings` 回复只确认启动/激活请求结果，不等待 WinUI 初始化和用户操作。慢启动不得阻塞 Core 事件循环。

## 右键菜单

菜单使用原生 `HMENU` / `TrackPopupMenuEx`，由 TSF 所在线程显示，内容为“输入法设置”“用户数据文件夹”“重启服务”“关闭服务”“关于”。左键与 Ctrl+Space 按用户列表顺序循环切换，右键不触发切换。指示栏通过 WIC 加载已安装的输入法 PNG 图标，加载失败时使用该输入法的紧凑标签图标；英文键盘使用 A。

“用户数据文件夹”在后台任务中通过 Windows Roaming AppData known folder 定位 `%APPDATA%/Fcitx5`，目录不存在时创建，再通过 Shell 打开。该目录同时包含 `config/fcitx5` 设置和 `pinyin` 用户词典、学习历史；服务关闭时仍可打开，不启动 Core 或 Settings，不涉及 IPC 协议变更。

“关于”由 TSF 的消息窗口在所属线程打开原生非模态窗口，不需要 Core、Settings 或 Windows App SDK runtime。显示 `Fcitx5 for Windows`、DLL 编译目标架构、CI 编号、commit 与带时区的 commit 时间；窗口下方保留一个可复制的仓库主页 URL。GPL-3.0 许可证和仓库主页按钮调用系统浏览器；Git SSH 地址转换为 HTTPS 主页。非 HTTP(S) 的 origin 仍展示在主页 URL 中，但不启用主页按钮。

`win32/cmake/buildinfo.cmake` 在每次 TSF 构建时读取 Git 元数据，生成内容未改变时不触发重新编译。GitHub Actions 读取 `GITHUB_RUN_NUMBER`；本地构建显示“本地编译（无 CI 信息）”，缺失 Git/origin/commit 时显示不可用说明。每个 TSF 实例只保留一个关于窗口，重复点击激活已有窗口；停用和析构均关闭窗口。窗口按自身 PMv2 DPI 缩放并恢复调用线程的 awareness，提供 Tab、Enter、Escape 和关闭按钮；不修改协议或注册元数据。

`LangBarItem::OnClick(TF_LBI_CLK_RIGHT, ...)` 接入右键弹出路径；`InitMenu(ITfMenu*)` 与 `OnMenuSelect()` 也提供相同菜单项和统一命令处理。命令选中后先发送消息给 dispatch window，再由后台任务执行相应服务、`OpenSettings` 或目录操作，避免在菜单循环中执行 IPC。

当前 style 为 `TF_LBI_STYLE_BTN_MENU | TF_LBI_STYLE_BTN_TOGGLE | TF_LBI_STYLE_HIDDENSTATUSCONTROL`，不声明 `TF_LBI_STYLE_TEXTCOLORICON`，以保留输入法 PNG 图标的原始颜色。提示文本显示当前输入法名称。`test_langbar` 验证菜单内容、命令派发、不同输入法标签/提示文本和停用后的迟到点击。Windows 11 实际输入指示器及浮动语言栏的右键回调和外观仍需实测；已有指示器正常的用户反馈不能代替新图标外观及右键派发验证。

菜单显示期间保持对象/DLL 引用；owner 窗口需满足原生菜单的焦点和取消行为；退出菜单、停用、detach 后释放资源。迟到消息必须验证当前激活绑定，不能作用于新实例。不能缓存回调参数的裸指针。

参考：[OnClick](https://learn.microsoft.com/en-us/windows/win32/api/ctfutb/nf-ctfutb-itflangbaritembutton-onclick)、[InitMenu](https://learn.microsoft.com/en-us/windows/win32/api/ctfutb/nf-ctfutb-itflangbaritembutton-initmenu)、[语言栏 style](https://learn.microsoft.com/en-us/windows/win32/tsf/tf-lbi-style--constants)。

## 设置窗口

默认窗口为 980 × 720 个有效像素，最小跟踪尺寸为 720 × 480，创建/缩放时按窗口 DPI 转为 HWND 物理像素。选项可纵向滚动，词库列表随布局伸缩。

| 内容 | 控件与行为 |
| --- | --- |
| 标题 | “输入法设置”，使用系统标题栏 |
| 输入法列表 | `ListView`：按用户顺序显示图标和名称；添加、删除、上移、下移 |
| 页面 | 根据 entry 的 panel 标识显示拼音、码表或键盘设置；拼音/双拼共用拼音面板，码表共用控件布局但选项值按各自 entry 保存 |
| 选项 | 通过上游 `Configuration::dumpDescription()` 生成布尔开关、整数步进框、枚举菜单、文本/按键列表控件；递归展示模糊拼音等子配置 |
| 双拼键位 | `ComboBox`：小鹤、自然码、微软、紫光、智能 ABC、中文之星、拼音加加、国标 |
| 全拼时的键位 | 上游拼音 entry 隐藏双拼专用选项；双拼 entry 显示这些选项 |
| 底部操作 | “重置为默认值”“关闭”“应用”；重置先更新草稿，应用后保存 |
| 状态 | 加载中、正在保存、连接失败、保存失败、配置冲突 |
| 连接重试 | 连接失败后显示“重试”，重新读取 Core 设置 |
| 词库页 | 展示目录及加载中/已加载/失败/停用状态，显示期间每两秒查询 Core |
| 词库目录按钮 | 创建并打开第三方词库目录，Core 无法连接时仍可使用；无导入/删除/启停控件 |

窗口打开后读取 Core 的实际设置；不使用 UI 本地默认值覆盖已有配置。第一次没有 Windows 设置文件时，默认全拼，键位从当前 Pinyin 配置继承。自定义双拼暂不提供编辑或导入；如果检测到已有 Custom 配置，应保留并明确显示现有自定义方案，不能静默覆盖为内置键位。

加载/保存期间禁用输入法和选项操作。保存失败保留草稿并显示错误；冲突要求重新加载，以免旧草稿覆盖其他客户端。使用原生键盘导航、Automation 属性、系统主题资源和 `MicaBackdrop`；深浅色、高对比度、文本缩放及背景回退仍需全面实测。

HiDPI 使用 XAML 有效像素和独立进程的 Per-Monitor DPI awareness；窗口初始大小/位置涉及 HWND 物理坐标时显式换算。验证不同 DPI 显示器之间拖动、任务栏位置、屏幕工作区和放大文本，不在 TSF DLL 内修改宿主进程的 DPI awareness。

参考：[XAML 有效像素与缩放](https://learn.microsoft.com/en-us/windows/apps/design/layout/screen-sizes-and-breakpoints-for-responsive-design)、[Mica](https://learn.microsoft.com/en-us/windows/apps/design/style/mica)。

## IPC 与配置所有权

v6 增加输入法目录、有序选择、配置描述和循环/指定切换，并在 `KeyReply` 返回实际 entry、名称、标签及图标路径。必须同步构建 Core、TSF DLL 和设置程序。以下全局消息强制 `contextId=0`；`CycleInputMethod`、`SelectInputMethod` 则验证 context 归属和焦点。

| 请求 | 内容 / 回复 |
| --- | --- |
| `OpenSettings` | 无可执行路径参数；返回启动/激活结果 |
| `GetSettings` | 返回 `PinyinSettings`（scheme、profile、revision）及 `pinyinAvailable` / `shuangpinAvailable` |
| `SetSettings` | 提交 scheme、profile、revision（草稿读取时的版本）；返回实际配置、新 revision 或错误 |
| `GetDictionaries` | 返回第三方词库目录、实际发现的扩展词库路径和加载状态 |
| `GetInputMethods` / `SetInputMethods` | 目录及有序选择，字段包含 id、name、icon、iconLabel、panel、available、keyboard；写入使用共享 revision，拒绝空列表和重复 id |
| `GetMethodConfig` / `SetMethodConfig` | 获取引擎声明的可编辑选项或提交 path/value；校验声明、类型、范围、枚举、按键语法及 revision |

scheme 使用明确枚举；profile 使用稳定的协议 ID 或上游字符串标识，不依赖 ComboBox 索引。映射到 `Xiaohe`、`Ziranma`、`MS`、`Ziguang`、`ABC`、`Zhongwenzhixing`、`PinyinJiajia`、`GB`，服务器严格校验。

协议 ID、引擎枚举和 `RawConfig` 字符串分别映射。尤其国标的引擎枚举是 `GB`，但 `ShuangpinProfile` 的实际序列化值是 `GB Standard`；不能直接写入枚举标识符或中文 UI 标签。配置更新后必须读取结果验证，防止无效字符串被配置加载器拒绝却仍回复成功。

设置程序独立建立 Pipe 连接，直接编译 `win32/tsf/pipeclient.cpp`，复用 `win32/ipc/protocol.h` 和 `transport.h`，不链接整个 `tsf` 静态库。I/O 通过 C++/WinRT `resume_background()` 放到后台线程，返回 UI apartment 后更新控件。

错误返回区分引擎缺失、非法设置、版本冲突、持久化失败和应用失败。保存超时可能已经在服务端生效，因此重连后先 `GetSettings` 核对结果，不盲目重放写请求。重复提交同一已生效配置幂等成功；请求的 `revision` 防止独立客户端的旧草稿覆盖新配置。

全局设置允许当前用户的独立连接访问，沿用 SID/session 命名、当前用户 ACL 和拒绝远程访问。仅接受已枚举的输入法及其公开选项，不提供任意文件写入、执行路径或任意 addon 调用。

## 真正切换引擎

1. 启动时枚举已安装的 entry 和 addon。拼音、双拼、五笔字型、五笔拼音、仓颉、电报码、二笔、冰蟾、晚风、自然码可加入用户列表；缺失引擎或码表时标为不可用，不允许新添加。
2. Core 保存有序列表，每个 TSF 实例保存当前 entry 并在焦点切换和重连时恢复。Windows compartment 的布尔值由实际 entry 是否为键盘推导；外部开关只在用户已选择的键盘/非键盘条目之间切换，没有对应条目时保持当前输入法。
3. 通过 addon 的 `getConfigForInputMethod` / `setConfigForInputMethod` 读写公开配置。拼音/双拼共用上游配置，码表按各自输入法保存。引擎未声明或隐藏的选项不能通过协议修改。
4. 在 Core 主事件循环内执行设置操作。Pipe worker 不直接操作 Instance、addon 或 context。
5. 移除当前条目时回退到仍选中的可用条目。列表顺序和 Core 的内部组顺序分开：内部组第一项保留 Fcitx 的键盘槽位，但它不强迫用户保留英文键盘。
6. scheme 或双拼键位改变时，取消旧方案尚未提交的预编辑和候选，不自动选词提交；已经提交的文本保持有效。相同设置不重复 reset。
7. Ctrl+Space 在实际按键回调中向 Core 请求轮换，自动重复和释放不再次轮换；测试回调仍只观察状态。循环切换和删除当前条目会丢弃旧预编辑，不自动提交。

## 扩展约定

复用 Fcitx addon loader 和输入法 entry 枚举，不维护封闭的协议输入法枚举。entry 默认以 addon 名声明 panel；键盘为 `keyboard`，拼音/双拼为 `pinyin`，码表为 `table`。entry 的 `inputmethod/*.conf` 可通过 `InputMethod/WindowsSettingsPanel` 和 `WindowsIconLabel` 覆盖共享面板标识和图标后备标签。Settings 对未知 panel 仍使用引擎公开描述生成通用页。未来插件仍需遵循对应 Core 工具链/ABI，不把插件对象传到 TSF 或 Settings。

`patches/chinese-addons-windows-table.patch` 在 Windows 中启用已带数据的仓颉条目，并将内置码表路径优先解析到部署树 `share/libime`；码表不存在时配置接口返回不可用。兼容补丁由 `apply-pinyin-patches.ps1` 按固定版本幂等应用。

设置全局生效，不要求重启 Core、重新注册 DLL 或重启应用。只增加设置入口的正常功能更新不应被设计成必须重新注册的操作；若同时修改注册元数据，仍按项目现有规则单独处理。

## 持久化与状态一致性

`WindowsSettings` 保存用户级 `conf/windows.conf` v2，包含 scheme、profile、共享 revision、`InputMethods` 有序列表和 `Options` 引擎配置。默认路径为 `%APPDATA%/Fcitx5/config/fcitx5/conf/windows.conf`。v1 文件按原全拼/双拼方案迁移为英文键盘加对应拼音条目；下次成功保存写 v2。此文件是 Windows 设置的权威来源，设置程序不直接写文件。

引擎还会保存上游 `conf/pinyin.conf` 或 `table/<id>.conf`，其接口不返回存储结果。这些文件是兼容镜像；Core 先原子保存 Windows 权威记录，再调用引擎并核对结果，启动时重新应用权威配置。

保存流程先校验能力和 revision，保留旧配置快照，安全写入 Windows 权威记录，再更新引擎配置并核对读取结果，最后发布新设置代次并切换 contexts。此顺序来自实际代码约束：libime 的 profile 更新会发出 `optionChanged` 并清空解码上下文，回滚配置不能恢复已经清空的预编辑。存储失败必须在调用 addon 前返回。引擎应用失败恢复旧配置和旧权威记录，同时 reset contexts 并更新独立的运行时代次；此异常路径会取消预编辑。启动时以最后一个完整权威记录为准，不能合并两个文件中的半次更新。保存过程中进程被终止时，重启可能应用已写入但尚未回复的设置；UI 超时后查询实际结果。

配置目录仍由 `StandardPaths` 决定；Windows 权威记录由主仓库 `atomicfile.h` 使用同目录临时文件、`FlushFileBuffers` 和 `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` 替换。`test_settingsfile` 验证重复覆盖、只读失败和临时文件清理；`settings_probe --read-only-file PATH` 验证失败不清空实际解码预编辑。断电/文件系统故障恢复尚未验证。

`KeyReply::settingsRevision` 是独立的运行时代次，不复用每次 snapshot 都递增的文本 revision，也不同于 `PinyinSettings::revision`。应用失败后需要 reset 时运行时代次也会增长，因此不要求它永远等于持久化配置 revision。TSF 在处理回复时识别代次，旧异步会话保留 commit、清空 preedit/candidates。TSF 在 edit pending 时仍轮询，及时观察新设置；焦点切换时仍遵循既有 context/generation 隔离。

## 文件与构建范围

| 范围 | 当前文件与职责 |
| --- | --- |
| TSF 菜单与同步 | `win32/tsf/langbaritem.*`、`pollstate.cpp`、`pipeclient.*`、编辑会话相关状态 |
| 协议 | `win32/ipc/protocol.h`：设置序列化、结构化错误和词库查询 |
| Core | `src/main.cpp`、`windowsfrontend.*`、`windowssettings.*`、`windowsdictionaries.*` |
| 设置程序 | `win32/settings/`：独立 `.vcxproj`、运行时 XAML、窗口和客户端 |
| 部署 | `scripts/build-settings.ps1`、`deploy-tsf.ps1`、`build-and-deploy.ps1`：构建并联合部署至运行树；系统运行时需另行准备 |
| 验证 | `win32/tests/`、拼音/词库/服务/UI 脚本；CI 统一构建包含独立 MSBuild Settings 工程，测试 job 复用运行树执行拼音/词库集成验证 |

Core 和 TSF 使用两套独立 CMake 工程；MSBuild 设置程序由统一脚本调度，不把 WinUI 工具要求强加给只构建 Core 的用户。统一入口暂存目录默认是 `build/all/prefix`，Settings 的 MSBuild 输出/缓存仍在 `win32/build/settings`，不受 `-BuildRoot` 影响。完整构建选项见 [build.md](build.md)。

## 验证与剩余范围

关键自动验证：

- `test_langbar`：菜单项文字和 ID、空指针、未知命令、左键行为、detach 后命令、菜单与 DLL 引用生命周期。
- `inputmethods_probe` / `scripts/test-inputmethods.ps1`：目录、共享面板/选项、真实五笔/拼音/双拼提交、轮换/重复/释放、删除保护、去掉英文键盘、活跃输入法删除、版本冲突和重启持久化。
- `scripts/test-settings-ui.ps1`：实际 WinUI 控件的添加、删除、排序、模糊拼音/双拼配置、词库状态和目录、最小窗口、最后一个输入法提示、重启和单实例；输出截图供检查。
- `test_protocol`：设置/词库消息往返、非法枚举、截断/额外字段、冲突错误编码和全局请求分类；真实 context 连接归属校验由 `ipc_probe` 覆盖，设置 revision 冲突由 `settings_probe` 覆盖。
- `ipc_probe`：基础全拼按键、候选、提交、模式和连接归属；`settings_probe` 验证全拼 `nihao`、小鹤 `nihc`、自然码 `nihk` 的真实“你好”候选/提交及方案切换。其他内置 profile 仍需选择键位差异明确的音节验证，不能只检查配置字段。
- `tsf_probe`：实际 DLL 的双拼文本插入、英文放行、SetMode 幂等、多个 context、设置变更时的迟到编辑与取消。
- 持久化：`test_settingsfile` 覆盖重复覆盖、只读失败和临时文件清理；`settings_probe --read-only-file PATH` 覆盖真实存储失败时保留预编辑，UI 自动化覆盖重启恢复。镜像不一致恢复、保存超时及设置程序崩溃的故障注入仍需补充验证。

实际 Windows 11 验证：任务栏指示器右键出现单条“输入法设置”，左键和 Ctrl+Space 按设置顺序轮换，图标/标签及提示名称跟随当前输入法；重复点击设置只显示一个窗口；记事本、浏览器和至少一个复杂文本应用中方案立即生效；100%/150%/200% DPI 和跨屏拖动、深浅色、高对比度、文本缩放、Explorer 重启及 Core 断连。

probes 的适配器回调不能代替实际 OS 菜单派发和真实应用验证。第一阶段不宣称 Windows 10、ARM64 TSF、双拼自定义方案和全部 Windows 输入兼容性已经受支持。

## 已记录的验证

- 设置功能的 AMD64 Core、TSF Release、WinUI Release 构建和当时的 8 项默认 CTest 已通过。后续候选窗、关于及安装/卸载回归扩展后，当前默认为 13 项；完整清单见 [TSF 测试](pinyin.md#build-and-test-tsf)，候选布局的历史验证范围见 [候选窗验证记录](windows-candidate-rendering.md#verification)。这些是既有验证记录，本次文档更新不代表重新执行 Core 集成 probes。
- `ipc_probe`、`settings_probe` 和扩展后的 `tsf_probe` 通过，真实全拼/小鹤/自然码中文提交、英文保持、存储失败保留解码上下文和迟到编辑取消均验证。
- 按用户明确授权安装当前用户的 x64 DDLM `8000.994.2142.0` 后，`scripts/test-settings-ui.ps1` 验证实际窗口、确定/取消、自然码/小鹤、重启恢复和单实例，截图在 `build/settings-ui-test`。窗口当前 200% 缩放正常；跨屏、文本缩放、高对比度和其他主题仍未全面验证。
- 未执行输入法注册/注销，真实任务栏指示器右键菜单派发尚未实测。
- 服务控制新增验证：`win32/build/service-tsf`、`dist/pinyin/tsf` 的实际 DLL 自动启动 Core、模拟菜单停止/重启、重新激活时保持暂停、按需启动 WinUI、重开原设置窗口、Settings 宿主委托退出和迟到 Settings 启动拒绝均通过。原有三个拼音 probes 和实际 WinUI 保存/取消、重启恢复、单实例重新通过，截图位于 `build/service-settings-ui-test`。
- 第三方词库验证包含真实启动加载、热增删改、Unicode 路径、坏文件、停用标记、全拼/小鹤/自然码候选和预编辑保留；WinUI 自动化覆盖实际加载/失败状态及 Explorer 目录打开。200% 缩放的正常/最小窗口截图位于 `build/dictionaries-settings-ui-test`，使用 `dist/dictionaries-test` 隔离部署。
