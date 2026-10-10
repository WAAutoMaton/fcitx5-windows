[CmdletBinding()]
param([string]$Prefix = '', [string]$LogDirectory = '', [string]$TsfBuild = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Prefix) { $Prefix = "$root/dist/pinyin" }
if (!$LogDirectory) { $LogDirectory = "$root/build/settings-ui-test" }
New-Item -ItemType Directory -Force $LogDirectory | Out-Null
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
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
    $bounds = $script:uiWindow.Current.BoundingRectangle
    $bitmap = [Drawing.Bitmap]::new([int]$bounds.Width, [int]$bounds.Height)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen([int]$bounds.X, [int]$bounds.Y, 0, 0, $bitmap.Size)
        $bitmap.Save("$LogDirectory/$Name.png", [Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}

try {
    Start-Core
    Open-Settings
    Screenshot 'full-pinyin'
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
    Write-Output 'PASS: WinUI framework-dependent window, Xiaohe/Ziranma selection, save, cancel, restart and single instance.'
} finally {
    foreach ($process in @($windowProcess, $secondProcess, $coreProcess)) {
        if ($process -and !$process.HasExited) {
            Stop-Process -Id $process.Id
            $process.WaitForExit()
        }
    }
}
