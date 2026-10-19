# Windows UI 国际化与全局设置设计

状态：已接入。Core、TSF 和 Settings 分别构建，翻译/配置/协议及免注册菜单、关于窗口回归已有覆盖。真实 WinUI 页面通过隔离命名空间和模拟 Core 验证；真实系统菜单派发、安装版服务重启和跨宿主刷新仍需验证。

## 目标与选型

Windows 自有 UI 统一使用 gettext 消息目录和 Boost.Locale，覆盖 Settings、语言栏右键菜单、关于窗口及相关错误、提示和无障碍名称。首批支持跟随系统、简体中文、繁体中文和英文。设置页使用现有 WinUI 3，菜单和关于窗口继续使用原生 Win32。

翻译源文件是 UTF-8 `.po`，运行时资源是外置 `.mo`。代码使用带上下文的英文原文，未翻译词条回退英文。Core 的上游引擎继续使用已有 libintl 和上游翻译目录；不把 Fcitx5 引擎库链接到 Settings 或 TSF。共享代码分别在三个工程中编译，不共享运行库二进制、C++ 对象或 locale 对象。

## 全局设置入口

左侧布局从上到下为输入法列表、添加/删除/上移/下移按钮行、全局设置入口；全局设置入口位于底部“重置为默认值”按钮上方。列表占据剩余高度，操作按钮与入口使用自动高度，不采用绝对坐标。

```text
左侧                           右侧
输入法                         全局设置
[有序输入法列表]
                               语言设置  [跟随系统语言 v]
[+] [删除] [上移] [下移]
[设置图标  全局设置]

重置为默认值                             关闭  应用
```

- 入口采用可呈现选中状态的原生导航项，包含系统设置图标和“全局设置”文字，AutomationId 为 `GlobalSettings`。它不属于输入法列表，不参与添加、删除、排序或 Ctrl+Space 轮换。
- 点击入口后，右侧标题变为“全局设置”，显示独立的全局设置面板，隐藏引擎设置和词库 TabView。左侧取消输入法的可见选中状态，但记住上一次输入法页面，方便恢复导航。
- 全局页的“常规”tab 包含“语言设置”，使用 ComboBox，AutomationId 为 `UiLanguage`。四个选项按下表顺序显示；“开发者选项”tab 提供详细日志开关和目录状态，见 [日志设计](windows-developer-logging-design.md)。
- “跟随系统语言”和字段标签随当前 Settings 窗口语言翻译。明确语言选项采用固定自称：`简体中文`、`繁體中文`、`English`，便于用户在误选语言后恢复。
- 全局页禁用删除、上移、下移；添加仍可用，添加成功保存列表后切到新输入法页。添加前同样处理当前全局草稿。
- “重置为默认值”只重置当前页面的草稿；全局页恢复 `System` 并关闭详细日志，点击“应用”后才保存。它不清空输入法列表或引擎选项。
- “应用”保存当前页面。跨页面导航沿用现有保存/放弃/取消对话框；取消或保存失败时恢复原导航选中项。关闭窗口放弃未应用草稿。
- 加载和保存时禁用相关操作，失败保留草稿，冲突提示重新加载。断线重试保留当前页面类型，不意外跳回拼音页。词库定时刷新只在输入法的词库页可见时执行。

| 选项 | 协议枚举，固定数值 | 持久化值 | 解析后的 UI 语言 | gettext locale |
| --- | --- | --- | --- | --- |
| 跟随系统语言 | `System = 0` | `system` | 运行时解析 | 运行时解析 |
| 简体中文 | `SimplifiedChinese = 1` | `zh-Hans` | `zh-Hans` | `zh_CN` |
| 繁體中文 | `TraditionalChinese = 2` | `zh-Hant` | `zh-Hant` | `zh_TW` |
| English | `English = 3` | `en` | `en` | `en_US` |

枚举、配置值与 ComboBox 索引显式映射；不保存本地化显示文本。

## 保存语言与当前语言

区分三种状态：`preferredLanguage` 是已保存的用户选择；`resolvedLanguage` 是按当前用户系统偏好解析后的目标语言；`coreActiveLanguage` 是当前 Core 启动时实际采用的语言。全局页的下拉框反映已保存选择，即使它还没有应用到 Settings。

