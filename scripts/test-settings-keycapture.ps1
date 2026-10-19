[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SettingsExe,
    [Parameter(Mandatory)][string]$FixtureExe,
    [string]$LogDirectory = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$LogDirectory) { $LogDirectory = "$root/build/keycapture-ui/$([guid]::NewGuid().ToString('N'))" }
New-Item -ItemType Directory -Force $LogDirectory | Out-Null
$SettingsExe = (Resolve-Path $SettingsExe).Path
$FixtureExe = (Resolve-Path $FixtureExe).Path
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing, System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FcitxKeyCaptureInput {
    [StructLayout(LayoutKind.Sequential)] public struct Keyboard {
        public ushort Key, Scan; public uint Flags, Time; public UIntPtr Extra;
    }
    [StructLayout(LayoutKind.Explicit, Size = 40)] public struct Input {
        [FieldOffset(0)] public uint Type;
        [FieldOffset(8)] public Keyboard Keyboard;
    }
    [DllImport("user32.dll")] static extern uint SendInput(uint count, Input[] inputs, int size);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint key, uint type);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    public static void Chord(params ushort[] keys) {
        var inputs = new Input[keys.Length * 2];
        for (int i = 0; i < keys.Length; ++i) {
            var down = new Input { Type = 1, Keyboard = new Keyboard {
                Scan = (ushort)MapVirtualKey(keys[i], 0), Flags = 8 } };
            if ((keys[i] >= 0x21 && keys[i] <= 0x2e) || keys[i] == 0xa3 || keys[i] == 0xa5)
                down.Keyboard.Flags |= 1;
            inputs[i] = down;
            down.Keyboard.Flags |= 2;
            inputs[inputs.Length - i - 1] = down;
        }
        if (SendInput((uint)inputs.Length, inputs, Marshal.SizeOf(typeof(Input))) != inputs.Length)
            throw new Exception("SendInput failed.");
    }
}
'@
$fixture = $null; $settings = $null
$script:shortcutId = 'PrevPage'
function Find-Id([string]$Id) {
    $script:window.FindFirst([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty, $Id))
}
function Find-Name([string]$Name) {
    $script:window.FindFirst([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $Name))
}
function Invoke-Control($Control) {
    if (!$Control) { throw 'Control missing.' }
    $Control.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 250
}
function Value($Control) {
    if (!$Control) { throw 'Value control missing.' }
    $Control.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value.Replace("`r`n", "`n").Replace("`r", "`n")
}
function Set-Value([string]$Text) {
    (Find-Id $script:shortcutId).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($Text)
}
function Select-Method([string]$Id) {
    (Find-Id "method:$Id").GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 500
}
function Open-Recorder {
    Invoke-Control (Find-Id "$script:shortcutId/record")
    $preview = Find-Id 'ShortcutPreview'
    if (!$preview) { throw 'Recorder did not open.' }
    if ((Find-Name 'Use shortcut').Current.IsEnabled) { throw 'Empty recorder accepted a shortcut.' }
    [FcitxKeyCaptureInput]::SetForegroundWindow([IntPtr]$script:window.Current.NativeWindowHandle) | Out-Null
    $preview.SetFocus()
    Start-Sleep -Milliseconds 100
    return $preview
}
function Record([uint16[]]$Keys, [string]$Expected, [bool]$Accept = $true) {
    $preview = Open-Recorder
    [FcitxKeyCaptureInput]::Chord($Keys)
    Start-Sleep -Milliseconds 200
    if ((Value $preview) -cne $Expected) { throw "Expected $Expected, recorded $(Value $preview)." }
    Invoke-Control (Find-Name $(if ($Accept) { 'Use shortcut' } else { 'Cancel' }))
    if (Find-Id 'ShortcutPreview') { throw 'Recorder did not close.' }
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
try {
    $file = [IO.Path]::GetFullPath("$LogDirectory/windows-ui.conf")
    [IO.File]::WriteAllText($file, "[General]`nVersion=1`nLanguage=en`nRevision=1`n")
    $fixture = Start-Process $FixtureExe -ArgumentList "`"$file`"" -WindowStyle Hidden -PassThru
    $settings = Start-Process $SettingsExe -WindowStyle Hidden -PassThru
    for ($i = 0; $i -lt 100; ++$i) {
        Start-Sleep -Milliseconds 100
        $script:window = [Windows.Automation.AutomationElement]::RootElement.FindFirst([Windows.Automation.TreeScope]::Children,
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty, $settings.Id))
        if ($script:window -and (Find-Id 'method:pinyin')) { break }
        if ($settings.HasExited) { throw "Settings exited: $($settings.ExitCode)" }
    }
    if (!$script:window) { throw 'Settings window missing.' }
    [FcitxKeyCaptureInput]::ShowWindow([IntPtr]$script:window.Current.NativeWindowHandle, 9) | Out-Null
    Select-Method 'pinyin'
    [FcitxKeyCaptureInput]::SetForegroundWindow([IntPtr]$script:window.Current.NativeWindowHandle) | Out-Null
    (Find-Id 'PrevPage').SetFocus()
    [FcitxKeyCaptureInput]::Chord(@(0xa2, 0x23))
    Start-Sleep -Milliseconds 100
    Record @(0xa2, 0x20) 'Control+space'
    if ((Value (Find-Id 'PrevPage')) -ne "Up`nminus`nControl+space") { throw "Recording did not append to the key list: $(Value (Find-Id 'PrevPage'))" }
    Invoke-Control (Find-Name 'Apply')
    Select-Method 'keyboard-us'; Select-Method 'pinyin'
    if ((Value (Find-Id 'PrevPage')) -ne "Up`nminus`nControl+space") { throw 'Recorded list did not survive apply/reload.' }
    $before = Value (Find-Id 'PrevPage')
    Record @(0xa2, 0xa0, 0x41) 'Control+Shift+A' $false
    if ((Value (Find-Id 'PrevPage')) -ne $before) { throw 'Cancel changed the key list.' }
    foreach ($case in @(
        @{ Keys = @(0xa2, 0xa0, 0x41); Value = 'Control+Shift+A' },
        @{ Keys = @(0xa1); Value = 'Shift_R' },
        @{ Keys = @(0x09); Value = 'Tab' },
        @{ Keys = @(0x0d); Value = 'Return' },
        @{ Keys = @(0x1b); Value = 'Escape' },
        @{ Keys = @(0x70); Value = '<59>' },
        @{ Keys = @(0xa0, 0xbb); Value = 'plus' }
    )) {
        Set-Value ''
        Record $case.Keys $case.Value
        if ((Value (Find-Id 'PrevPage')) -cne $case.Value) { throw 'Recording result mismatch.' }
    }
    Set-Value 'Up'
    (Find-Id 'PrevPage').SetFocus()
    [FcitxKeyCaptureInput]::Chord(@(0xa2, 0x41))
    Start-Sleep -Milliseconds 100
    Record @(0x28) 'Down'
    if ((Value (Find-Id 'PrevPage')) -ne 'Down') { throw 'Selected shortcut was not replaced.' }
    $preview = Open-Recorder
    [FcitxKeyCaptureInput]::Chord(@(0xa2, 0xa0, 0x41))
    Start-Sleep -Milliseconds 200
    Capture 'recorder'
    $other = [Windows.Forms.Form]::new()
    $otherText = [Windows.Forms.TextBox]::new()
    $other.Controls.Add($otherText)
    try {
        $other.Show()
        [FcitxKeyCaptureInput]::SetForegroundWindow($other.Handle) | Out-Null
        $otherText.Focus() | Out-Null
        [Windows.Forms.Application]::DoEvents()
        [FcitxKeyCaptureInput]::Chord(@(0x42))
        Start-Sleep -Milliseconds 150
        [Windows.Forms.Application]::DoEvents()
        if ($otherText.Text -cne 'b') { throw 'Recorder intercepted another window.' }
        if ((Value $preview) -cne 'Control+Shift+A') { throw 'Recorder changed in background.' }
        [FcitxKeyCaptureInput]::SetForegroundWindow([IntPtr]$script:window.Current.NativeWindowHandle) | Out-Null
        $preview.SetFocus()
        Start-Sleep -Milliseconds 150
        [FcitxKeyCaptureInput]::Chord(@(0xa2, 0x41))
        Start-Sleep -Milliseconds 150
        if ((Value $preview) -cne 'Control+A') { throw 'Recorder failed to resume after activation.' }
    } finally { $other.Dispose() }
    Invoke-Control (Find-Name 'Cancel')
    Capture 'settings'
    Invoke-Control (Find-Name 'Apply')
    (Find-Id 'GlobalSettings').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
    Start-Sleep -Milliseconds 500
    (Find-Id 'ShortcutOptionsTab').GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    foreach ($i in 0..5) {
        $expected = if ($i -eq 0) { 'Control+space' } else { "Control+Alt+$i" }
        if ((Value (Find-Id "GlobalShortcut/$i")) -cne $expected) { throw "Wrong global shortcut default: $i" }
    }
    $script:shortcutId = 'GlobalShortcut/0'
    Set-Value ''
    Record @(0xa2, 0xa0, 0x4a) 'Control+Shift+J'
    $script:shortcutId = 'GlobalShortcut/1'
    Set-Value ''
    Record @(0xa4, 0x70) 'Alt+<59>'
    $script:shortcutId = 'GlobalShortcut/5'
    Set-Value ''
    Invoke-Control (Find-Name 'Apply')
    Select-Method 'pinyin'
    (Find-Id 'GlobalSettings').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
    Start-Sleep -Milliseconds 500
    (Find-Id 'ShortcutOptionsTab').GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    if ((Value (Find-Id 'GlobalShortcut/0')) -cne 'Control+Shift+J' -or
        (Value (Find-Id 'GlobalShortcut/1')) -cne 'Alt+<59>' -or
        (Value (Find-Id 'GlobalShortcut/5')) -cne '') { throw 'Global shortcut save/reload failed.' }
    Record @(0xa2, 0xa0, 0x41) 'Control+Shift+A' $false
    if ((Value (Find-Id 'GlobalShortcut/5')) -cne '') { throw 'Global recorder cancel changed the draft.' }
    $script:shortcutId = 'GlobalShortcut/2'
    Set-Value 'Control+Shift+J'
    Invoke-Control (Find-Name 'Apply')
    if (!(Find-Name 'Invalid or duplicate shortcuts. Use the recorder to choose a different shortcut.')) { throw 'Duplicate global shortcut was accepted.' }
    Invoke-Control (Find-Name 'Reset to Defaults')
    foreach ($i in 0..5) {
        $expected = if ($i -eq 0) { 'Control+space' } else { "Control+Alt+$i" }
        if ((Value (Find-Id "GlobalShortcut/$i")) -cne $expected) { throw 'Global shortcut reset failed.' }
    }
    Invoke-Control (Find-Name 'Apply')
    Capture 'global-shortcuts'
    Invoke-Control (Find-Name 'Close')
    if (!$settings.WaitForExit(5000)) { throw 'Settings did not close.' }
    Write-Output "PASS: global shortcuts, defaults, duplicate validation, reset, recording, modifiers, special keys, append, replacement, cancel, apply/reload, focus isolation, screenshots. Logs: $LogDirectory"
} catch {
    if ($script:window) { Capture 'failure' }
    throw
} finally {
    foreach ($process in @($settings, $fixture)) {
        if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit() }
    }
}
