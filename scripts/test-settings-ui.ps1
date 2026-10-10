[CmdletBinding()]
param([string]$Prefix = '', [string]$LogDirectory = '', [string]$TsfBuild = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Prefix) { $Prefix = "$root/dist/pinyin" }
if (!$LogDirectory) { $LogDirectory = "$root/build/settings-ui-test" }
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
if (Get-Process Fcitx5,Fcitx5Settings -ErrorAction SilentlyContinue) {
    throw 'Stop Core and settings before this isolated UI test.'
}
$settingsFile = [IO.Path]::GetFullPath("$LogDirectory/windows.conf")
$coreProcess = $null
$windowProcess = $null
$secondProcess = $null
$title = -join ([char[]](0x8f93,0x5165,0x6cd5,0x8bbe,0x7f6e))
$confirmText = -join ([char[]](0x786e,0x5b9a))
$cancelText = -join ([char[]](0x53d6,0x6d88))
$fullText = -join ([char[]](0x5168,0x62fc))
$doubleText = -join ([char[]](0x53cc,0x62fc))
$xiaoheText = -join ([char[]](0x5c0f,0x9e64))
$ziranmaText = -join ([char[]](0x81ea,0x7136,0x7801))
$dictionaryTab = -join ([char[]](0x8bcd,0x5e93))
$pinyinTab = -join ([char[]](0x62fc,0x97f3))
$dictionaryLoaded = -join ([char[]](0x5df2,0x52a0,0x8f7d))
$dictionaryFailed = -join ([char[]](0x52a0,0x8f7d,0x5931,0x8d25))
$dictionaryFiles = @()
function Start-Core {
    $script:coreProcess = Start-Process "$Prefix/bin/Fcitx5.exe" -WindowStyle Hidden -PassThru `
        -WorkingDirectory "$Prefix/bin" -ArgumentList @('--windows-settings-file', "`"$settingsFile`"") `
        -RedirectStandardOutput "$LogDirectory/core.stdout.log" -RedirectStandardError "$LogDirectory/core.stderr.log"
    Start-Sleep -Seconds 2
    if ($script:coreProcess.HasExited) { throw 'Core failed to start.' }
}
function Open-Settings {
    if ($TsfBuild) {
        & "$TsfBuild/tests/settings_probe.exe" --open-settings
        if ($LASTEXITCODE) { throw 'Core OpenSettings failed.' }
        for ($attempt = 0; $attempt -lt 30; ++$attempt) {
            $script:windowProcess = Get-Process Fcitx5Settings -ErrorAction SilentlyContinue
            if ($script:windowProcess) { break }
            Start-Sleep -Milliseconds 100
        }
        if (!$script:windowProcess) { throw 'Core did not start the settings process.' }
    } else {
        $script:windowProcess = Start-Process "$Prefix/settings/Fcitx5Settings.exe" -WindowStyle Hidden -PassThru `
            -WorkingDirectory "$Prefix/settings"
    }
    for ($attempt = 0; $attempt -lt 50; ++$attempt) {
        Start-Sleep -Milliseconds 100
        $windows = [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
            [System.Windows.Automation.TreeScope]::Children,
            [System.Windows.Automation.PropertyCondition]::new(
                [System.Windows.Automation.AutomationElement]::ProcessIdProperty,$script:windowProcess.Id))
        foreach ($window in $windows) {
            if ($window.Current.Name -eq $title) {
                $script:uiWindow = $window
                $confirm = Find-Control $confirmText
                if ($confirm -and $confirm.Current.IsEnabled) { return }
            }
        }
        if ($script:windowProcess.HasExited) { throw "Settings exited ($($script:windowProcess.ExitCode))." }
    }
    throw 'Settings did not load. Check the x64 Windows App SDK runtime and Core.'
}
function Find-Control([string]$Name) {
    return $script:uiWindow.FindFirst([System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty,$Name))
}
function Select-Scheme([string]$Name) {
    (Find-Control $Name).GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 100
}
function Select-Profile([string]$Name) {
    $combo = $script:uiWindow.FindFirst([System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::ComboBox))
    $expand = $combo.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
    $expand.Expand()
    Start-Sleep -Milliseconds 150
    (Find-Control $Name).GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    $expand.Collapse()
    Start-Sleep -Milliseconds 300
}
function Click([string]$Name) {
    $control = Find-Control $Name
    if (!$control) {
        $script:uiWindow.FindAll([System.Windows.Automation.TreeScope]::Descendants,
            [System.Windows.Automation.Condition]::TrueCondition) | ForEach-Object { Write-Verbose $_.Current.Name }
        throw "Control is missing: $Name"
    }
    $control.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    if (!$script:windowProcess.WaitForExit(5000)) { throw "$Name did not close the settings window." }
}
function Screenshot([string]$Name) {
    $handle = [IntPtr]$script:uiWindow.Current.NativeWindowHandle
    $dpiContext = [FcitxSettingsCapture]::SetThreadDpiAwarenessContext([IntPtr](-4))
    [FcitxSettingsCapture]::ShowWindow($handle, 9) | Out-Null
    [FcitxSettingsCapture]::SetWindowPos($handle, [IntPtr](-1), 0, 0, 0, 0, 0x43) | Out-Null
    [FcitxSettingsCapture]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Milliseconds 500
    $bitmap = $null
    $graphics = $null
    try {
        $bounds = $script:uiWindow.Current.BoundingRectangle
        $bitmap = [Drawing.Bitmap]::new([int]$bounds.Width, [int]$bounds.Height)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $graphics.CopyFromScreen([int]$bounds.X, [int]$bounds.Y, 0, 0, $bitmap.Size)
        $bitmap.Save("$LogDirectory/$Name.png", [Drawing.Imaging.ImageFormat]::Png)
    } finally {
        if ($graphics) { $graphics.Dispose() }
        if ($bitmap) { $bitmap.Dispose() }
        [FcitxSettingsCapture]::SetWindowPos($handle, [IntPtr](-2), 0, 0, 0, 0, 0x13) | Out-Null
        [FcitxSettingsCapture]::SetThreadDpiAwarenessContext($dpiContext) | Out-Null
    }
}

try {
    Start-Core
    Open-Settings
    Screenshot 'full-pinyin'
    (Find-Control $dictionaryTab).GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    $dictionaryDirectory = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Fcitx5/pinyin/dictionaries'
    New-Item -ItemType Directory -Force $dictionaryDirectory | Out-Null
    $fixtureName = "ui-poetry-$([guid]::NewGuid().ToString('N')).dict"
    $brokenName = "ui-broken-$([guid]::NewGuid().ToString('N')).dict"
    $dictionaryFiles = @((Join-Path $dictionaryDirectory $fixtureName), (Join-Path $dictionaryDirectory $brokenName))
    $poetry = -join ([char[]](0x591c,0x6765,0x98ce,0x96e8,0x58f0))
    [IO.File]::WriteAllText("$LogDirectory/poetry.txt", "$poetry ye'lai'feng'yu'sheng 0`n", [Text.UTF8Encoding]::new($false))
    & "$Prefix/bin/libime_pinyindict.exe" "$LogDirectory/poetry.txt" $dictionaryFiles[0]
    if ($LASTEXITCODE -ne 0) { throw 'UI dictionary fixture conversion failed.' }
    [IO.File]::WriteAllText($dictionaryFiles[1], 'invalid dictionary')
    for ($attempt = 0; $attempt -lt 100; ++$attempt) {
        if ((Find-Control $fixtureName) -and (Find-Control $brokenName) -and
            (Find-Control $dictionaryLoaded) -and (Find-Control $dictionaryFailed)) { break }
        Start-Sleep -Milliseconds 100
    }
    if (!(Find-Control $fixtureName) -or !(Find-Control $dictionaryLoaded) -or !(Find-Control $dictionaryFailed)) {
        throw 'Dictionary tab did not show actual loaded/failed Core states.'
    }
    Screenshot 'dictionaries'
    $dictionaryBounds = $script:uiWindow.Current.BoundingRectangle
    $dictionaryHandle = [IntPtr]$script:uiWindow.Current.NativeWindowHandle
    $dictionaryDpi = [FcitxSettingsCapture]::GetDpiForWindow($dictionaryHandle)
    $captureContext = [FcitxSettingsCapture]::SetThreadDpiAwarenessContext([IntPtr](-4))
    try {
        [FcitxSettingsCapture]::SetWindowPos($dictionaryHandle, [IntPtr]::Zero, 0, 0,
            [int](440 * $dictionaryDpi / 96), [int](360 * $dictionaryDpi / 96), 0x16) | Out-Null
        Screenshot 'dictionaries-minimum'
        [FcitxSettingsCapture]::SetWindowPos($dictionaryHandle, [IntPtr]::Zero, 0, 0,
            [int]$dictionaryBounds.Width, [int]$dictionaryBounds.Height, 0x16) | Out-Null
    } finally { [FcitxSettingsCapture]::SetThreadDpiAwarenessContext($captureContext) | Out-Null }
    $folderButton = $script:uiWindow.FindFirst([System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty,'DictionaryFolder'))
    if (!$folderButton) { throw 'Dictionary folder button is missing.' }
    $shell = New-Object -ComObject Shell.Application
    $existingExplorer = @($shell.Windows() | ForEach-Object { $_.HWND })
    $folderButton.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    $opened = $null
    for ($attempt = 0; $attempt -lt 50; ++$attempt) {
        foreach ($explorer in $shell.Windows()) {
            try {
                if ([IO.Path]::GetFullPath($explorer.Document.Folder.Self.Path) -eq [IO.Path]::GetFullPath($dictionaryDirectory)) {
                    $opened = $explorer
                    break
                }
            } catch { }
        }
        if ($opened) { break }
        Start-Sleep -Milliseconds 100
    }
    if (!$opened) { throw 'Dictionary folder button did not open the correct Explorer directory.' }
    if ($opened.HWND -notin $existingExplorer) { $opened.Quit() }
    $script:uiWindow.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).SetWindowVisualState(
        [System.Windows.Automation.WindowVisualState]::Normal)
    (Find-Control $pinyinTab).GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    $originalProfile = $script:uiWindow.FindFirst([System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::ComboBox)).GetCurrentPattern(
                [System.Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()[0].Current.Name
    $secondProcess = Start-Process "$Prefix/settings/Fcitx5Settings.exe" -WindowStyle Hidden -PassThru `
        -WorkingDirectory "$Prefix/settings"
    if (!$secondProcess.WaitForExit(5000)) { throw 'Second settings instance did not redirect activation.' }
    Select-Scheme $doubleText
    Select-Profile $xiaoheText
    Screenshot 'double-pinyin'
    Click $confirmText
    $saved = Get-Content -Raw $settingsFile
    if ($saved -notmatch 'Scheme=Double' -or $saved -notmatch 'Profile=Xiaohe') { throw 'UI did not persist Xiaohe.' }
    Stop-Process -Id $coreProcess.Id
    $coreProcess.WaitForExit()
    $coreProcess = $null
    Start-Core
    Open-Settings
    if (!(Find-Control $doubleText).GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Current.IsSelected) {
        throw 'Core restart did not restore double pinyin.'
    }
    Select-Scheme $fullText
    Click $cancelText
    if ((Get-Content -Raw $settingsFile) -ne $saved) { throw 'Cancel changed settings.' }
    Open-Settings
    Select-Profile $ziranmaText
    Click $confirmText
    if ((Get-Content -Raw $settingsFile) -notmatch 'Profile=Ziranma') { throw 'UI did not persist Ziranma.' }
    Open-Settings
    Select-Profile $originalProfile
    Select-Scheme $fullText
    Click $confirmText
    Write-Output 'PASS: WinUI dictionary states and Explorer folder, Xiaohe/Ziranma selection, save, cancel, restart and single instance.'
} finally {
    foreach ($process in @($windowProcess, $secondProcess, $coreProcess)) {
        if ($process -and !$process.HasExited) {
            Stop-Process -Id $process.Id
            $process.WaitForExit()
        }
    }
    foreach ($file in $dictionaryFiles) {
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
    }
}
