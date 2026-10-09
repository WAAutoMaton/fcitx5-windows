# Windows Pinyin Integration

## Versions and Architecture

The pinned combination is Fcitx5 5.1.22, chinese-addons 5.1.15 and libime
1.1.17, including libime's pinned KenLM submodule. Although chinese-addons
declares libime 1.1.14 as its minimum, its selected revision uses newer
`HistoryBigram::WordWithCode` and Pinyin context APIs. The tested combination
is recorded in `cmake/pinyin-lock.json`.

Core remains a separate MSYS2 clang64 process. The TSF DLL is compiled with
LLVM's Windows SDK/ATL toolchain. No C++ object, allocator or engine ABI crosses
the pipe. All engine operations run on Core's event loop through EventDispatcher.

`windowskeyboard` is a static direct-input engine with no XKB dependency. It
provides the first group entry required by Core; it never commits ASCII itself.
The Windows group uses `keyboard-us` and `pinyin`. Ctrl+Space switches modes.
TSF subscribes to the thread manager's `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE`:
system keyboard open/close changes select Pinyin/direct input in Core. Focus
changes and reconnections preserve that system mode. An exact Ctrl+Space
preserved key and the ordinary key callback use the same mode synchronization;
ordinary Space is never registered as a preserved key. Existing user groups are
not saved or overwritten. ASCII fallback is disabled by default and by the Pinyin build script.
Core prewarms Pinyin before accepting pipe clients so initial model paging does
not consume the first key's IPC timeout. Runtime DLL search includes its own bin.

## Compatibility Patches

Initialize the pinned submodules, then apply the patches kept in the main repo:

```powershell
git submodule update --init --recursive
./scripts/apply-pinyin-patches.ps1
```

The script verifies HEAD revisions, skips already-applied patches and refuses
conflicting changes. It does not reset submodules or create commits.

- `fcitx5-windows-paths.patch`: Windows built-in resource paths use the installed
  `share`, `share/fcitx5` and `share/locale` directories. Core 5.1.22's Windows
  StandardPaths does not use the earlier resource environment overrides.
- `libime-windows.patch`: imported executable paths include `.exe`; model path
  lists use `;` on Windows, preserving drive letters.
- `chinese-addons-windows.patch`: dictionary loading accepts filesystem paths,
  allowing Windows' wide native paths to reach `std::ifstream`.

The submodule working trees are intentionally patched. Commit the main-repo
patches and version pointers together when preparing a commit; do not commit
unrecorded modifications only inside the submodules.

## Build and Deploy Core

Prerequisites: CMake 3.27+, MSYS2 clang64 Clang/Ninja/pkgconf/ECM/dlfcn/libuv/
gettext, zstd, and Boost headers/iostreams. No script installs or upgrades system
packages. An isolated extra dependency prefix may be passed via
`-DependencyPrefix`; it must contain dependencies for the same MSYS2 runtime.

```powershell
./scripts/build-pinyin.ps1
```

This builds Core, libime (including tools and datasets), then chinese-addons
without Qt configuration tools, OpenCC or cloud Pinyin. Source datasets are
downloaded from the upstream server with SHA256 validation. Language-model
generation can take significant time and memory.

Alternatively, use the pinned precompiled data archive:

```powershell
./scripts/build-pinyin.ps1 -DataMode Prebuilt
```

Only `usr/share/libime` and `usr/lib/libime` are extracted from the SHA256-pinned
Arch Linux libime 1.1.17 package. No Linux executable/library is loaded. The
dictionary and KenLM data formats have been verified with the Windows build.
For offline use, supply `-DataArchive <local-package-path>`. This archive mode is
the locally verified data deployment path; source-data generation has not been
validated end to end in this environment.

The default prefix is `dist/pinyin`. The script recursively resolves PE imports
and copies the necessary MSYS2 runtime DLLs into `bin`, without copying Windows
system DLLs. Start the deployed `dist/pinyin/bin/Fcitx5.exe`, not the build-tree
executable, for input validation. Keep `bin`, `lib` and `share` together when
moving the prefix. User dictionaries remain in Fcitx5's user data directories.

## Build and Test TSF

Use an initialized Visual Studio x64 development environment with LLVM clang,
Windows SDK/ATL, Ninja and ImageMagick, not the MSYS2 clang compiler:

