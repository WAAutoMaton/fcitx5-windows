[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Prefix,
    [ValidateSet('en', 'zh-Hans', 'zh-Hant')][string]$Language = 'zh-Hans',
    [string]$LogDirectory = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$Prefix = (Resolve-Path -LiteralPath $Prefix).Path
if (!$LogDirectory) { $LogDirectory = "$root/build/punctuation-ui/$([guid]::NewGuid().ToString('N'))" }
New-Item -ItemType Directory -Force $LogDirectory | Out-Null
$LogDirectory = (Resolve-Path -LiteralPath $LogDirectory).Path
& "$Prefix/bin/Fcitx5.exe" --check-ui-test
if ($LASTEXITCODE) { throw 'This test requires FCITX5_UI_TEST Core and Settings builds.' }
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
function Text([int[]]$Codes) { -join ([char[]]$Codes) }
$chinese = if ($Language -eq 'en') { 'Chinese punctuation' }
    elseif ($Language -eq 'zh-Hans') { Text @(0x4e2d,0x6587,0x6807,0x70b9) }
    else { Text @(0x4e2d,0x6587,0x6a19,0x9ede) }
$english = if ($Language -eq 'en') { 'English punctuation' }
    elseif ($Language -eq 'zh-Hans') { Text @(0x82f1,0x6587,0x6807,0x70b9) }
    else { Text @(0x82f1,0x6587,0x6a19,0x9ede) }
$apply = if ($Language -eq 'en') { 'Apply' }
    elseif ($Language -eq 'zh-Hans') { Text @(0x5e94,0x7528) }
    else { Text @(0x5957,0x7528) }
$coreProcess = $null; $settingsProcess = $null
$oldSkipUser = $env:SKIP_FCITX_USER_PATH
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
        if ($settingsProcess.HasExited -or $coreProcess.HasExited) { throw 'Test process exited.' }
        Start-Sleep -Milliseconds 100
    }
    throw $Failure
}
function Select-Method([string]$Id) {
    (Find-Id "method:$Id").GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 350
    Wait-For { (Find-Name $apply).Current.IsEnabled } 'Settings is busy.'
}
function Selected-Punctuation {
    $selection = (Find-Id 'WindowsPunctuation').GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()
    if ($selection.Count) { $selection[0].Current.Name }
}
function Select-Punctuation([string]$Name) {
    $combo = Find-Id 'WindowsPunctuation'
    $expand = $combo.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern)
    $expand.Expand()
    Wait-For { Find-Name $Name } 'Punctuation choice missing.'
    (Find-Name $Name).GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    $expand.Collapse()
    Start-Sleep -Milliseconds 200
}
function Apply {
    Wait-For { (Find-Name $apply).Current.IsEnabled } 'Apply button missing or busy.'
    (Find-Name $apply).GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 350
    Wait-For { (Find-Name $apply).Current.IsEnabled } 'Apply did not finish.'
}
try {
    [IO.File]::WriteAllText("$LogDirectory/windows-ui.conf", "[General]`nVersion=2`nLanguage=$Language`nRevision=1`nDetailedLogging=false`n")
    # Start with both pages available, without changing the user's input list.
    [IO.File]::WriteAllText("$LogDirectory/windows.conf", "Version=2`nScheme=Full`nProfile=Xiaohe`nRevision=1`n[InputMethods]`n0=keyboard-us`n1=pinyin`n2=shuangpin`n")
    $env:SKIP_FCITX_USER_PATH = '1'
    $coreProcess = Start-Process "$Prefix/bin/Fcitx5.exe" -WindowStyle Hidden -PassThru -WorkingDirectory "$Prefix/bin" `
        -ArgumentList @('--windows-settings-file', "`"$LogDirectory/windows.conf`"") `
        -RedirectStandardOutput "$LogDirectory/core.stdout.log" -RedirectStandardError "$LogDirectory/core.stderr.log"
    $env:SKIP_FCITX_USER_PATH = $oldSkipUser
    Start-Sleep -Milliseconds 1500
    if ($coreProcess.HasExited) { throw 'Isolated Core failed to start.' }
    $settingsProcess = Start-Process "$Prefix/settings/Fcitx5Settings.exe" -WindowStyle Hidden -PassThru
    Wait-For {
        $script:window = [Windows.Automation.AutomationElement]::RootElement.FindFirst([Windows.Automation.TreeScope]::Children,
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty, $settingsProcess.Id))
        $script:window -and (Find-Id 'method:pinyin') -and (Find-Name $apply).Current.IsEnabled
    } 'Settings did not become ready.'
    Select-Method 'pinyin'
    Wait-For { Find-Id 'WindowsPunctuation' } 'Punctuation editor missing.'
    if ((Selected-Punctuation) -cne $chinese) { throw 'Wrong punctuation default/translation.' }
    Select-Punctuation $english
    Apply
    Select-Method 'shuangpin'
    if ((Selected-Punctuation) -cne $english) { throw 'Shuangpin did not share the saved punctuation mode.' }
    Select-Punctuation $chinese
    Apply
    Select-Method 'pinyin'
    if ((Selected-Punctuation) -cne $chinese) { throw 'Pinyin did not reload the saved punctuation mode.' }
    if ((Get-Content -Raw "$LogDirectory/windows.conf") -notmatch '(?m)^WindowsPunctuation=Chinese') {
        throw 'Punctuation mode was not persisted.'
    }
    $bounds = $script:window.Current.BoundingRectangle
    $bitmap = [Drawing.Bitmap]::new([int]$bounds.Width, [int]$bounds.Height)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen([int]$bounds.X, [int]$bounds.Y, 0, 0, $bitmap.Size)
        $bitmap.Save("$LogDirectory/punctuation.png", [Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
    Write-Output "PASS: real Core/WinUI punctuation default, translations, choices, apply, shared Pinyin/Shuangpin state and persistence. Logs: $LogDirectory"
} finally {
    foreach ($process in @($settingsProcess, $coreProcess)) {
        if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit() }
    }
    $env:SKIP_FCITX_USER_PATH = $oldSkipUser
}
