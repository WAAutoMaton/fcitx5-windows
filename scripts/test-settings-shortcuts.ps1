[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SettingsExe,
    [Parameter(Mandatory)][string]$FixtureExe,
    [string]$LogDirectory = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$LogDirectory) { $LogDirectory = "$root/build/shortcuts-ui/$([guid]::NewGuid().ToString('N'))" }
New-Item -ItemType Directory -Force $LogDirectory | Out-Null
$SettingsExe = (Resolve-Path $SettingsExe).Path
$FixtureExe = (Resolve-Path $FixtureExe).Path
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
function Find-Id([string]$Id) {
    $script:window.FindFirst([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty, $Id))
}
function Find-Name([string]$Name) {
    $script:window.FindFirst([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $Name))
}
function Wait-For([scriptblock]$Check, [string]$Failure) {
    for ($i = 0; $i -lt 100; ++$i) {
        if (& $Check) { return }
        Start-Sleep -Milliseconds 100
    }
    throw $Failure
}
function Invoke-Control($Control) {
    if (!$Control) { throw 'Control missing.' }
    Wait-For { $Control.Current.IsEnabled } 'Control did not become enabled.'
    $Control.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 300
}
function Value([int]$Index) {
    (Find-Id "GlobalShortcut/$Index").GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value.Replace("`r`n", "`n").Replace("`r", "`n")
}
function Set-Value([int]$Index, [string]$Text) {
    (Find-Id "GlobalShortcut/$Index").GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($Text)
}
function Open-Shortcuts {
    Wait-For { (Find-Id 'GlobalSettings').Current.IsEnabled } 'Global settings is busy.'
    (Find-Id 'GlobalSettings').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
    Wait-For { (Find-Id 'ShortcutOptionsTab').Current.IsEnabled } 'Global settings did not open.'
    (Find-Id 'ShortcutOptionsTab').GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Wait-For { Find-Id 'GlobalShortcut/0' } 'Shortcut editors missing.'
}
function Capture([string]$Name) {
    $bounds = $script:window.Current.BoundingRectangle
    $bitmap = [Drawing.Bitmap]::new([int]$bounds.Width, [int]$bounds.Height)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen([int]$bounds.X, [int]$bounds.Y, 0, 0, $bitmap.Size)
        $bitmap.Save("$LogDirectory/$Name.png", [Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}
$settings = $null; $fixture = $null
try {
    $file = [IO.Path]::GetFullPath("$LogDirectory/windows-ui.conf")
    [IO.File]::WriteAllText($file, "[General]`nVersion=2`nLanguage=en`nRevision=1`nDetailedLogging=false`n")
    $fixture = Start-Process $FixtureExe -ArgumentList "`"$file`"" -WindowStyle Hidden -PassThru
    $settings = Start-Process $SettingsExe -WindowStyle Hidden -PassThru
    Wait-For {
        $script:window = [Windows.Automation.AutomationElement]::RootElement.FindFirst([Windows.Automation.TreeScope]::Children,
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty, $settings.Id))
        $script:window -and (Find-Id 'method:pinyin')
    } 'Settings did not become ready.'
    Open-Shortcuts
    foreach ($i in 0..5) {
        $expected = if ($i -eq 0) { 'Control+space' } else { "Control+Alt+$i" }
        if ((Value $i) -cne $expected) { throw "Wrong shortcut default: $i" }
        Invoke-Control (Find-Id "GlobalShortcut/$i/record")
        if (!(Find-Id 'ShortcutPreview') -or (Find-Name 'Use shortcut').Current.IsEnabled) { throw 'Empty recorder state is invalid.' }
        Invoke-Control (Find-Name 'Cancel')
        if ((Value $i) -cne $expected) { throw 'Cancel changed the shortcut draft.' }
    }
    Set-Value 0 "Control+Shift+J`nAlt+<59>"
    Set-Value 1 'Control+Alt+F2'
    Set-Value 5 ''
    Invoke-Control (Find-Name 'Apply')
    (Find-Id 'method:pinyin').GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 400
    Open-Shortcuts
    if ((Value 0) -cne "Control+Shift+J`nAlt+<59>" -or (Value 1) -cne 'Control+Alt+F2' -or (Value 5) -cne '') { throw 'Shortcut save/reload failed.' }
    Set-Value 2 'Control+Shift+J'
    Invoke-Control (Find-Name 'Apply')
    if (!(Find-Name 'Invalid or duplicate shortcuts. Use the recorder to choose a different shortcut.')) { throw 'Duplicate shortcut was accepted.' }
    Set-Value 2 'InvalidKey'
    Invoke-Control (Find-Name 'Apply')
    if (!(Find-Name 'Invalid or duplicate shortcuts. Use the recorder to choose a different shortcut.')) { throw 'Invalid shortcut was accepted.' }
    Invoke-Control (Find-Name 'Reset to Defaults')
    foreach ($i in 0..5) {
        $expected = if ($i -eq 0) { 'Control+space' } else { "Control+Alt+$i" }
        if ((Value $i) -cne $expected) { throw 'Shortcut reset failed.' }
    }
    Invoke-Control (Find-Name 'Apply')
    Capture 'global-shortcuts'
    if ((Get-Content -Raw $file) -notmatch 'Version=3') { throw 'Global configuration did not migrate to v3.' }
    Invoke-Control (Find-Name 'Close')
    if (!$settings.WaitForExit(5000)) { throw 'Settings did not close.' }
    Write-Output "PASS: global shortcut defaults, recorder open/cancel, editing, save/reload, disable, validation, reset and v2 migration. Physical key injection is covered separately by test-settings-keycapture.ps1. Logs: $LogDirectory"
} catch {
    if ($script:window) {
        Capture 'failure'
        $script:window.FindAll([Windows.Automation.TreeScope]::Descendants, [Windows.Automation.Condition]::TrueCondition) |
            ForEach-Object { "$($_.Current.AutomationId) | $($_.Current.Name) | enabled=$($_.Current.IsEnabled)" } |
            Set-Content -Encoding UTF8 "$LogDirectory/ui-elements.txt"
    }
    throw
} finally {
    foreach ($process in @($settings, $fixture)) {
        if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit() }
    }
}