Settings 使用 `coreActiveLanguage` 构建整个窗口，窗口生命周期内保持不变。改变下拉框不会自动保存或翻译控件。保存成功不自动重启服务；用户通过现有“重启服务”命令使 Core 和 Settings 同时重新创建。该命令目前会关闭已有 Settings，并在 Core 就绪后重新打开它。

启动 Settings 时创建禁用的加载界面，异步读取 `GetGlobalSettings`，确定窗口语言后更新固定文案并加载目录和选项；启动错误提示使用本地语言快照。已有单实例仅被激活时继续保持原语言。关闭再打开 Settings，如果 Core 未重启，仍使用旧 `coreActiveLanguage`，避免固定文案与 Core 返回的动态标签混用语言。

## 生效时机

以下为实现的行为约定；具体系统派发时机仍由 Windows 控制。

| 内容 | 应用保存后的生效时间 | 是否要求重启宿主应用 |
| --- | --- | --- |
| 已打开的 Settings | 重启服务后，新建 Settings 窗口生效 | 否 |
| Core 提供的输入法名称、引擎标签和枚举描述 | Core 重启后重新创建目录与引擎生效 | 否；TSF 重连后获取新名称 |
| 原生右键菜单 `OnClick` | TSF 后台观察到新配置后，下次打开菜单生效 | 否 |
| 系统请求的 `InitMenu` | 后台更新后，下一次 `InitMenu` 构建时生效 | 否；系统重新请求菜单的时机由 Windows 决定 |
| 正在显示的菜单 | 本次菜单保持原语言，重新打开后采用新快照 | 否 |
| 关于窗口首次打开或关闭后重开 | 后台更新后使用新快照创建 | 否 |
| 已打开的关于窗口 | 下次点击“关于”时刷新已有窗口，再激活 | 否；首版不主动刷新正在阅读的窗口 |
| TSF 自有服务错误和后备提示 | 后台更新后，下次生成文案时生效 | 否 |

`LangBarItem::OnClick` 在允许原生菜单的桌面路径每次创建并销毁 HMENU；`InitMenu` 每次向 Windows 提供菜单项。`AboutWindow::show()` 在复用已有窗口时调用 `refreshText()` 更新标题、详情和按钮，再激活窗口。设置菜单命令 ID 与派发消息保持稳定，不用翻译结果识别命令。immersive/UIElement-only 使用系统菜单；自有菜单和关于窗口遵守 UIElement 显示许可。

重启 Core 不会卸载应用进程中的 TSF DLL，也不一定销毁关于窗口。菜单和关于的语言刷新采用独立机制，不依赖 Core 重启、重新注册输入法或重新加载 DLL。以上只适用于已包含此实现的 DLL；部署新版 DLL 后，已加载旧代码的应用仍需按现有部署规则重新打开。

后台检查目标间隔为 1 秒，消息窗口现有的 100 ms timer 仅负责调度和收取完成结果。该间隔不是传播时延保证：磁盘、漫游目录和线程调度可能延迟更新。保存后立刻打开菜单可能仍显示旧语言，下一次后台刷新完成后的打开采用新语言。

## 全局配置与所有权

`%APPDATA%/Fcitx5/config/fcitx5/conf/windows-ui.conf` 是全局 UI 设置的唯一权威文件。它与 `windows.conf` 中的输入法和引擎配置分别保存，使用独立 revision，避免语言变更触发输入引擎 revision 或预编辑重置。语言、详细日志与快捷键共享此全局 revision。

```ini
[General]
Version=3
Language=system
Revision=1
DetailedLogging=false

[Shortcuts]
0=Control+space
1=Control+Alt+1
2=Control+Alt+2
3=Control+Alt+3
4=Control+Alt+4
5=Control+Alt+5
```

