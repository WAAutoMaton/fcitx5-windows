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
Write-Output 'PASS: exact session/current-user rules and invalid section rejection; no configuration writes.'
