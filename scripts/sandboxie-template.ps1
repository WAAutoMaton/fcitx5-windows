<#
.SYNOPSIS
Print or explicitly apply the current user's Fcitx5 Sandboxie compatibility rule.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [ValidatePattern('^[A-Za-z0-9_]{1,32}$')][string]$BoxName,
    [switch]$SessionOnly,
    [switch]$Apply,
    [string]$SbieIniPath = ''
)

$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
try {
    $sid = $identity.User.Value
} finally {
    $identity.Dispose()
}
$session = if ($SessionOnly) { [Diagnostics.Process]::GetCurrentProcess().SessionId } else { '*' }
$rule = "\Device\NamedPipe\fcitx5-windows-v2-$sid-$session"

function Invoke-SandboxieIni([string[]]$Arguments) {
    $outputFile = [IO.Path]::GetTempFileName()
    try {
        $process = Start-Process -FilePath $SbieIniPath -ArgumentList $Arguments `
            -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $outputFile
        if ($process.ExitCode -ne 0) {
            throw "SbieIni.exe failed ($($process.ExitCode)): $($Arguments -join ' ')"
        }
        return @(Get-Content -LiteralPath $outputFile)
    } finally {
        Remove-Item -LiteralPath $outputFile -ErrorAction SilentlyContinue
    }
}

if ($Apply) {
    if (!$BoxName) { throw '-Apply requires -BoxName.' }
    if (!$PSCmdlet.ShouldProcess("Sandboxie [$BoxName]", "Append OpenPipePath=$rule")) { return }
    if (!$SbieIniPath) {
        $candidates = @(
            "$env:ProgramFiles/Sandboxie-Plus/SbieIni.exe",
            "$env:ProgramFiles/Sandboxie/SbieIni.exe"
        )
        $SbieIniPath = $candidates | Where-Object {
            Test-Path -LiteralPath $_ -PathType Leaf
        } | Select-Object -First 1
    }
    if (!$SbieIniPath -or !(Test-Path -LiteralPath $SbieIniPath -PathType Leaf)) {
        throw 'SbieIni.exe was not found. Pass -SbieIniPath explicitly.'
    }
    $enabled = @(Invoke-SandboxieIni @('query', $BoxName, 'Enabled'))
    if ($enabled -notcontains 'y') { throw "Sandbox [$BoxName] does not exist or is disabled." }
    $existing = @(Invoke-SandboxieIni @('query', $BoxName, 'OpenPipePath'))
    if ($existing -contains $rule) {
        Write-Output "OpenPipePath already exists in [$BoxName]: $rule"
        return
    }
    Invoke-SandboxieIni @('append', $BoxName, 'OpenPipePath', $rule) | Out-Null
    $actual = @(Invoke-SandboxieIni @('query', $BoxName, 'OpenPipePath'))
    if ($actual -notcontains $rule) { throw "OpenPipePath was not saved in [$BoxName]." }
    Write-Output "Applied OpenPipePath to [$BoxName]: $rule"
    Write-Output 'Reopen applications in this sandbox to reload their access rules.'
} else {
    if ($BoxName) { Write-Output "[$BoxName]" }
    Write-Output "OpenPipePath=$rule"
}