- Core 是唯一写入者；Settings 通过 IPC 读写。普通桌面 TSF 后台只读此固定路径，因此 Core 关闭时菜单和关于仍能使用已保存语言。Sandboxie/AppContainer 等 HostOnly 客户端通过 IPC 获取宿主解析后的语言，不读取隔离环境中的用户配置；断线时沿用已有快照。TSF 的日志开关从 Core 回复同步，不以后台文件读取开启。
- 共享路径函数使用 `SHGetKnownFolderPath(FOLDERID_RoamingAppData)`，按已检查的上游 Windows StandardPaths 目录规则追加相对路径；不根据宿主 EXE、当前目录或 PATH 查找用户配置。
- 文件采用 ASCII 字段和严格 schema。共享读写模块使用 Boost.PropertyTree 的 INI parser，校验版本、语言白名单、详细日志布尔值、快捷键及非零 `uint64_t` revision；限制大小为 4 KiB。兼容读取 v1/v2，旧版本补齐默认快捷键，v1 的日志默认关闭，下次成功保存写 v3。
- 无文件视为默认 `System`、revision 1。损坏、未知版本或权限失败应保留已有有效语言快照；初次读取失败回退系统解析结果或英文。写请求遇到损坏或不可读文件返回结构化错误，不自动覆盖用户文件。
- 保存使用现有 `atomicfile.h` 的同目录临时文件和原子替换。读者使用允许替换的共享打开方式，取得完整的旧版或新版，失败后稍后重试。
- 文件为用户级，各 session 的 Core 可能同时写入：同目录使用固定的 `windows-ui.conf.lock` 文件，通过非等待的 `LockFileEx` 获取写锁，并在锁内重新读取 revision 后校验。锁忙时返回可重试错误，Core 主事件循环不等待另一个 session。进程退出由操作系统释放锁，不删除锁文件，也不依赖 session 限定的服务互斥。
- 语言、日志和快捷键均未变化时幂等成功，不增加 revision；任一字段修改增加 revision，溢出拒绝写入。保存成功后发布新偏好并即时应用日志开关和 Core 快捷键，保持 `coreActiveLanguage` 不变，不调用引擎 `setConfig`、context reset 或 `settingsEpoch_` 增量。
- 隔离测试通过 `--windows-ui-settings-file ABSOLUTE_PATH` 注入全局配置路径，输入设置路径由 `--windows-settings-file` 独立指定。IPC 不接受客户端指定的读写路径；测试 runner 还需隔离上游用户数据和服务对象。

## IPC 扩展

当前协议为 v12，Core、TSF 和 Settings 同步更新。`GetGlobalSettings`、`SetGlobalSettings` 和 `GlobalSettingsReply` 均要求 `contextId=0`。语言请求始于 v7，v10 增加详细日志字段，v11 增加独立的只读插件查询，v12 增加全局快捷键；以下为当前相关字段。AppContainer 仅允许读取，不允许 `SetGlobalSettings`。

```cpp
enum class UiLanguage : uint8_t {
    System = 0, SimplifiedChinese = 1,
    TraditionalChinese = 2, English = 3
};

struct GlobalSettings {
    UiLanguage language = UiLanguage::System;
    uint64_t revision = 1;
    bool detailedLogging = false;
    Shortcuts shortcuts = defaultShortcuts(); // 轮换和快速切换第 1～5 项
};

struct GlobalSettingsReply {
    SettingsError error;
    GlobalSettings settings;
    UiLanguage resolvedLanguage;   // 只能为三种明确语言
    UiLanguage coreActiveLanguage; // 只能为三种明确语言
};
```

`SetGlobalSettings` 携带语言、日志开关、六项快捷键及草稿读取时的 revision。服务端拒绝非法枚举/布尔值、无效或重复快捷键、零 revision、额外/截断字段和非零 contextId，回复实际保存状态。锁忙返回 `SettingsError::Busy`；解析故障返回结构化错误，不把错误当默认配置返回成功。

保存超时不重放写请求，重连后先查询已保存选择和 revision。Settings 在更新全局页后不修改 `catalog_.selection.revision` 或 `config_.revision`。输入法列表变更只需处理当前草稿，并沿用已有输入法 revision。

## 翻译目录与接口

