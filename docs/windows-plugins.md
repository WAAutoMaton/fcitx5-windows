# Windows 输入法插件

## 概述

Windows 侧沿用上游 Fcitx5 的 `SharedLibrary` addon ABI，输入法以独立 DLL（或纯资源包）、addon 描述、输入法描述及数据文件形式部署。插件仅在 Core 进程中运行，TSF 与 Settings 继续通过现有 IPC 交互；插件提供的方案、模式和选项菜单通过通用状态菜单桥接展示，详见 [Windows 状态菜单设计](windows-status-menu-design.md)。

- `fcitx5-chinese-addons` 随 `build-pinyin.ps1` 作为 `base` 版一部分构建部署；Core 已注册 `SharedLibraryLoader`。
- 新增输入法无需加入 Core 子模块或静态注册表。Windows `StandardPaths` 通过 `patches/fcitx5-windows-paths.patch` 读取 Core 生成的 `FCITX_ADDON_DIRS`、`FCITX_DATA_DIRS`、`XDG_DATA_DIRS`，并配合 `AddDllDirectory` 搜索各插件包的私有 DLL。

## 参考实现

| 来源 | 借鉴点 |
| --- | --- |
| [Fcitx5 5.1.22 addonloader.cpp](https://github.com/fcitx/fcitx5/blob/5.1.22/src/lib/fcitx/addonloader.cpp) / [addonloader_p.h](https://github.com/fcitx/fcitx5/blob/5.1.22/src/lib/fcitx/addonloader_p.h) | 根据 addon 描述查找共享库、解析工厂入口、按依赖加载并捕获初始化异常 |
| [fcitx5-macos/src/fcitx.cpp](https://github.com/fcitx/fcitx5-macos/blob/master/src/fcitx.cpp) | 应用与用户插件分离前缀；启动前配置 addon、数据与语言模型路径并绑定翻译 |
| [fcitx5-plugins](https://github.com/fcitx-contrib/fcitx5-plugins) | 插件可在独立仓库构建发布，平台适配与引擎源码分离（需使用本项目匹配的 SDK 构建） |

Windows 使用 PE DLL、导入库和 Clang Windows GNU 运行库，与 macOS 动态链接方式及 TSF 使用的 MSVC ABI 相互独立。

## 目录与发现

每个插件包使用独立目录并保留标准 `bin`/`lib`/`share`/`licenses` 结构（一个包可包含多个 addon）。Core 启动时按以下顺序加入搜索前缀：

1. Core 安装目录（`bin`、`lib/fcitx5`、`share`、`lib/libime`）。
2. `<Core 安装目录>/plugins/<插件包名>`（便携部署）。
3. `%LOCALAPPDATA%/Fcitx5/plugins/<插件包名>`（当前用户安装，无需管理员权限）。

运行时布局示例：

```text
plugins/
  <插件包名>/
    bin/<私有依赖>.dll
    lib/fcitx5/<引擎库>.dll
    lib/libime/<语言模型>.lm
    lib/libime/<语言模型>.lm.predict
    share/fcitx5/addon/<addon名>.conf
    share/fcitx5/inputmethod/<输入法ID>.conf
    share/<插件资源目录>/...
    share/locale/<语言>/LC_MESSAGES/<翻译域>.mo
    licenses/...
```

发现与搜索规则：

- 仅扫描两个 `plugins` 目录的直接子目录，不递归嵌套。子目录包含 `bin`、`lib` 或 `share` 之一即作为有效插件前缀（支持纯资源包；缺 DLL 的插件可被发现并标为不可用）。`bin`/`lib`/`share` 为保留名（不区分大小写），不作为插件包目录；同一位置按目录名字典序排序。
- 兼容旧的 `plugins/bin`、`plugins/lib`、`plugins/share` 共用布局，完整搜索顺序为：Core → 安装目录旧布局 → 安装目录独立插件 → 用户目录旧布局 → 用户目录独立插件。新包应使用独立目录。
- Core 根据安装位置生成分号分隔的 `FCITX_ADDON_DIRS`、`FCITX_DATA_DIRS`、`XDG_DATA_DIRS` 和 `LIBIME_MODEL_DIRS`（仅用于系统内置搜索前缀，用户配置与词库写入路径保持不变，且用户数据优先于系统资源）。
- Core 通过 `AddDllDirectory` 注册存在的插件 `bin` 目录以解析私有依赖。独立目录便于文件管理，但不提供进程/符号命名空间隔离：addon ID、资源相对路径、翻译域及不同插件的私有 DLL 文件名均应避免冲突（Windows 不保证多 `AddDllDirectory` 目录间的查找顺序）。
- 安装、升级或移除插件前先关闭 Core 服务，完成后重启（当前不支持 DLL 热卸载）。独立包可直接替换或删除目录，用户数据保留在 `%APPDATA%/Fcitx5`。

## 设置中的插件状态

**全局设置 → 插件**通过只读 IPC 展示 Core 发现的全部 addon（内置与外部）名称、标识、版本、库、说明与状态（已加载、未加载、已禁用），可见时定期刷新，并提供打开 `%LOCALAPPDATA%/Fcitx5/plugins` 的按钮（目录不存在时自动创建）。查询不触发按需加载；插件增删需重启 Core。

## 仓库插件与 full 包

`build-pinyin.ps1` 构建 `base` 中的 `fcitx5-chinese-addons`；`build-plugins.ps1` 自动发现 `plugins/` 下其余直接子目录，执行 `git lfs pull`，使用同一次构建的 SDK 编译并安装到 `Prefix/plugins/<name>`。当前 `full` 包包含：

- **`fcitx5-chinese-addons`**（`base` 已含）：拼音、双拼及八张基础码表。
- **`fcitx5-table-extra`**：纯资源包，复用 `base` 的 `table` 引擎。使用 SDK 的 `libime_tabledict.exe` 生成 25 张码表，CI 验证五笔 98、仓颉五代、粤拼码表提交及 `full`/`base` 清单差异。
- **`fcitx5-rime`**：补丁见 `patches/fcitx5-rime-windows.patch`。依赖匹配的 `clang64` `librime` 与 `librime-data`，构建时复制共享方案与 OpenCC 数据到 `plugins/fcitx5-rime/share/rime-data`。状态菜单与繁简切换验证见 `scripts/test-status-menu.ps1 -Rime`（[build.md](build.md#status-menus)）。
- **`fcitx5-wenzhounese`**：温州话引擎，CI addon probe 验证 `nyi` 候选提交。
- **`fcitx5-zhuyin`**：依赖 `clang64` GLib（`glib2`）与 Berkeley DB（`db`）。`cmake/zhuyin-deps` 从 SHA256 固定的 libpinyin 2.10.3 源码构建 libzhuyin 及三个原生数据工具（`patches/libpinyin-windows.patch`），编译固定模型至 `share/fcitx5/zhuyin`；引擎补丁位于 `patches/fcitx5-zhuyin-windows.patch`。CI 验证标准注音 `su3` 候选与 Enter 提交、模型连续保存/重载及 `full`/`base` 清单差异。

## ABI 与插件 SDK

输入法工厂返回 C++ 对象，插件必须匹配 Core 的架构（当前为 AMD64 Release）、Fcitx5/libime 版本与补丁、Clang Windows GNU 工具链、libc++、CRT 及 ABI 相关编译选项。

- **SDK**：即同一次 Core/libime 构建的 staging 前缀（如 `build/all/prefix`），含头文件、导入库、CMake package、模块公共头和数据生成工具（导出路径含绝对路径，开发时应直接使用原 staging 路径，不能用仅含运行文件的部署树替代）。
- **工具链**：主仓库提供 `cmake/windows-addon-toolchain.cmake`（传入 `-DFCITX5_WINDOWS_SDK=<SDK 前缀>`）。插件应链接 SDK 共享库，不静态链接 Core，也不随包分发 `libFcitx5*.dll`、`libIMECore.dll`、`libc++.dll` 等宿主公共运行库。
- **打包**：将私有共享库装入 `bin`、addon DLL 装入 `lib/fcitx5`、资源装入 `lib`/`share` 并附带许可证，分发运行时可省略头文件、导入库、CMake 文件和构建工具。
- **未来扩展**：上游版本依赖仅核对 Core 版本，公开分发场景后续可引入包含 SDK 标识、架构、哈希和文件所有权的包清单与管理器。

## 隔离验证

`tools/addon-probe` 通过 SDK 链接 Core 并复用主程序插件发现逻辑（禁用用户配置/词库读写，不启动 Pipe 或注册 TSF）：

```powershell
$sdk = (Resolve-Path ./build/all/prefix).Path.Replace('\', '/')
$env:MSYS2_ROOT = 'C:/msys64'
cmake -S tools/addon-probe -B build/addon-probe -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/windows-addon-toolchain.cmake" `
  -DFCITX5_WINDOWS_SDK="$sdk" -DCMAKE_INSTALL_PREFIX="$sdk" `
  -DARCH=AMD64 -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER=C:/msys64/clang64/bin/clang++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/msys64/clang64/bin/ninja.exe
cmake --build build/addon-probe
cmake --install build/addon-probe

# 显式指定单个插件前缀（含 bin/lib/share）：
& "$sdk/bin/addon_probe.exe" "$sdk" "D:/path/to/plugin/package" "<输入法ID>" "<ASCII按键序列>"
# 验证 plugins/<包名> 自动发现：
& "$sdk/bin/addon_probe.exe" "$sdk" --default "<输入法ID>" "<ASCII按键序列>"
# 先选预编辑再 Enter 提交的引擎（如注音）：
& "$sdk/bin/addon_probe.exe" "$sdk" --default zhuyin su3 --preedit
```

最后一个参数支持 `--absent`（验证未安装）或 `--unavailable`（验证描述存在但缺 DLL/依赖/数据）。配套目录发现回归可通过 `ctest --test-dir build/addon-probe --output-on-failure` 运行。

其他回归：`scripts/test-build-plugins.ps1`（fixture 自动发现、目录布局与依赖复制）；`scripts/test-settings-plugins.ps1 -SettingsExe <UiTest Settings> -FixtureExe <ui_fixture.exe>`（插件状态页 UI 与翻译）。

剩余待验收：真实 Windows 应用输入、安装器升级/卸载对用户插件的保留、ARM64 支持，以及完整的包管理与跨版本 ABI 校验。