```powershell
cmake -S win32 -B win32/build/pinyin-tsf -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build win32/build/pinyin-tsf
./scripts/test-pinyin.ps1
```

The test script starts only the isolated Core, runs both probes and CTest, then
stops the process it started. It refuses to run alongside an existing Core.
Neither probe registers/unregisters an input method or calls DllRegisterServer.

- `ipc_probe`: real `nihao` candidates and Chinese commits, number/space
  selection, backspace, cancellation, Ctrl+C pass-through, Ctrl+Space switching,
  and separate connections/context ownership.
- `tsf_probe`: actual DLL loading/factory/interfaces, actual Windows TSF document
  contexts backed by an in-memory ITextStoreACP, composition, Chinese commit,
  synchronous-to-asynchronous edit fallback, cancellation, late edits and focus
  isolation. Because this TIP is not registered, a test-only adapter substitutes
  key-sink subscription, and the probe explicitly supplies key/focus callbacks.
  This does not verify Windows' registered TIP activation or OS key dispatch.
  Mode regressions use a real thread-manager keyboard compartment, including
  open/close notifications, cancellation, focus changes and a simulated failure
  to register the exact Ctrl+Space preserved key. Preserved-key callbacks are
  explicitly invoked; the adapter does not prove real OS hotkey dispatch.
- CTest: helper functions, protocol framing/snapshots, UTF-8/UTF-16/key policy,
  and real pipe timeout/cancellation followed by a successful read.

Optional `ENABLE_PINYIN_INTEGRATION_TESTS=ON` registers both probes with CTest;
a deployed Core must already be running for those two tests.

## Protocol and Behavior

Protocol v3 uses a pipe name scoped to the user's SID and Windows session. Its
ACL permits only the current user, rejects remote clients, isolates context IDs
by connection, and permits multiple connections. Clients use cancellable
overlapped I/O with a 500 ms read/write wait limit; disconnected contexts are destroyed.

`SetMode` sets an explicit Pinyin/direct-input mode rather than replaying a
toggle keystroke. Repeated requests are idempotent; changing modes cancels the
current preedit. Core and TSF must both be rebuilt and deployed after this
protocol upgrade; v2 and v3 do not connect to each other.

Each key response contains consumption, mode, commit, preedit, UTF-8 byte cursor,
revision and the current candidate page. Reset cancels Core composition. PollState
collects deferred engine updates on Core's event loop. TSF polls every 100 ms on
its owning thread, rather than using server-pushed updates. Disconnected clients
retry every two seconds. Requests are not replayed after a timeout.

The key test callback performs no IPC or editing. Actual processing runs once
and decides consumption from Core. Each edit session owns its original context,
generation and immutable reply. Queued edits are serialized and stale generations
cannot modify a new context. Once Core consumes a key, an edit failure does not
cause the original key to be replayed; the error resets the input state.

TSF keeps a persistent composition, converts byte cursors into UTF-16 offsets,
applies a dotted underline, replaces the composition range on commit and cancels
on focus loss. A nonactivating Win32 candidate popup uses the context's text
extent and basic DPI/work-area bounds. Candidate selection and paging are handled
by the Pinyin engine; the popup does not insert candidate text itself.

## Remaining Validation and Limits

- Registered activation and keyboard input in Notepad, browsers and other real
  host applications still require explicit approval to register the DLL.
- Candidate rendering, DPI behavior and screen-reader integration are not
  verified in those applications; the window does not implement ITfUIElement.
- The first version has keyboard-only candidates, no candidate mouse selection,
  tray/configuration UI or automatic Core startup.
- Password/secure contexts, dead keys, AltGr/non-US layouts, forwarded keys and
  surrounding-text editing are not claimed as supported. Secure-mode and other
  unimplemented TSF categories are no longer declared.
- Polling is a minimal deferred-output mechanism, not an asynchronous push queue
  or transactional exactly-once delivery across process failures. An IPC timeout
  can lose the pending input; it does not guarantee recovery of a consumed key.
- Only AMD64 Core/TSF/Pinyin was exercised locally. The existing Core ARM64 build
  matrix does not imply Pinyin or TSF ARM64 compatibility.

The first-run Pinyin history load may log a missing/empty-history error; the
system dictionary/model and actual Chinese conversion are verified separately.