共享源码目录 `win32/i18n/` 负责语言枚举映射、系统语言解析、配置路径/解析、消息目录定位和显式 locale 翻译。Core、TSF 和 Settings 分别构建这些模块；共享头文件不向 IPC 暴露 Boost 类型。

```text
po/fcitx5-windows.pot
po/zh_CN.po
po/zh_TW.po

share/locale/zh_CN/LC_MESSAGES/fcitx5-windows.mo
share/locale/zh_TW/LC_MESSAGES/fcitx5-windows.mo
```

英文原文嵌入代码，不要求英文 `.mo`。上游 `fcitx5.mo`、`fcitx5-chinese-addons.mo` 与 Windows domain 同处 `share/locale`，分别维护。汉字候选、用户文本、文件名、URL、commit、配置键值和协议标识不作为 UI 词条翻译。紧凑的输入法图标标签按输入法身份处理，不由英文名称截取字节生成。

Core 将进程内的 `LANGUAGE`、`FCITX_LOCALE` 和 UTF-8 `LC_ALL` 设置为选定语言，再通过 libintl 的 `setlocale` 初始化 locale（包括 Windows CRT 没有的 `LC_MESSAGES`），避免继承 `C` / `C.UTF-8` 时 gettext 忽略 `LANGUAGE`。创建 `Instance` 和加载引擎前，用 `StandardPaths::fcitxPath("localedir")` 注册 `fcitx5-chinese-addons` domain。上游插件工厂后续的重复注册沿用该目录，避免构建时的绝对安装前缀进入运行时资源查找。部署树移动后，拼音、双拼和码表的配置描述仍从当前安装树加载。Settings 分别匹配拼音的 `CurrentCandidate` 和码表的 `DefaultCandidate`；上游繁体译文缺失的“选词末字”使用 Windows domain 补充。

`scripts/test-engine-localization.ps1 -Prefix <runtime> -TsfBuild <tsf-build>` 启动临时 Core，依次验证简体、繁体和英文的真实引擎配置描述，测试中跳过用户配置目录。与已运行的输入法并行验证时，Core 和 probes 必须都以 `FCITX5_UI_TEST` 构建，使用独立的 Pipe 和服务互斥；普通构建需要空闲服务端点。将运行树复制到与构建前缀不同的位置后执行此测试，可覆盖部署路径迁移。

接口：`Translator::text(context, english)` 返回 UTF-8 `std::string`，`Translator::wide(context, english)` 返回 UTF-16 `std::wstring`；`format(context, english, args...)` 翻译后格式化编号参数。Settings 使用同样带显式 context 的 `tr` 封装，动态表格中的原文用 `i18n::mark` 标记。首版没有数量型 UI 文案，未来新增时使用 Boost.Locale 的复数查询，并同步扩展提取关键字。

```cpp
auto caption = translator.wide("menu", "Input Method Settings");
auto heading = translator.wide("settings", "Global Settings");
```

```po
msgctxt "settings"
msgid "Global Settings"
msgstr "全局设置"
```

翻译内部使用 Boost.Locale 显式 locale/domain 查询。每次菜单构建捕获同一不可变快照，所有条目使用同一语言。语言变更时创建新 locale 和译文集合，再在 TSF 所属线程整体替换快照。后台检查中解析后的语言未变时复用原 locale，不重复加载 `.mo`。菜单回调只访问内存，按键和 edit session 不加载文件或运行翻译初始化。

不调用 `std::locale::global`、修改 TSF 宿主环境变量、线程 UI 首选语言或宿主 DLL 搜索路径。目录缺失、加载/转换异常均捕获并回退英文，不能让翻译异常穿过 COM 方法。动态参数使用可重排的编号占位符，例如 `Architecture: {0}`；翻译后再格式化，参数不拼接成待翻译词条。校验占位符并捕获格式错误，必要时使用英文模板。资源提取与错误处理需要覆盖多行字面量。

Settings 的运行时 XAML 保留结构和 `x:Name`，字符串通过加载后的属性赋值或动态控件创建时设置；包括标题、按钮、tab、tooltip、ContentDialog、状态和 AutomationProperties.Name。不将译文直接拼入 XML。全局页状态显式建模为 `PageKind::Global` / `PageKind::InputMethod`，不伪造输入法 ID，也不复用 `config_.id` 表示全局页。

