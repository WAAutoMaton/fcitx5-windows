[CmdletBinding()]
param([string]$Prefix = '', [string]$LogDirectory = '', [string]$TsfBuild = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Prefix) { $Prefix = "$root/dist/pinyin" }
if (!$LogDirectory) { $LogDirectory = "$root/build/settings-ui-test/$([guid]::NewGuid().ToString('N'))" }
New-Item -ItemType Directory -Force $LogDirectory | Out-Null
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
if (!('FcitxSettingsCapture' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FcitxSettingsCapture {
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
}
'@
}
if (Get-Process Fcitx5,Fcitx5Settings -ErrorAction SilentlyContinue) { throw 'Core or Settings is already running; isolated UI tests need a free endpoint.' }
$coreProcess = $null; $windowProcess = $null; $secondProcess = $null
$settingsFile = [IO.Path]::GetFullPath("$LogDirectory/windows.conf")
[IO.File]::WriteAllText("$LogDirectory/windows-ui.conf", "[General]`nVersion=1`nLanguage=zh-Hans`nRevision=1`n")
$configuration = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Fcitx5/config/fcitx5'
$backups = @{}
foreach ($relative in @('conf/pinyin.conf', 'table/wbx.conf')) {
    $path = Join-Path $configuration $relative
    $backups[$path] = if (Test-Path -LiteralPath $path) { [IO.File]::ReadAllBytes($path) } else { $null }
}
$dictionaryFiles = @()
function Text([int[]]$Codepoints) { return -join ([char[]]$Codepoints) }
function Find-Id([string]$Id) {
    return $script:uiWindow.FindFirst([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$Id))
}
function Find-Name([string]$Name) {
    return $script:uiWindow.FindFirst([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$Name))
}
function Invoke-Control($Control) {
    if (!$Control) { throw 'Control is missing.' }
    $Control.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 350
}
function Wait-Ready {
    for ($attempt = 0; $attempt -lt 80; ++$attempt) {
        Start-Sleep -Milliseconds 100
        $apply = Find-Name (Text @(0x5e94,0x7528))
        if ($apply -and $apply.Current.IsEnabled) { return }
        if ($windowProcess.HasExited) { throw "Settings exited ($($windowProcess.ExitCode))." }
    }
    throw 'Settings did not become ready.'
}
function Start-Core {
    $script:coreProcess = Start-Process "$Prefix/bin/Fcitx5.exe" -WindowStyle Hidden -PassThru -WorkingDirectory "$Prefix/bin" `
        -ArgumentList @('--windows-settings-file', "`"$settingsFile`"") -RedirectStandardOutput "$LogDirectory/core.stdout.log" -RedirectStandardError "$LogDirectory/core.stderr.log"
    Start-Sleep -Seconds 2
    if ($script:coreProcess.HasExited) { throw 'Core failed to start.' }
}
function Open-Settings {
    if ($TsfBuild) {
        & "$TsfBuild/tests/settings_probe.exe" --open-settings
        if ($LASTEXITCODE) { throw 'Core OpenSettings failed.' }
        $script:windowProcess = $null
        for ($attempt = 0; $attempt -lt 40; ++$attempt) {
            $script:windowProcess = Get-Process Fcitx5Settings -ErrorAction SilentlyContinue
            if ($script:windowProcess) { break }; Start-Sleep -Milliseconds 100
        }
    } else {
        $script:windowProcess = Start-Process "$Prefix/settings/Fcitx5Settings.exe" -WindowStyle Hidden -PassThru -WorkingDirectory "$Prefix/settings"
    }
    if (!$script:windowProcess) { throw 'Settings process is missing.' }
    for ($attempt = 0; $attempt -lt 60; ++$attempt) {
        Start-Sleep -Milliseconds 100
        $script:uiWindow = [Windows.Automation.AutomationElement]::RootElement.FindFirst([Windows.Automation.TreeScope]::Children,
            [Windows.Automation.AndCondition]::new(
                [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,$script:windowProcess.Id),
                [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,(Text @(0x8f93,0x5165,0x6cd5,0x8bbe,0x7f6e)))))
        if ($script:uiWindow) { Wait-Ready; return }
    }
    throw 'Settings window is missing.'
}
function Select-Method([string]$Id) {
    $item = Find-Id "method:$Id"
    if (!$item) { throw "Missing input method: $Id" }
    $item.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 350; Wait-Ready
}
function Select-Combo([string]$Id, [string]$Name, [string]$ItemId = '') {
    $combo = Find-Id $Id
    if (!$combo) { throw "Missing combo: $Id" }
    $expand = $combo.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern)
    $expand.Expand(); Start-Sleep -Milliseconds 200
    $item = if ($ItemId) { Find-Id $ItemId } else { Find-Name $Name }
    if (!$item) { throw "Missing choice: $Name $ItemId" }
    $item.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    $expand.Collapse(); Start-Sleep -Milliseconds 150
}
function Add-Method([string]$Id) {
    Invoke-Control (Find-Id 'AddInputMethod')
    Select-Combo 'AvailableInputMethods' '' "available:$Id"
    Invoke-Control (Find-Name (Text @(0x6dfb,0x52a0)))
    Wait-Ready
    if (!(Find-Id "method:$Id")) { throw "Input method was not added: $Id" }
}
function Apply {
    Invoke-Control (Find-Name (Text @(0x5e94,0x7528))); Wait-Ready
}
function Close-Settings {
    Invoke-Control (Find-Name (Text @(0x5173,0x95ed)))
    if (!$script:windowProcess.WaitForExit(5000)) { throw 'Settings did not close.' }
}
function Screenshot([string]$Name, [int]$Width = 0, [int]$Height = 0) {
    $handle = [IntPtr]$script:uiWindow.Current.NativeWindowHandle
    $context = [FcitxSettingsCapture]::SetThreadDpiAwarenessContext([IntPtr](-4))
    try {
        [FcitxSettingsCapture]::ShowWindow($handle, 9) | Out-Null
        [FcitxSettingsCapture]::SetWindowPos($handle, [IntPtr](-1), 0, 0, 0, 0, 0x43) | Out-Null
        if ($Width) {
            $dpi = [FcitxSettingsCapture]::GetDpiForWindow($handle)
            [FcitxSettingsCapture]::SetWindowPos($handle, [IntPtr]::Zero, 0, 0, [int]($Width * $dpi / 96), [int]($Height * $dpi / 96), 0x16) | Out-Null
        }
        [FcitxSettingsCapture]::SetForegroundWindow($handle) | Out-Null
        Start-Sleep -Milliseconds 500
        $bounds = $script:uiWindow.Current.BoundingRectangle
        $bitmap = [Drawing.Bitmap]::new([int]$bounds.Width, [int]$bounds.Height)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try { $graphics.CopyFromScreen([int]$bounds.X, [int]$bounds.Y, 0, 0, $bitmap.Size); $bitmap.Save("$LogDirectory/$Name.png", [Drawing.Imaging.ImageFormat]::Png) }
        finally { $graphics.Dispose(); $bitmap.Dispose() }
    } finally {
        [FcitxSettingsCapture]::SetWindowPos($handle, [IntPtr](-2), 0, 0, 0, 0, 0x13) | Out-Null
        [FcitxSettingsCapture]::SetThreadDpiAwarenessContext($context) | Out-Null
    }
}
try {
    Start-Core; Open-Settings
    if (!(Find-Id 'method:keyboard-us') -or !(Find-Id 'method:pinyin')) { throw 'Default methods are missing.' }
    Screenshot 'keyboard'
    Select-Method 'pinyin'; Screenshot 'pinyin'
    $fuzzy = Find-Id 'Fuzzy/IN_ING'
    if (!$fuzzy) { throw 'Fuzzy pinyin control is missing.' }
    $fuzzy.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle(); Apply
    (Find-Name (Text @(0x8bcd,0x5e93))).GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    $dictionaryDirectory = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Fcitx5/pinyin/dictionaries'
    New-Item -ItemType Directory -Force $dictionaryDirectory | Out-Null
    $fixture = "ui-poetry-$([guid]::NewGuid().ToString('N')).dict"
    $broken = "ui-broken-$([guid]::NewGuid().ToString('N')).dict"
    $dictionaryFiles = @((Join-Path $dictionaryDirectory $fixture), (Join-Path $dictionaryDirectory $broken))
    [IO.File]::WriteAllText("$LogDirectory/poetry.txt", "$(Text @(0x591c,0x6765,0x98ce,0x96e8,0x58f0)) ye'lai'feng'yu'sheng 0`n", [Text.UTF8Encoding]::new($false))
    & "$Prefix/bin/libime_pinyindict.exe" "$LogDirectory/poetry.txt" $dictionaryFiles[0]
    if ($LASTEXITCODE) { throw 'Dictionary conversion failed.' }
    [IO.File]::WriteAllText($dictionaryFiles[1], 'invalid dictionary')
    for ($attempt = 0; $attempt -lt 100; ++$attempt) {
        if ((Find-Name $fixture) -and (Find-Name $broken) -and (Find-Name (Text @(0x5df2,0x52a0,0x8f7d))) -and (Find-Name (Text @(0x52a0,0x8f7d,0x5931,0x8d25)))) { break }
        Start-Sleep -Milliseconds 100
    }
    if (!(Find-Name $fixture) -or !(Find-Name (Text @(0x52a0,0x8f7d,0x5931,0x8d25)))) { throw 'Dictionary states are missing.' }
    Screenshot 'dictionaries'; Screenshot 'dictionaries-minimum' 720 480
    $folder = Find-Id 'DictionaryFolder'
    if (!$folder) { throw 'Dictionary folder command is missing.' }
    $shell = New-Object -ComObject Shell.Application
    $existingExplorer = @($shell.Windows() | ForEach-Object { $_.HWND })
    Invoke-Control $folder
    $opened = $null
    for ($attempt = 0; $attempt -lt 50; ++$attempt) {
        foreach ($explorer in $shell.Windows()) {
            try { if ([IO.Path]::GetFullPath($explorer.Document.Folder.Self.Path) -eq [IO.Path]::GetFullPath($dictionaryDirectory)) { $opened = $explorer; break } } catch {}
        }
        if ($opened) { break }; Start-Sleep -Milliseconds 100
    }
    if (!$opened) { throw 'Dictionary folder command did not open the correct directory.' }
    if ($opened.HWND -notin $existingExplorer) { $opened.Quit() }
    (Find-Name (Text @(0x62fc,0x97f3,0x8bbe,0x7f6e))).GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Screenshot 'pinyin-minimum' 720 480
    Screenshot 'pinyin-restored' 980 720
    Add-Method 'shuangpin'; Select-Method 'shuangpin'
    Select-Combo 'ShuangpinProfile' (Text @(0x5c0f,0x9e64)); Apply
    Screenshot 'shuangpin'
    Add-Method 'wbx'; Select-Method 'wbx'
    (Find-Id 'PrevPage').GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue("Up`r`nminus")
    Apply
    if ((Get-Content -Raw $settingsFile) -notmatch '(?m)\[Options/wbx/PrevPage\]\r?\n0=Up\r?\n1=minus') {
        throw 'Multiline table key settings did not persist.'
    }
    Screenshot 'wubi'
    Invoke-Control (Find-Id 'MoveInputMethodUp'); Wait-Ready
    Invoke-Control (Find-Id 'RemoveInputMethod'); Wait-Ready
    if (Find-Id 'method:wbx') { throw 'Wubi removal failed.' }
    Select-Method 'shuangpin'; Invoke-Control (Find-Id 'RemoveInputMethod'); Wait-Ready
    Select-Method 'keyboard-us'; Invoke-Control (Find-Id 'RemoveInputMethod'); Wait-Ready
    Select-Method 'pinyin'; Invoke-Control (Find-Id 'RemoveInputMethod'); Wait-Ready
    if (!(Find-Id 'method:pinyin') -or !(Find-Name (Text @(0x81f3,0x5c11,0x4fdd,0x7559,0x4e00,0x4e2a,0x53ef,0x7528,0x8f93,0x5165,0x6cd5,0xff0c,0x8bf7,0x5148,0x6dfb,0x52a0,0x65b0,0x7684,0x8f93,0x5165,0x6cd5,0x518d,0x5220,0x9664,0x3002)))) { throw 'Last method deletion guard is missing.' }
    Screenshot 'last-input-method'
    Add-Method 'keyboard-us'; Add-Method 'wbx'
    $secondProcess = Start-Process "$Prefix/settings/Fcitx5Settings.exe" -WindowStyle Hidden -PassThru -WorkingDirectory "$Prefix/settings"
    if (!$secondProcess.WaitForExit(5000)) { throw 'Second Settings instance did not redirect activation.' }
    Close-Settings
    $saved = Get-Content -Raw $settingsFile
    Stop-Process -Id $coreProcess.Id; $coreProcess.WaitForExit(); $coreProcess = $null
    Start-Core; Open-Settings
    if (!(Find-Id 'method:wbx') -or !(Find-Id 'method:pinyin')) { throw 'Restart lost the input method list.' }
    Select-Method 'pinyin'
    (Find-Id 'Fuzzy/IN_ING').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
    Close-Settings
    if ((Get-Content -Raw $settingsFile) -ne $saved) { throw 'Close saved an unapplied draft.' }
    Write-Output "PASS: WinUI shared settings, dictionaries/folder, add/remove/reorder, last method guard, restart, draft discard, single instance and screenshots. Logs: $LogDirectory"
} finally {
    foreach ($process in @($windowProcess, $secondProcess, $coreProcess)) {
        if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit() }
    }
    foreach ($file in $dictionaryFiles) { if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file } }
    foreach ($path in $backups.Keys) {
        if ($null -ne $backups[$path]) { [IO.File]::WriteAllBytes($path, $backups[$path]) }
        elseif (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
    }
}
