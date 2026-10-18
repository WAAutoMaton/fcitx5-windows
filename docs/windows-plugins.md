# Windows 输入法插件

## 结论与现状

采用上游 Fcitx5 的 `SharedLibrary` addon ABI，输入法以独立 DLL、addon 描述、输入法描述及数据文件发布。Windows 侧补充目录发现、DLL 依赖搜索和独立 SDK 构建。TSF 和 Settings 继续通过 Core 的现有 IPC 工作，插件只在 Core 进程中执行。

当前 chinese-addons 已经是 DLL：`build-pinyin.ps1` 分别构建 Core、libime、chinese-addons，最后统一部署；Core 注册了 `SharedLibraryLoader`。本次保留这条构建及加载链路。新增输入法无需成为 Core 子模块，也无需加入 Core 的 `add_subdirectory` 或静态注册表。

原先的阻碍是 Windows `StandardPaths` 不读取 `FCITX_ADDON_DIRS`、`FCITX_DATA_DIRS`、`XDG_DATA_DIRS`，以及没有为独立前缀的私有 DLL 配置搜索目录。修复保存在 `patches/fcitx5-windows-paths.patch` 中，由现有补丁脚本重现。

## 参考实现

| 来源 | 采用的设计 |
| --- | --- |
| [Fcitx5 5.1.22 addonloader.cpp](https://github.com/fcitx/fcitx5/blob/5.1.22/src/lib/fcitx/addonloader.cpp) 和 [addonloader_p.h](https://github.com/fcitx/fcitx5/blob/5.1.22/src/lib/fcitx/addonloader_p.h) | 根据 addon 描述查找共享库，解析工厂入口，按依赖加载并捕获初始化异常。这里与仓库固定版本比较，不混入 master 的后续重构。 |
| [fcitx5-macos/src/fcitx.cpp](https://github.com/fcitx/fcitx5-macos/blob/master/src/fcitx.cpp) | 应用与用户插件分别使用前缀；启动前配置 addon、数据和语言模型路径，提前绑定插件翻译。 |
| [fcitx5-plugins](https://github.com/fcitx-contrib/fcitx5-plugins) | 插件可在独立仓库中构建发布，平台适配与引擎源码分离。其现成 Windows 包没有在本项目验证过 ABI，不能直接视为兼容。 |

Windows 使用 PE DLL、导入库与 Windows GNU C++ 运行库；不能套用 macOS 的动态链接参数，或将 TSF 使用的 MSVC ABI 用于 Core 插件。

## 目录与发现

启动时依次加入这些前缀：

1. Core 安装目录，保持现有 `bin`、`lib/fcitx5`、`share` 和 `lib/libime`。
2. `<Core 安装目录>/plugins`，用于便携部署。
3. `%LOCALAPPDATA%/Fcitx5/plugins`，用于不需管理员权限、与 Core 安装目录分离的插件安装。

每个插件安装到上述插件前缀的共同目录树，**不是** `plugins/<插件名>`。通用的运行时文件布局为：

```text
plugins/
  bin/<私有依赖>.dll
  lib/fcitx5/<引擎库>.dll
  lib/libime/<语言模型>.lm
  lib/libime/<语言模型>.lm.predict
  share/fcitx5/addon/<addon名>.conf
  share/fcitx5/inputmethod/<输入法ID>.conf
  share/<插件资源目录>/...
  share/locale/<语言>/LC_MESSAGES/<翻译域>.mo
```

`FCITX_ADDON_DIRS`、`FCITX_DATA_DIRS`、`XDG_DATA_DIRS` 和 `LIBIME_MODEL_DIRS` 使用分号分隔的绝对路径，由 Core 根据安装位置重新生成。Windows 路径层仍保留原有用户配置、用户词库和内置路径规则；这些变量用于内置搜索前缀，不改变用户数据写入位置。既有用户数据路径在系统资源路径之前，新增前缀之间按上面的顺序查找。相同 addon 名称及数据相对路径应视为冲突，插件不应覆盖内置文件。

Core 使用 `AddDllDirectory` 加入存在的插件 `bin` 目录，并启用相应的 DLL 搜索模式，供 addon 的私有依赖使用。主程序及已经加载的 Core 运行库继续共享；不同插件的私有 DLL 应使用不同文件名。Windows 不保证多个 `AddDllDirectory` 目录之间的搜索顺序，不能靠目录顺序解决 DLL 同名冲突。

安装、升级或移除插件后重启 Core，再在 Settings 的输入法列表启用条目。当前不实现 DLL 热卸载。已有 Settings 会枚举 addon 的输入法、可用状态及配置描述；符合上游接口的输入法可使用通用配置页和文字标签，不需要增加专用 IPC 或 TSF profile。

## ABI 与发布边界

输入法工厂入口是 C 符号，返回值及后续接口是 C++ 对象，因此这不是跨编译器的稳定 C ABI。插件必须匹配 Core 的架构、Fcitx5/libime 版本及补丁、Clang Windows GNU 工具链、libc++、CRT 和影响 ABI 的编译选项。当前统一 Core 构建脚本使用 AMD64 Release。

SDK 是同一次 Core/libime 构建的安装前缀，包含头文件、导入库、CMake package、模块公共头文件及生成数据的工具。现有部署脚本的 staging prefix 可以用于开发；SDK 中部分导出路径是绝对路径，开发时应直接引用原安装前缀，不能仅移动 SDK 目录后继续使用。

插件引用 SDK 中的共享库，不再静态链接一份 Core，也不分发 `libFcitx5*.dll`、`libIMECore.dll`、`libc++.dll` 等公共运行库。引擎使用其他 addon 时，应在 addon 描述中声明依赖，并确保宿主已安装相应模块。

上游 addon 描述中的版本依赖会检查 Core 版本，但不能校验完整 Windows C++ ABI。目前是针对匹配 SDK 的本地插件能力，尚无自动 ABI 校验或在线插件管理器。面向公开分发时，建议增加宿主 SDK 标识以及包清单，记录架构、依赖、哈希和拥有的文件；安装器先验证兼容性和文件冲突，再合并目录，卸载时只移除该包拥有的文件。清单是部署层协议，不必改变上游 addon ABI。每个 Core ABI 更新应同时提供对应 SDK，并重建插件。

## 插件 SDK

各插件在自身仓库维护构建脚本、平台适配和打包说明。主仓库提供通用的 `cmake/windows-addon-toolchain.cmake`，其 `FCITX5_WINDOWS_SDK` 参数指向匹配的 Core/libime SDK 安装前缀，例如统一构建生成的 `build/all/prefix`。SDK 需要包含头文件、导入库、模块公共接口、CMake package 和所需数据生成工具；仅有运行时文件的部署目录不能代替 SDK。

插件应将私有共享库安装到 `bin`，将 addon DLL 安装到 `lib/fcitx5`，并通过运行时搜索路径读取资源。数据文件使用插件专用名称，避免与其他插件冲突。将包中的 `bin`、`lib` 和 `share` 合并到选定插件前缀后重启 Core。分发运行时包时可省略头文件、导入库、CMake 文件和数据转换工具，并附上引擎及数据许可。

## 隔离验证

`tools/addon-probe` 是按需构建的独立工程，通过 SDK 链接 Core，使用与主程序相同的插件环境设置，适用于提供配置和候选列表的输入法。以下插件包路径、输入法 ID 和 ASCII 按键序列应替换为待验证插件的实际值。将工具安装到隔离 Core 树的 `bin` 后运行：

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
$plugin = (Resolve-Path 'D:/path/to/plugin/package').Path.Replace('\', '/')
$method = '<输入法ID>'
$keys = '<可产生候选的ASCII按键序列>'
& "$sdk/bin/addon_probe.exe" "$sdk" "$plugin" "$method" "$keys"
```

probe 禁用用户配置与词库读写，不创建 Pipe 服务或注册 TSF。最后一个参数可以是 `--absent` 或 `--unavailable`，分别验证没有安装描述文件、以及描述存在但缺 DLL/依赖/数据的情况。第二个参数使用 `--default` 可以检查 Core 的默认插件前缀。

通过 probe 代表资源发现、DLL 加载、配置读取和引擎候选提交通过，不能替代真实 Windows 应用中的 TSF 输入验收。安装器对用户插件的保留策略、完整插件包管理、ARM64 和跨版本二进制兼容性需要后续实现或验证。
