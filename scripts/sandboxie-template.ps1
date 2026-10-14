<#
.SYNOPSIS
Print the current user's Fcitx5 Sandboxie compatibility rule without modifying configuration.
#>
[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9_]{1,32}$')][string]$BoxName,
    [switch]$SessionOnly
)

$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
try {
    $sid = $identity.User.Value
} finally {
    $identity.Dispose()
}
$session = if ($SessionOnly) { [Diagnostics.Process]::GetCurrentProcess().SessionId } else { '*' }
if ($BoxName) { Write-Output "[$BoxName]" }
Write-Output "OpenPipePath=\Device\NamedPipe\fcitx5-windows-v2-$sid-$session"
