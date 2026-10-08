# Fcitx5 Windows

## Windows Pinyin

Core 5.1.22 connects the TSF DLL to chinese-addons 5.1.15 Pinyin with libime
1.1.17. The basic preedit/candidate/Chinese-commit path is covered by IPC and
unregistered TSF-context probes; registered application input remains unverified.

See [build, deployment and validation](docs/pinyin.md). Start with:

```powershell
git submodule update --init --recursive
./scripts/build-pinyin.ps1 -DataMode Prebuilt
```

Build TSF separately using the Windows SDK/ATL toolchain. No build or test script
registers the DLL or modifies system input-method settings.

## Credits
* [fcitx5](https://github.com/fcitx/fcitx5): LGPL-2.1-or-later
* [weasel](https://github.com/rime/weasel): GPL-3.0-only