## 系统语言解析与 Core 启动

`System` 使用 `GetUserPreferredUILanguages(MUI_LANGUAGE_NAME)` 获取当前用户的有序 UI 语言列表，不使用键盘布局、区域数字格式、TSF 宿主的线程语言或 `std::locale("")` 推断显示语言。依序寻找受支持语言，最终回退英文。明确选择不受系统语言变化影响。

中文先识别 script：`zh-Hans-*` 映射简体，`zh-Hant-*` 映射繁体；未显式指定 script 时，`zh-CN` / `zh-SG` 映射简体，`zh-TW` / `zh-HK` / `zh-MO` 映射繁体，单独 `zh` 默认简体。`en-*` 映射英文。BCP-47 比较不区分大小写；标签解析和规范化使用 Windows NLS 能力，最终按白名单映射，不以任意语言标签构造文件路径。

Core 先读取全局配置并接入诊断日志，在 `setupEnv()` 确立安装树资源路径后解析 `coreActiveLanguage`；这些步骤均在创建 `Instance` 和加载 addon 前完成。配置路径解析不依赖 `WindowsSettings` 或引擎。Core 自己的进程中初始化可用的非 C locale，并设置 gettext 的语言选择及 `FCITX_LOCALE` 为一致的 `zh_CN`、`zh_TW` 或 `en_US`。英文也显式设置，避免继承外部中文覆盖。

上游 `I18NString::match("system")` 在 Windows 明确读取 `FCITX_LOCALE`；引擎配置使用 `_()` 构造标签，`OptionBase` 缓存已翻译的 description。启动顺序因此是功能要求，不能只在现有 `WindowsSettings` 构造完成后改 locale。libintl 与 CRT locale 的具体组合需在当前 MSYS2 工具链中验证，不把设置环境变量成功等同于翻译加载成功。

Core 目录内的英文键盘、拼音、双拼及五笔硬编码中文名称改为翻译词条；优先复用上游已本地化的其他名称和配置描述。Settings 的路径标签、双拼选项和分组覆盖表改为 Windows domain 词条，其他引擎标签直接使用 Core 的结果。简体、繁体上游资源随安装树部署，英文回退源文。

普通桌面 TSF 在激活时启动后台全局配置读取，之后按约 1 秒间隔检查，更新逻辑位于 `pollState()` 的焦点和 Core 可用性提前返回之前；Core 关闭、没有焦点或处于重启期间仍可刷新。`System` 同时重新解析用户 UI 首选语言。HostOnly 客户端从 Core IPC 回复获取语言，不读取隔离视图的配置。后台最多一个任务，包含读取/目录加载；完成后校验原 TSF 激活代次，只在原所属线程安装快照。

后台任务独立持有 DLL 生命周期引用和任务状态，不能携带失效的 `Tsf*`、窗口裸指针；停用取消/废弃结果，不在 UI 线程等待目录 I/O。漫游目录可能慢或不可读，不能用同步文件访问堵住 `OnClick` / `InitMenu` / 按键回调。初次加载尚未完成时使用英文或已缓存快照，稍后打开采用最新配置。

## 构建与部署

