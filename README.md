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

## Run

After building the TSF project in `win32/build/pinyin-tsf`, register the DLL from
an elevated PowerShell:

```powershell
$dll = (Resolve-Path ".\win32\build\pinyin-tsf\dll\fcitx5-x86_64.dll").Path
& "$env:WINDIR\System32\regsvr32.exe" $dll
```

Start the Core from a normal PowerShell in the repository root:

```powershell
$core = (Resolve-Path ".\dist\pinyin\bin\Fcitx5.exe").Path
Start-Process -FilePath $core -WorkingDirectory (Split-Path $core)
```

Use `Win+Space` to select `Fcitx5`, then type `nihao` in a text application.
Use number keys or Space to select a candidate, and `Ctrl+Space` to switch
between Pinyin and direct input. Core must remain running while the TSF is used.

To stop Core, run `Get-Process Fcitx5 | Stop-Process`. To unregister the TSF,
run the following command as administrator:

```powershell
& "$env:WINDIR\System32\regsvr32.exe" /u $dll
```

## Credits
* [fcitx5](https://github.com/fcitx/fcitx5): LGPL-2.1-or-later
* [weasel](https://github.com/rime/weasel): GPL-3.0-only
