param([string]$TemplateScript = (Join-Path $PSScriptRoot '../../scripts/sandboxie-template.ps1'))
$ErrorActionPreference = 'Stop'
$script = $TemplateScript
$user = [Security.Principal.WindowsIdentity]::GetCurrent()
try { $sid = $user.User.Value } finally { $user.Dispose() }
$session = [Diagnostics.Process]::GetCurrentProcess().SessionId
$lines = @(& $script -BoxName TestBox -SessionOnly)
if ($lines.Count -ne 2 -or $lines[0] -ne '[TestBox]' -or
    $lines[1] -ne "OpenPipePath=\Device\NamedPipe\fcitx5-windows-v2-$sid-$session") {
    throw 'Session-specific Sandboxie template mismatch.'
}
$lines = @(& $script)
if ($lines.Count -ne 1 -or $lines[0] -ne "OpenPipePath=\Device\NamedPipe\fcitx5-windows-v2-$sid-*") {
    throw 'Current-user Sandboxie template mismatch.'
}
try {
    & $script -BoxName "TestBox`nOpenIpcPath=*"
    throw 'Invalid sandbox name was accepted.'
} catch [Management.Automation.ParameterBindingException] {}
try {
    & $script -Apply
    throw 'Apply without a box name was accepted.'
} catch [System.Management.Automation.RuntimeException] {
    if ($_.Exception.Message -notmatch '-Apply requires -BoxName') { throw }
}
$testState = @{ Calls = @(); Rule = ''; Fail = $false; IgnoreAppend = $false; Enabled = 'y' }
function Start-Process {
    param($FilePath, $ArgumentList, $WindowStyle, [switch]$Wait, [switch]$PassThru, $RedirectStandardOutput)
    if ($WindowStyle -ne 'Hidden' -or !$Wait -or !$PassThru) { throw 'SbieIni must run hidden and wait for exit.' }
    $testState.Calls += ,@($ArgumentList)
    if ($ArgumentList[0] -eq 'query') {
        $value = if ($ArgumentList[2] -eq 'Enabled') { $testState.Enabled } else { $testState.Rule }
        [IO.File]::WriteAllText($RedirectStandardOutput, $value)
    } elseif ($ArgumentList[0] -eq 'append') {
        if (!$testState.IgnoreAppend) { $testState.Rule = $ArgumentList[3] }
    } else { throw 'Unexpected mutation.' }
    return [pscustomobject]@{ ExitCode = $(if ($testState.Fail) { 5 } else { 0 }) }
}
& $script -BoxName TestBox -Apply -WhatIf | Out-Null
if ($testState.Calls.Count) { throw '-WhatIf ran SbieIni.' }
& $script -BoxName TestBox -Apply -SbieIniPath $PSCommandPath | Out-Null
if ($testState.Calls.Count -ne 4 -or $testState.Rule -ne "\Device\NamedPipe\fcitx5-windows-v2-$sid-*") {
    throw 'Apply did not query, append and verify the exact rule.'
}
& $script -BoxName TestBox -Apply -SbieIniPath $PSCommandPath | Out-Null
if ($testState.Calls.Count -ne 6) { throw 'Repeated Apply appended a duplicate rule.' }
foreach ($mode in @('Fail', 'IgnoreAppend', 'Disabled')) {
    $testState.Rule = ''
    $testState.Fail = $mode -eq 'Fail'
    $testState.IgnoreAppend = $mode -eq 'IgnoreAppend'
    $testState.Enabled = if ($mode -eq 'Disabled') { 'n' } else { 'y' }
    $failed = $false
    try { & $script -BoxName TestBox -Apply -SbieIniPath $PSCommandPath | Out-Null } catch { $failed = $true }
    if (!$failed) { throw "Apply reported success on $mode." }
}
Write-Output 'PASS: rule generation, WhatIf, idempotent Apply and failed/disabled configuration checks (mocked SbieIni).'
