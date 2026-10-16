[CmdletBinding()]
param([Parameter(Mandatory)][string]$SettingsBuild, [Parameter(Mandatory)][string]$TsfBuild,
      [string]$LogDirectory = '', [switch]$TestLogging)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$LogDirectory) { $LogDirectory = "$root/build/ui-localization-test/$([guid]::NewGuid().ToString('N'))" }
New-Item -ItemType Directory -Force $LogDirectory | Out-Null
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
if (!('FcitxLocalizationWindow' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FcitxLocalizationWindow {
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int width, int height, uint flags);
}
'@
}
$settingsExe = (Resolve-Path "$SettingsBuild/Release/Fcitx5Settings.exe").Path
$fixtureExe = (Resolve-Path "$TsfBuild/tests/ui_fixture.exe").Path
$file = [IO.Path]::GetFullPath("$LogDirectory/windows-ui.conf")
New-Item -ItemType Directory -Force "$SettingsBuild/share/locale" | Out-Null
Copy-Item "$TsfBuild/share/locale/*" "$SettingsBuild/share/locale" -Recurse -Force
$fixture = $null; $settings = $null
$previousDiagnosticDirectory = $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY
$env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY = [IO.Path]::GetFullPath("$LogDirectory/diagnostics")
function Find-Id([string]$Id) {
    $script:window.FindFirst([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty, $Id))
}
function Find-Name([string]$Name) {
    $script:window.FindFirst([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $Name))
}
function Text([int[]]$Points) { -join ([char[]]$Points) }
function Wait-Ready {
    for ($i = 0; $i -lt 100; ++$i) {
        $button = Find-Id 'GlobalSettings'
        if ($button -and $button.Current.IsEnabled) { return }
        Start-Sleep -Milliseconds 100
    }
    throw 'UI did not become ready.'
}
function Invoke($Control) {
    if (!$Control) { throw 'Missing control.' }
    $Control.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 250
}
function Global {
    $button = Find-Id 'GlobalSettings'
    $button.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
    Start-Sleep -Milliseconds 250
    Wait-Ready
    $general = Find-Id 'GeneralOptions'
    if ($general) {
        $general.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
        Start-Sleep -Milliseconds 100
    }
    if (!(Find-Id 'UiLanguage')) { throw 'Global language control is missing.' }
}
function Choose-Language([string]$Name) {
    $combo = Find-Id 'UiLanguage'
    $expansion = $combo.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern)
    $expansion.Expand(); Start-Sleep -Milliseconds 150
    $item = Find-Name $Name
    if (!$item) { throw "Language choice missing: $Name" }
    $item.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    $expansion.Collapse(); Start-Sleep -Milliseconds 150
}
function Start-Fixture {
    $script:fixture = Start-Process $fixtureExe -ArgumentList "`"$file`"" -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 300
}
function Open-Settings([string]$Title) {
    $script:settings = Start-Process $settingsExe -WindowStyle Hidden -PassThru
    for ($i = 0; $i -lt 100; ++$i) {
        Start-Sleep -Milliseconds 100
        $script:window = [Windows.Automation.AutomationElement]::RootElement.FindFirst([Windows.Automation.TreeScope]::Children,
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty, $script:settings.Id))
        if ($script:window) {
            Wait-Ready
            if ($script:window.Current.Name -ne $Title) { throw "Wrong title: $($script:window.Current.Name)" }
            return
        }
        if ($script:settings.HasExited) { throw "Test Settings exited: $($script:settings.ExitCode)" }
    }
    throw 'Test window did not open.'
}
function Close-Settings([string]$CloseLabel) {
    Invoke (Find-Name $CloseLabel)
    if (!$script:settings.WaitForExit(10000)) { throw 'Test Settings did not close.' }
    if ($script:settings.ExitCode -ne 0) { throw "Test Settings exited abnormally: $($script:settings.ExitCode)" }
}
function Stop-Fixture {
    if ($script:fixture -and !$script:fixture.HasExited) {
        Stop-Process -Id $script:fixture.Id; $script:fixture.WaitForExit()
    }
}
function Capture([string]$Name, [int]$Width = 0, [int]$Height = 0) {
    if ($Width) {
        $handle = [IntPtr]$script:window.Current.NativeWindowHandle
        $dpi = [FcitxLocalizationWindow]::GetDpiForWindow($handle)
        [FcitxLocalizationWindow]::SetWindowPos($handle, [IntPtr]::Zero, 0, 0, [int]($Width * $dpi / 96), [int]($Height * $dpi / 96), 0x16) | Out-Null
        Start-Sleep -Milliseconds 300
    }
    $bounds = $script:window.Current.BoundingRectangle
    $bitmap = [Drawing.Bitmap]::new([int]$bounds.Width, [int]$bounds.Height)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen([int]$bounds.X, [int]$bounds.Y, 0, 0, $bitmap.Size)
        $bitmap.Save("$LogDirectory/$Name.png", [Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}
try {
    [IO.File]::WriteAllText($file, "[General]`nVersion=1`nLanguage=en`nRevision=1`n")
    Start-Fixture; Open-Settings 'Input Method Settings'; Global
    if ($TestLogging) {
        function Developer {
            (Find-Id 'DeveloperOptions').GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
            Start-Sleep -Milliseconds 300
        }
        function Toggle-Logging {
            (Find-Id 'DetailedLogging').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
            Start-Sleep -Milliseconds 100
        }
        function Assert-Logging([bool]$Enabled) {
            $expected = if ($Enabled) { [Windows.Automation.ToggleState]::On } else { [Windows.Automation.ToggleState]::Off }
            if ((Find-Id 'DetailedLogging').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Current.ToggleState -ne $expected) {
                throw "Wrong logging toggle state: expected $Enabled"
            }
        }
        function Discard-LoggingDraft {
            (Find-Id 'method:pinyin').GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
            Start-Sleep -Milliseconds 300
            Invoke (Find-Name 'Discard'); Wait-Ready
            Global
            Developer
        }
        Developer
        Assert-Logging $false
        if (Test-Path -LiteralPath $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY) { throw 'Disabled Settings created logs.' }
        Invoke (Find-Id 'OpenLogFolder')
        $shell = New-Object -ComObject Shell.Application
        $opened = $false
        for ($attempt = 0; $attempt -lt 30 -and !$opened; ++$attempt) {
            Start-Sleep -Milliseconds 200
            foreach ($folder in $shell.Windows()) {
                if ($folder.LocationURL -and
                    ([uri]$folder.LocationURL).LocalPath.TrimEnd('\', '/') -ieq $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY.TrimEnd('\', '/')) {
                    $folder.Quit()
                    $opened = $true
                    break
                }
            }
        }
        if (!$opened) { throw 'Log folder button did not open the isolated directory.' }
        Assert-Logging $false
        if (Get-ChildItem -LiteralPath $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY -File) { throw 'Opening the folder enabled logging.' }
        Toggle-Logging
        Invoke (Find-Name 'Apply'); Wait-Ready
        if ((Get-Content $file -Raw) -notmatch 'DetailedLogging=true') { throw 'Logging was not saved.' }
        Start-Sleep -Milliseconds 700
        if (!(Get-ChildItem -LiteralPath $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY -Filter 'settings-*.log')) { throw 'Settings log missing.' }
        Capture 'developer-logging-on'
        Capture 'developer-logging-minimum' 720 480
        Toggle-Logging
        (Find-Id 'method:pinyin').GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
        Start-Sleep -Milliseconds 300
        Invoke (Find-Name 'Cancel'); Wait-Ready
        Assert-Logging $false
        Discard-LoggingDraft
        Assert-Logging $true
        Toggle-Logging
        $external = (Get-Content $file -Raw) -replace 'Revision=2', 'Revision=3'
        [IO.File]::WriteAllText($file, $external)
        Invoke (Find-Name 'Apply'); Wait-Ready
        if ((Get-Content $file -Raw) -notmatch 'DetailedLogging=true') { throw 'Stale draft overwrote logging.' }
        Invoke (Find-Name 'Reload'); Wait-Ready
        Developer
        Assert-Logging $true
        Invoke (Find-Name 'Reset to Defaults')
        Assert-Logging $false
        if ((Get-Content $file -Raw) -notmatch 'DetailedLogging=true') { throw 'Reset saved a draft.' }
        Discard-LoggingDraft
        Close-Settings 'Close'
        $exitEvents = @(Get-ChildItem -LiteralPath $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY -Filter "settings-$($settings.Id)-*.log*" -File |
            ForEach-Object { Get-Content -LiteralPath $_.FullName -Encoding UTF8 | ConvertFrom-Json })
        foreach ($eventName in @('settings_close_requested', 'settings_window_closed', 'settings_dispatcher_returned', 'settings_runtime_shutdown_begin', 'settings_exit')) {
            if (!($exitEvents | Where-Object event -eq $eventName)) { throw "Missing actual WinUI shutdown event: $eventName" }
        }
        $normalExit = @($exitEvents | Where-Object event -eq 'settings_exit')
        if ($normalExit.Count -ne 1 -or $normalExit[0].exit_code -ne 0 -or $normalExit[0].reason -ne 'dispatcher_returned') {
            throw 'Actual WinUI shutdown did not report normal dispatcher exit.'
        }
        foreach ($operationName in @('render_global', 'render_config')) {
            if (!($exitEvents | Where-Object { $_.event -eq 'settings_operation_end' -and $_.operation -eq $operationName })) {
                throw "Missing actual WinUI page render: $operationName"
            }
        }
        if ($exitEvents | Where-Object { $_.event -eq 'settings_exception' -and $_.fatal }) { throw 'Unexpected fatal WinUI exception.' }
        Stop-Fixture
        Start-Fixture; Open-Settings 'Input Method Settings'; Global; Developer
        Assert-Logging $true
        Toggle-Logging
        Invoke (Find-Name 'Apply'); Wait-Ready
        Assert-Logging $false
        Start-Sleep -Milliseconds 500
        $total = (Get-ChildItem -LiteralPath $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY -File | Measure-Object Length -Sum).Sum
        Invoke (Find-Name 'Apply'); Wait-Ready
        Start-Sleep -Milliseconds 500
        if ((Get-ChildItem -LiteralPath $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY -File | Measure-Object Length -Sum).Sum -ne $total) {
            throw 'Settings recorded events after logging was disabled.'
        }
        if ((Find-Id 'LogStatus').Current.Name -ne 'Off') { throw 'Disabled logging status missing.' }
        if (!(Find-Id 'OpenLogFolder') -or !(Find-Id 'LogStatistics')) { throw 'Log folder controls missing.' }
        Capture 'developer-logging-off'
        Close-Settings 'Close'
        Write-Output "PASS: actual WinUI developer page, default off, directory button, save, draft, conflict, reset, restart and disable. Screenshots: $LogDirectory"
        return
    }
    $languageCombo = Find-Id 'UiLanguage'
    $expandChoices = $languageCombo.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern)
    $expandChoices.Expand(); Start-Sleep -Milliseconds 150
    foreach ($choice in @('Follow System Language', (Text @(0x7b80,0x4f53,0x4e2d,0x6587)), (Text @(0x7e41,0x9ad4,0x4e2d,0x6587)), 'English')) {
        if (!(Find-Name $choice)) { throw "Missing language choice: $choice" }
    }
    $expandChoices.Collapse()
    Start-Sleep -Milliseconds 300
    $globalBounds = (Find-Id 'GlobalSettings').Current.BoundingRectangle
    $upBounds = (Find-Id 'MoveInputMethodUp').Current.BoundingRectangle
    $resetBounds = (Find-Name 'Reset to Defaults').Current.BoundingRectangle
    if ($globalBounds.Top -lt $upBounds.Bottom -or $globalBounds.Bottom -gt $resetBounds.Top) {
        Capture 'global-placement-failure'
        throw "Global navigation is misplaced: global=$globalBounds up=$upBounds reset=$resetBounds"
    }
    foreach ($id in @('RemoveInputMethod', 'MoveInputMethodUp', 'MoveInputMethodDown')) {
        if ((Find-Id $id).Current.IsEnabled) { throw "Global page enabled $id" }
    }
    Choose-Language (Text @(0x7b80,0x4f53,0x4e2d,0x6587))
    Invoke (Find-Name 'Apply'); Wait-Ready
    if ((Get-Content $file -Raw) -notmatch 'Language=zh-Hans' -or $window.Current.Name -ne 'Input Method Settings') { throw 'Save or deferred language failed.' }
    Capture 'global-english'
    Capture 'global-english-minimum' 720 480
    Choose-Language 'English'
    (Find-Id 'method:pinyin').GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 300
    Invoke (Find-Name 'Cancel'); Wait-Ready
    if ((Find-Id 'UiLanguage').Current.IsOffscreen) { throw 'Cancel left the global page.' }
    (Find-Id 'method:pinyin').GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 300
    Invoke (Find-Name 'Discard'); Wait-Ready
    if ((Find-Id 'GlobalSettings').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Current.ToggleState -ne [Windows.Automation.ToggleState]::Off) { throw 'Input method navigation failed.' }
    Global
    Invoke (Find-Name 'Reset to Defaults')
    $selected = (Find-Id 'UiLanguage').GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()
    if ($selected[0].Current.Name -ne 'Follow System Language') { throw 'Global defaults failed.' }
    if ((Get-Content $file -Raw) -notmatch 'Language=zh-Hans') { throw 'Reset saved an unapplied draft.' }
    Close-Settings 'Close'; Stop-Fixture
    Start-Fixture; Open-Settings (Text @(0x8f93,0x5165,0x6cd5,0x8bbe,0x7f6e)); Global
    Capture 'global-simplified'
    Choose-Language (Text @(0x7e41,0x9ad4,0x4e2d,0x6587))
    Invoke (Find-Name (Text @(0x5e94,0x7528))); Wait-Ready
    Close-Settings (Text @(0x5173,0x95ed)); Stop-Fixture
    Start-Fixture; Open-Settings (Text @(0x8f38,0x5165,0x6cd5,0x8a2d,0x5b9a)); Global
    if (!(Find-Name (Text @(0x5168,0x57df,0x8a2d,0x5b9a)))) { throw 'Traditional translation missing.' }
    Capture 'global-traditional'
    Capture 'global-traditional-minimum' 720 480
    Choose-Language 'English'
    Invoke (Find-Name (Text @(0x5957,0x7528))); Wait-Ready
    Close-Settings (Text @(0x95dc,0x9589)); Stop-Fixture
    Start-Fixture; Open-Settings 'Input Method Settings'; Global
    Capture 'global-english-restored'
    Close-Settings 'Close'
    Write-Output "PASS: actual WinUI with isolated simulated Core; navigation placement, save/deferred language, draft cancel/discard, reset, simplified/traditional/English startup. Screenshots: $LogDirectory"
} finally {
    if ($settings -and !$settings.HasExited) { Stop-Process -Id $settings.Id; $settings.WaitForExit() }
    Stop-Fixture
    $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY = $previousDiagnosticDirectory
}
