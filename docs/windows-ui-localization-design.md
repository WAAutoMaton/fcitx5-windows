# Windows UI 国际化与全局设置设计

状态：已接入。Core、TSF 和 Settings 分别构建，翻译/配置/协议、免注册菜单/关于窗口回归及隔离 WinUI 自动化均有覆盖；真实系统菜单派发、安装版服务重启与跨宿主刷新仍需系统验证。

## 目标与选型

Windows 自有 UI 统一采用 gettext 消息目录与 Boost.Locale 1.92，覆盖 Settings、语言栏右键菜单、关于窗口以及错误/提示/无障碍文案。支持跟随系统、简体中文、繁体中文、英文、日语和韩语。

翻译源文件为 UTF-8 `.po`，运行时加载外置 `.mo`；代码中保留带上下文的英文原文，缺失译文自动回退英文。Core 的上游引擎沿用 libintl 与上游翻译目录。`win32/i18n/` 源码在 Core、TSF、Settings 三个工程中分别编译，不共享运行库二进制、C++ 对象或 locale 对象。

## 全局设置入口

左侧布局自上而下为输入法列表、添加/删除/上移/下移按钮行、全局设置入口（位于底部**重置为默认值**按钮上方）。列表占据剩余高度，其余元素使用自动高度。

```text
左侧                           右侧
输入法                         全局设置
[有序输入法列表]                [常规] [开发者选项] [插件] [快捷键]
                               语言设置    [跟随系统语言 v]
[+] [删除] [上移] [下移]        候选框布局  [竖排 v]
[设置图标  全局设置]

重置为默认值                             关闭  应用
```