- Boost.Locale 源码和纯 Boost 头文件固定为 1.92，源码归档校验 SHA256。三个工程各自编译消息、复数表达式和编码组件，显式构建消息 facet；编码使用 WinAPI，不引入 ICU、全局 backend 初始化或 Boost.Thread 二进制依赖。沿用每个工程的 CRT 设置，安装树部署 Boost 许可证。
- TSF 的 standalone LLVM / Windows SDK 环境与 Settings 的 MSVC 环境分别构建、验证所用库。Core 的 Windows 自有提示采用共享封装，使用其 MSYS2/Clang GNU 对应版本；不能复用 MSYS2 的 Boost DLL 给 TSF 或 Settings。
- 配置 INI parser 属于 Boost.PropertyTree 头文件依赖，不要求为它新增动态库。构建时显式提供匹配的头文件路径。
- `scripts/update-translations.ps1` 提取/更新 PO/POT，`-ExtractOnly` 只更新 POT；常规构建编译 `.po`，不重写翻译源文件，并执行 `msgfmt --check`。
- 翻译构建属于共享部署资源，由统一构建入口生成到 staging 的 `share/locale`，独立 TSF/Settings 构建需提供明确的资源构建/部署入口。
- TSF 从实际 DLL 路径、Settings 从自身 EXE 路径定位安装根的 `share/locale`；不使用宿主 EXE 路径。单独运行构建输出时可明确提供测试资源目录，资源不存在正常回退英文。
- `build-and-deploy.ps1`、Settings 构建和 CI 检查所需依赖；`-WhatIf` 保持无构建/部署副作用。安装包包含翻译目录和 Boost 许可证，打包过程不执行注册或用户服务控制，实际安装行为见 [安装器设计](windows-installer-design.md)。
- 语言偏好变更是运行配置变更，不要求重新注册。注册描述等系统缓存元数据的国际化不属于这一批动态 UI 文案。

## 验证

| 验证范围 | 必须覆盖的场景 |
| --- | --- |
| 语言映射 | 中文 script/地区、英文地区、未知语言、有序偏好列表、API 失败 |
| 翻译资源 | 上下文、缺词回退、UTF-8/UTF-16、可重排占位符、格式错误与资源缺失 |
| 配置 | 首次默认、原子替换、非法/超大/损坏文件、只读失败、并发 session revision 冲突、隔离路径 |
| IPC | v10 语言/日志字段往返、非法枚举/布尔值/长度/contextId、锁忙/保存失败、超时后只查询不重放 |
| WinUI 自动化 | 入口位置、选中状态、四个选项、跨页草稿、添加导航、当前页重置、最小尺寸与长译文 |
| 服务集成 | 应用语言后 Settings 保持旧语言，重启后新语言，单实例激活不重建，Core 标签一致 |
| TSF 回归 | HMENU 与 `InitMenu` 同一快照、后台刷新与停用竞态、Core 停止后菜单/关于可用、窗口复用刷新 |
| 输入回归 | 仅保存语言不 reset/提交预编辑、不改变列表/引擎配置、不改变输入设置运行时代次 |

优先运行受影响的 `test_protocol`、`test_langbar`、`test_about` 和 `test_i18n`，再运行 `ctest --test-dir <隔离的 TSF build> --output-on-failure`。`test_i18n` 覆盖真实目录加载、后台快照刷新、配置校验、写锁和保存失败。

`build-settings.ps1 -UiTest -BuildRoot <隔离目录>` 生成编译时隔离的测试版 Settings，配合 `scripts/test-ui-localization.ps1 -SettingsBuild <目录> -TsfBuild <目录>` 和 `ui_fixture.exe` 验证真实 WinUI 控件。测试版使用不同的 Pipe、服务互斥和激活对象；模拟 Core 只读写测试目录，不修改已运行的安装版服务或用户语言配置。该测试模拟 Core 再启动，不代表真实服务控制链验证。常规构建不定义 `FCITX5_UI_TEST`。

实际 Windows 菜单重新请求时机、多个真实宿主中的刷新、系统首选语言变化、DPI/文本缩放和漫游目录行为需要真实系统验证。免注册测试可以证明函数和窗口逻辑，但不能代替这些验证。构建与自动化不执行注册、安装或停止用户服务。

## 参考

- [Boost.Locale 消息翻译与显式 locale](https://www.boost.org/doc/libs/latest/libs/locale/doc/html/messages_formatting.html)
- [Boost.Locale 构建与 Windows backend](https://www.boost.org/doc/libs/latest/libs/locale/doc/html/building_boost_locale.html)
- [GNU gettext 手册](https://www.gnu.org/software/gettext/manual/gettext.html)
- [GetUserPreferredUILanguages](https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getuserpreferreduilanguages)
- 本仓库当前调用链：`SettingsWindow.cpp`、`servicecontrol.cpp`、`langbaritem.cpp`、`aboutwindow.cpp`、`src/main.cpp`、`src/windowsinputmethods.cpp`、上游 `i18nstring.cpp` 与 `option.cpp`。
