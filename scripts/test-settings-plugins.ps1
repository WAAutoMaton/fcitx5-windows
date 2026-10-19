[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SettingsExe,
    [Parameter(Mandatory)][string]$FixtureExe,
    [string]$LogDirectory = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$LogDirectory) { $LogDirectory = "$root/build/plugins-ui/$([guid]::NewGuid().ToString('N'))" }
New-Item -ItemType Directory -Force $LogDirectory | Out-Null
$SettingsExe = (Resolve-Path -LiteralPath $SettingsExe).Path
$FixtureExe = (Resolve-Path -LiteralPath $FixtureExe).Path
$probe = Start-Process $SettingsExe -ArgumentList '--check-settings-diagnostics-test' -WindowStyle Hidden -PassThru
try {
    if (!$probe.WaitForExit(10000) -or $probe.ExitCode -ne 0) { throw 'Settings must be built with -UiTest to use the isolated test endpoint.' }
} finally {
    if (!$probe.HasExited) { Stop-Process -Id $probe.Id; $probe.WaitForExit() }
    $probe.Dispose()
}
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FcitxPluginCapture {
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
}
'@
$fixture = $null
$settings = $null
function Find-Id([string]$Id) {
    $script:window.FindFirst([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty, $Id))
}
function Find-Name([string]$Name) {
    $script:window.FindFirst([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $Name))
}
function Wait-For([scriptblock]$Condition, [string]$Failure) {
    for ($attempt = 0; $attempt -lt 100; ++$attempt) {
        if (& $Condition) { return }
        if ($settings.HasExited) { throw "Settings exited: $($settings.ExitCode)" }
        Start-Sleep -Milliseconds 100
    }
    throw $Failure
}
function Capture([string]$Name, [int]$Width, [int]$Height) {
    $handle = [IntPtr]$script:window.Current.NativeWindowHandle
    $dpi = [FcitxPluginCapture]::GetDpiForWindow($handle)
    [FcitxPluginCapture]::SetWindowPos($handle, [IntPtr](-1), 0, 0, [int]($Width * $dpi / 96), [int]($Height * $dpi / 96), 0x40) | Out-Null
    Start-Sleep -Milliseconds 500
    $bounds = $script:window.Current.BoundingRectangle
    $bitmap = [Drawing.Bitmap]::new([int]$bounds.Width, [int]$bounds.Height)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen([int]$bounds.X, [int]$bounds.Y, 0, 0, $bitmap.Size)
        $bitmap.Save("$LogDirectory/$Name.png", [Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
        [FcitxPluginCapture]::SetWindowPos($handle, [IntPtr](-2), 0, 0, 0, 0, 0x13) | Out-Null
    }
}
try {
    $file = [IO.Path]::GetFullPath("$LogDirectory/windows-ui.conf")
    [IO.File]::WriteAllText($file, "[General]`nVersion=1`nLanguage=en`nRevision=1`n")
    $fixture = Start-Process $FixtureExe -ArgumentList "`"$file`"" -WindowStyle Hidden -PassThru
    $settings = Start-Process $SettingsExe -WindowStyle Hidden -PassThru
    Wait-For {
        $script:window = [Windows.Automation.AutomationElement]::RootElement.FindFirst([Windows.Automation.TreeScope]::Children,
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty, $settings.Id))
        $script:window -and (Find-Id 'method:pinyin')
    } 'Settings did not become ready.'
    (Find-Id 'GlobalSettings').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
    Wait-For { Find-Id 'UiLanguage' } 'Global settings did not open.'
    (Find-Id 'PluginOptions').GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Wait-For { Find-Name 'Loaded plugin' } 'Plugin list did not populate.'
    foreach ($text in @('Loaded', 'Not loaded', 'Disabled', 'On-demand plugin', 'Disabled plugin', 'loaded | 1.2.3 | test-library', 'Plugin description')) {
        if (!(Find-Name $text)) { throw "Missing plugin detail: $text" }
    }
    if ((Find-Id 'PluginDirectory').Current.Name -ne 'C:/isolated-test/plugins') { throw 'Plugin directory missing.' }
    if (!(Find-Id 'PluginFolder').Current.IsEnabled) { throw 'Folder button is disabled.' }
    Capture 'plugins' 980 720
    Capture 'plugins-minimum' 720 480
    $shell = New-Object -ComObject Shell.Application
    $previous = @($shell.Windows() | ForEach-Object { $_.HWND })
    $personal = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Fcitx5/plugins'
    (Find-Id 'PluginFolder').GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    try {
        Wait-For {
            $script:folderWindow = @($shell.Windows() | Where-Object {
                try { $_.Document.Folder.Self.Path.TrimEnd('\') -eq $personal.Replace('/', '\').TrimEnd('\') } catch { $false }
            }) | Select-Object -First 1
            $script:folderWindow
        } 'Personal plugin directory did not open.'
        if (!(Test-Path -LiteralPath $personal -PathType Container)) { throw 'Personal plugin directory is missing.' }
    } finally {
        if ($script:folderWindow -and $script:folderWindow.HWND -notin $previous) { $script:folderWindow.Quit() }
    }
    Stop-Process -Id $fixture.Id
    $fixture.WaitForExit()
    Wait-For { (Find-Id 'PluginStatus').Current.Name -eq 'Cannot connect to Core.' } 'Connection error was not displayed.'
    if (Find-Name 'Loaded plugin') { throw 'Disconnected page kept stale plugin data.' }
    $fixture = Start-Process $FixtureExe -ArgumentList @("`"$file`"", '--empty-plugins') -WindowStyle Hidden -PassThru
    Wait-For { (Find-Id 'PluginStatus').Current.Name -eq 'No plugins' } 'Empty plugin list was not displayed after reconnect.'
    Write-Output "PASS: plugin metadata, statuses, folder button, empty list, disconnect/reconnect and screenshots. Logs: $LogDirectory"
} finally {
    foreach ($process in @($settings, $fixture)) {
        if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit() }
    }
}
