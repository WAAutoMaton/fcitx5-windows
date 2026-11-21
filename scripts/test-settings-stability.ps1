[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SettingsBuild,
    [Parameter(Mandatory)][string]$TsfBuild,
    [string]$LogDirectory = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$settingsExe = (Resolve-Path -LiteralPath "$SettingsBuild/Release/Fcitx5Settings.exe").Path
$fixtureExe = (Resolve-Path -LiteralPath "$TsfBuild/tests/ui_fixture.exe").Path
if (!$settingsExe.StartsWith(($root + '\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Use an isolated Settings build inside the repository.'
}
if (!$LogDirectory) {
    $LogDirectory = "$root/build/settings-stability/$([guid]::NewGuid().ToString('N'))"
}
$LogDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($LogDirectory)
New-Item -ItemType Directory -Force $LogDirectory | Out-Null
$probe = Start-Process $settingsExe -WindowStyle Hidden -PassThru -ArgumentList '--check-settings-diagnostics-test'
try {
    if (!$probe.WaitForExit(10000) -or $probe.ExitCode -ne 0) {
        throw 'Settings must be built with -UiTest.'
    }
} finally {
    if (!$probe.HasExited) { Stop-Process -Id $probe.Id; $probe.WaitForExit() }
    $probe.Dispose()
}

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
if (!('FcitxStabilityWindow' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FcitxStabilityWindow {
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);
}
'@
}

New-Item -ItemType Directory -Force "$SettingsBuild/share/locale" | Out-Null
Copy-Item "$TsfBuild/share/locale/*" "$SettingsBuild/share/locale" -Recurse -Force

$previousDiagnosticDirectory = $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY
$diagnosticDirectory = Join-Path $LogDirectory 'diagnostics'
$env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY = $diagnosticDirectory

function Find-Id($Window, [string]$Id) {
    $Window.FindFirst([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty, $Id))
}
function Find-Name($Window, [string]$Name) {
    $Window.FindFirst([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $Name))
}
function Wait-Ready($Window, $Process) {
    for ($i = 0; $i -lt 100; ++$i) {
        if ($Process.HasExited) { throw "Settings exited unexpectedly: 0x$($Process.ExitCode.ToString('X8'))" }
        $button = Find-Id $Window 'GlobalSettings'
        if ($button -and $button.Current.IsEnabled) { return }
        Start-Sleep -Milliseconds 100
    }
    throw 'Settings did not become ready.'
}
function Invoke-Control($Control) {
    if (!$Control) { throw 'Control missing.' }
    $Control.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 250
}
function Select-Control($Control) {
    if (!$Control) { throw 'Selection control missing.' }
    $Control.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 250
}
function Toggle-Global($Window, $Process) {
    (Find-Id $Window 'GlobalSettings').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
    Start-Sleep -Milliseconds 250
    Wait-Ready $Window $Process
}
function Assert-CleanExit([string]$CaseName, $Process) {
    if (!$Process.WaitForExit(10000)) { throw "${CaseName}: Settings did not exit." }
    if ($Process.ExitCode -ne 0) {
        throw "${CaseName}: Settings exited abnormally with 0x$($Process.ExitCode.ToString('X8')) ($($Process.ExitCode))."
    }
    $files = @(Get-ChildItem -LiteralPath $diagnosticDirectory -Filter "settings-$($Process.Id)-*.log*" -File -ErrorAction SilentlyContinue)
    if (!$files) { throw "${CaseName}: Missing diagnostic log for pid $($Process.Id)." }
    $events = @($files | ForEach-Object { Get-Content -LiteralPath $_.FullName -Encoding UTF8 | ConvertFrom-Json })
    $fatal = @($events | Where-Object { $_.event -eq 'settings_exception' -and $_.fatal })
    if ($fatal.Count) { throw "${CaseName}: Recorded fatal exception." }
    $exits = @($events | Where-Object { $_.event -eq 'settings_exit' })
    if ($exits.Count -ne 1 -or $exits[0].exit_code -ne 0 -or $exits[0].reason -ne 'dispatcher_returned') {
        throw "${CaseName}: Missing normal settings_exit event."
    }
}

function Run-Case([string]$CaseName, [scriptblock]$Body) {
    $confFile = Join-Path $LogDirectory "$CaseName.conf"
    [IO.File]::WriteAllText($confFile, "[General]`nVersion=2`nLanguage=en`nDetailedLogging=true`nRevision=1`n")
    $fixture = $null
    $process = $null
    try {
        $fixture = Start-Process $fixtureExe -ArgumentList "`"$confFile`"" -WindowStyle Hidden -PassThru
        Start-Sleep -Milliseconds 300
        if ($fixture.HasExited) { throw "${CaseName}: ui_fixture exited early ($($fixture.ExitCode))." }
        $process = Start-Process $settingsExe -WindowStyle Hidden -PassThru
        $window = $null
        for ($i = 0; $i -lt 100; ++$i) {
            Start-Sleep -Milliseconds 100
            if ($process.HasExited) { throw "${CaseName}: Settings exited on startup: 0x$($process.ExitCode.ToString('X8'))" }
            $window = [Windows.Automation.AutomationElement]::RootElement.FindFirst(
                [Windows.Automation.TreeScope]::Children,
                [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty, $process.Id))
            if ($window) { break }
        }
        if (!$window) { throw "${CaseName}: Settings window did not appear." }
        Wait-Ready $window $process
        & $Body $window $process
        Assert-CleanExit $CaseName $process
    } finally {
        foreach ($proc in @($process, $fixture)) {
            if ($proc -and !$proc.HasExited) { Stop-Process -Id $proc.Id -Force; $proc.WaitForExit() }
        }
    }
}

try {
    # 1. Global -> Shortcuts -> General -> Apply -> Pinyin -> Global -> Close button
    Run-Case 'shortcuts-inactive-apply-and-reenter' {
        param($w, $p)
        Toggle-Global $w $p
        Select-Control (Find-Id $w 'ShortcutOptionsTab')
        Select-Control (Find-Id $w 'GeneralOptions')
        Invoke-Control (Find-Name $w 'Apply')
        Wait-Ready $w $p
        Select-Control (Find-Id $w 'method:pinyin')
        Wait-Ready $w $p
        Toggle-Global $w $p
        Invoke-Control (Find-Name $w 'Close')
    }

    # 2. Global -> Developer -> General -> Apply -> Close via WM_CLOSE (AppWindow.Closing path)
    Run-Case 'developer-inactive-apply-and-wmclose' {
        param($w, $p)
        Toggle-Global $w $p
        Select-Control (Find-Id $w 'DeveloperOptions')
        Select-Control (Find-Id $w 'GeneralOptions')
        Invoke-Control (Find-Name $w 'Apply')
        Start-Sleep -Milliseconds 600
        Wait-Ready $w $p
        $hwnd = [IntPtr]$w.Current.NativeWindowHandle
        [FcitxStabilityWindow]::PostMessageW($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    }

    # 3. Pinyin -> Dictionaries -> Keyboard -> Close button (previously crashed in TabView::~TabView ToolTip teardown)
    Run-Case 'pinyin-dictionaries-switch-close' {
        param($w, $p)
        Select-Control (Find-Id $w 'method:pinyin')
        Wait-Ready $w $p
        Select-Control (Find-Name $w 'Dictionaries')
        Select-Control (Find-Id $w 'method:keyboard-us')
        Wait-Ready $w $p
        Invoke-Control (Find-Name $w 'Close')
    }

    # 4. Open AddInputMethod child window and close main window while child window is still open
    Run-Case 'add-window-open-main-close' {
        param($w, $p)
        Invoke-Control (Find-Id $w 'AddInputMethod')
        $addWindow = $null
        for ($i = 0; $i -lt 30; ++$i) {
            $windows = [Windows.Automation.AutomationElement]::RootElement.FindAll(
                [Windows.Automation.TreeScope]::Children,
                [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id))
            foreach ($candidate in $windows) {
                if (Find-Id $candidate 'AddInputMethodLanguages') { $addWindow = $candidate; break }
            }
            if ($addWindow) { break }
            Start-Sleep -Milliseconds 100
        }
        if (!$addWindow) { throw 'Add input method window did not open.' }
        Invoke-Control (Find-Name $w 'Close')
    }

    Write-Output "PASS: inactive-tab shortcuts/developer refresh, TabView close teardown (button + WM_CLOSE), and AddInputMethod child window close all exited cleanly with exit code 0. Logs: $LogDirectory"
} finally {
    $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY = $previousDiagnosticDirectory
}