- 入口为独立导航项（AutomationId `GlobalSettings`），不参与输入法列表编辑或 `Ctrl+Space` 轮换。选中后右侧切换为全局设置面板，隐藏引擎设置与词库页，同时记住上一次选中的输入法页以便返回。
- 全局页包含四个分页：
  - **常规**：UI 语言下拉框（AutomationId `UiLanguage`，选项见下表）与候选框布局下拉框（AutomationId `CandidateLayout`，选项为**竖排** `Vertical`、**横排** `Horizontal`）。
  - **开发者选项**：详细日志开关与目录统计，见 [日志设计](windows-developer-logging-design.md)。
  - **插件**：只读 addon 状态与个人插件目录入口，见 [插件指南](windows-plugins.md)。
  - **快捷键**：轮换与直切第 1～5 项快捷键，见 [设置设计](windows-settings-design.md#快捷键录制)。
- “跟随系统语言”与字段标签随当前窗口语言翻译；明确语言选项采用固定自称（`简体中文`、`繁體中文`、`English`、`日本語`、`한국어`），方便误选后恢复。
- 全局页禁用删除/上移/下移；仍可点击添加，保存新输入法后跳转到新页面（添加前同样处理未保存全局草稿）。
- **重置为默认值**仅重置当前页草稿：全局页恢复 `System` 语言、`Vertical` 候选框布局、关闭详细日志与默认快捷键，点击**应用**后保存。
- 跨页导航沿用保存/放弃/取消提示；关闭窗口放弃未应用草稿；加载/保存期间禁用操作，失败保留草稿，冲突提示重新加载。

| 选项 | 协议枚举值 | 持久化值 | 解析后的 UI 语言 | gettext locale |
| --- | --- | --- | --- | --- |
| 跟随系统语言 | `System = 0` | `system` | 运行时解析 | 运行时解析 |
| 简体中文 | `SimplifiedChinese = 1` | `zh-Hans` | `zh-Hans` | `zh_CN` |
| 繁體中文 | `TraditionalChinese = 2` | `zh-Hant` | `zh-Hant` | `zh_TW` |
| English | `English = 3` | `en` | `en` | `en_US` |
| 日本語 | `Japanese = 4` | `ja` | `ja` | `ja_JP` |
| 한국어 | `Korean = 5` | `ko` | `ko` | `ko_KR` |

枚举、配置值与下拉框索引显式映射，不保存本地化显示文本。

## 保存语言与当前语言

系统区分三种语言状态：`preferredLanguage`（用户保存的选择）、`resolvedLanguage`（结合系统偏好解析后的目标语言）、`coreActiveLanguage`（当前 Core 启动时实际采用的语言）。

Settings 使用 `coreActiveLanguage` 构建整个窗口并在本次生命周期内保持一致，避免固定文案与 Core 动态标签混用语言。启动时先异步读取 `GetGlobalSettings`，再按返回的 `coreActiveLanguage` 更新固定文案并加载目录。保存语言后不自动重启服务，用户执行**重启服务**时 Core 与已打开的 Settings 会一起重建并切换到新语言。

## 生效时机

| 内容 | 应用保存后的生效时机 | 需重启宿主应用 |
| --- | --- | --- |
| 已打开的 Settings | 重启服务后新建窗口生效 | 否 |
| Core 输入法名称、引擎标签与枚举描述 | 重启 Core 后生效，TSF 重连获取新名称 | 否 |
| 候选框布局 | Core 在下一次 `KeyReply` 下发，TSF 立即生效 | 否 |
| 原生右键菜单 `OnClick` | TSF 后台刷新配置后，下次打开菜单生效 | 否 |
| 系统托管菜单 `InitMenu` | 后台刷新后，下一次 `InitMenu` 构建时生效 | 否 |
| 正在显示的菜单 | 本次保持原语言，重新打开后更新 | 否 |
| 关于窗口 | 首次打开使用新快照；已打开窗口在再次点击**关于**时刷新文本并激活 | 否 |
| TSF 自有服务错误与后备提示 | 后台刷新后，下次生成文案时生效 | 否 |

普通桌面 TSF 的菜单与关于窗口通过独立后台轮询刷新（目标间隔约 1 秒，由现有 100 ms timer 调度，受磁盘/漫游目录/线程调度影响可能略有延迟），无需重启 Core 或重新注册 DLL。

## 全局配置与所有权

`%APPDATA%/Fcitx5/config/fcitx5/conf/windows-ui.conf` 是全局设置的唯一权威文件，与输入法配置 `windows.conf` 分离存储，避免 UI 偏好修改触发输入引擎 revision 递增或预编辑重置。语言、详细日志、快捷键和候选框布局共享同一个全局 revision。

```ini
[General]
Version=4
Language=system
Revision=1
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

- **写入与读取**：Core 是唯一写入者，Settings 通过 IPC 读写。普通桌面 TSF 后台只读此固定路径（基于 `SHGetKnownFolderPath(FOLDERID_RoamingAppData)`），因此 Core 关闭时菜单和关于窗口仍可使用已保存语言。Sandboxie/AppContainer 等 HostOnly 客户端仅通过 IPC 获取宿主解析后的语言；TSF 详细日志开关始终仅从 Core 回复同步。
- **格式与迁移**：使用 Boost.PropertyTree INI parser 严格校验版本、语言白名单、详细日志布尔值、快捷键、候选框布局（`vertical`/`horizontal`）和非零 `uint64_t` revision，文件上限 4 KiB。兼容读取旧版本：v1 默认关闭日志，v1/v2 补齐默认快捷键，v1–v3 默认竖排，下次成功保存写入 v4。无文件视为默认配置（`System`、`Vertical`、revision 1）；损坏或不可读时保留已有快照并拒绝静默覆盖。
- **原子性与跨 session 并发**：通过 `atomicfile.h` 原子替换写入，并配合同目录 `windows-ui.conf.lock` 非阻塞 `LockFileEx` 写锁防止多 session Core 并发覆盖（锁忙返回 `SettingsError::Busy`）。字段未变时幂等成功；保存后即时应用日志开关、Core 快捷键并在后续 `KeyReply` 下发候选框布局。
- **隔离测试**：Core 支持 `--windows-ui-settings-file ABSOLUTE_PATH` 单独重定向全局配置文件。

## IPC 扩展

当前协议为 **v17**，`GetGlobalSettings`、`SetGlobalSettings` 和 `GlobalSettingsReply` 均要求 `contextId = 0`。AppContainer 仅允许只读 `GetGlobalSettings`。

```cpp
enum class UiLanguage : uint8_t {
    System = 0, SimplifiedChinese = 1,
    TraditionalChinese = 2, English = 3,
    Japanese = 4, Korean = 5
};

enum class CandidateLayout : uint8_t { Vertical = 0, Horizontal = 1 };

struct GlobalSettings {
    UiLanguage language = UiLanguage::System;
    uint64_t revision = 1;
    bool detailedLogging = false;
    Shortcuts shortcuts = defaultShortcuts(); // 轮换和直切第 1～5 项
    CandidateLayout candidateLayout = CandidateLayout::Vertical;
};

struct GlobalSettingsReply {
    SettingsError error;
    GlobalSettings settings;
    UiLanguage resolvedLanguage;   // 五种明确语言之一
    UiLanguage coreActiveLanguage; // 五种明确语言之一
};
```

`SetGlobalSettings` 携带语言、详细日志开关、六项快捷键、候选框布局及草稿 revision。服务端校验所有枚举、布尔值、快捷键有效性与唯一性及非零 revision，写入超时不重放，重连后先查询状态。

## 翻译目录与接口

`win32/i18n/` 提供语言映射、系统语言解析、配置读写、消息目录定位和显式 locale 翻译，不向 IPC 暴露 Boost 类型。

```text
po/fcitx5-windows.pot
po/zh_CN.po
po/zh_TW.po
po/ja.po
po/ko.po

share/locale/zh_CN/LC_MESSAGES/fcitx5-windows.mo
share/locale/zh_TW/LC_MESSAGES/fcitx5-windows.mo
share/locale/ja/LC_MESSAGES/fcitx5-windows.mo
share/locale/ko/LC_MESSAGES/fcitx5-windows.mo
```

- 上游 `fcitx5.mo`、`fcitx5-chinese-addons.mo` 与 Windows domain 共存于 `share/locale`；英文回退源码原文。
- Core 启动时设置进程内 `LANGUAGE`、`FCITX_LOCALE` 与 UTF-8 `LC_ALL`，通过 libintl `setlocale` 初始化（含 `LC_MESSAGES`），并在创建 `Instance` 前以 `StandardPaths::fcitxPath("localedir")` 注册 `fcitx5-chinese-addons` domain，保证部署树移动后引擎描述仍可正确加载。上游繁体缺失的“选词末字”由 Windows domain 补充。
- `Translator::text(context, english)` / `wide(context, english)` / `format(context, english, args...)` 基于 Boost.Locale 显式 locale/domain 查询；Settings 使用带上下文的 `tr` 和 `i18n::mark`。
- 翻译不调用 `std::locale::global`，不修改 TSF 宿主环境变量或线程首选语言；异常均在边界捕获并回退英文。动态文案采用可重排编号占位符（如 `Architecture: {0}`），先翻译再格式化。
- Settings 运行时 XAML 保留结构与 `x:Name`，所有文本与无障碍名称通过代码赋值而非拼接 XML；全局页使用独立的 `PageKind::Global` 状态建模。

## 系统语言解析与后台刷新

`System` 模式调用 `GetUserPreferredUILanguages(MUI_LANGUAGE_NAME)` 按用户首选列表匹配受支持语言，最终回退英文：

- 优先识别 script：`zh-Hans-*` → 简体，`zh-Hant-*` → 繁体。
- 未指定 script 时：`zh-CN`、`zh-SG`、单独 `zh` → 简体；`zh-TW`、`zh-HK`、`zh-MO` → 繁体；`ja` / `ja-*` → 日语；`ko` / `ko-*` → 韩语；`en-*` → 英文（大小写不敏感，白名单映射）。

普通桌面 TSF 激活时启动后台配置读取，后续每约 1 秒检查一次（位于 `pollState()` 的焦点和 Core 可用性早退之前，Core 停止时仍可刷新）。后台任务独立持有 DLL 引用，完成后回到 TSF 所属线程校验激活代次再替换不可变快照；解析语言未变时复用现有 locale。

## 构建与部署

- Boost.Locale 1.92 源码归档校验 SHA256，配套 Boost 1.92 便携头文件。三个工程各自构建消息、复数与 WinAPI 编码组件（无需 ICU 或 Boost.Thread），安装树包含 `licenses/Boost-1.92-LICENSE.txt`。
- `scripts/update-translations.ps1` 更新 PO/POT（`-ExtractOnly` 仅更新 POT）；常规构建运行 `msgfmt --check` 编译 `.mo`，不修改源文件。
- TSF 从 DLL 路径、Settings 从 EXE 路径定位安装树 `share/locale`，不依赖宿主 EXE 路径。语言切换属于运行配置变更，无需重新注册 DLL。

## 验证

| 验证范围 | 覆盖内容 |
| --- | --- |
| 单元与配置 | `test_i18n`：语言映射、翻译/占位符/回退、v1–v4 迁移、原子替换、写锁、非法/损坏文件与后台快照刷新 |
| IPC 协议 | `test_protocol`：v17 语言/日志/快捷键/候选框布局往返、非法值与错误编码 |
| TSF 菜单与关于 | `test_langbar`、`test_about`：HMENU/`InitMenu` 统一快照、停用竞态、Core 停止可用性与关于窗口复用刷新 |
| 引擎翻译迁移 | `scripts/test-engine-localization.ps1`：简体/繁体/英文/日语/韩语真实引擎描述与目录迁移验证 |
| WinUI 自动化 | `scripts/test-ui-localization.ps1`（配合 `-UiTest` Settings 与 `ui_fixture`）：入口、语言切换、跨页草稿、重置与日志页；`scripts/test-settings-shortcuts.ps1`：快捷键、候选框布局切换与 v2→v4 迁移 |

真实系统菜单重新请求时机、多宿主刷新、安装版服务重启、系统首选语言变更、DPI/文本缩放及漫游目录行为仍需实机验证。

## 参考

- [Boost.Locale 消息翻译与显式 locale](https://www.boost.org/doc/libs/latest/libs/locale/doc/html/messages_formatting.html)
- [Boost.Locale 构建与 Windows backend](https://www.boost.org/doc/libs/latest/libs/locale/doc/html/building_boost_locale.html)
- [GNU gettext 手册](https://www.gnu.org/software/gettext/manual/gettext.html)
- [GetUserPreferredUILanguages](https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getuserpreferreduilanguages)
