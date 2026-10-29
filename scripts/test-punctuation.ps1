[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Prefix,
    [Parameter(Mandatory)][string]$TsfBuild,
    [ValidateSet('x64', 'x86')][string]$Architecture = 'x64',
    [string]$LogDirectory = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$Prefix = (Resolve-Path -LiteralPath $Prefix).Path
$TsfBuild = (Resolve-Path -LiteralPath $TsfBuild).Path
if (!$LogDirectory) { $LogDirectory = "$root/build/punctuation-test/$([guid]::NewGuid().ToString('N'))" }
New-Item -ItemType Directory -Force $LogDirectory | Out-Null
$LogDirectory = (Resolve-Path -LiteralPath $LogDirectory).Path
$core = "$Prefix/bin/Fcitx5.exe"
& $core --check-ui-test
if ($LASTEXITCODE) { throw 'This test requires a Core built with FCITX5_UI_TEST.' }
& "$TsfBuild/tests/ipc_probe.exe" --check-ui-test
if ($LASTEXITCODE) { throw 'This test requires TSF/probes built with FCITX5_UI_TEST.' }
$settingsFile = "$LogDirectory/windows.conf"
[IO.File]::WriteAllText("$LogDirectory/windows-ui.conf", "[General]`nVersion=2`nLanguage=en`nRevision=1`nDetailedLogging=false`n")
$oldSkipUser = $env:SKIP_FCITX_USER_PATH
$process = $null
function Start-TestCore {
    # Exclude user dictionaries/configuration and disable upstream mirror writes.
    # The explicit windows.conf remains writable and tests authoritative storage.
    $env:SKIP_FCITX_USER_PATH = '1'
    $script:process = Start-Process $core -WindowStyle Hidden -PassThru -WorkingDirectory "$Prefix/bin" `
        -ArgumentList @('--windows-settings-file', "`"$settingsFile`"") `
        -RedirectStandardOutput "$LogDirectory/core.stdout.log" -RedirectStandardError "$LogDirectory/core.stderr.log"
    $env:SKIP_FCITX_USER_PATH = $oldSkipUser
    for ($attempt = 0; $attempt -lt 60; ++$attempt) {
        if ($script:process.HasExited) { throw 'Isolated Core exited; inspect core.stderr.log.' }
        $previous = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & "$TsfBuild/tests/ipc_probe.exe" --ready 2>$null
            $ready = $LASTEXITCODE -eq 0
        } finally { $ErrorActionPreference = $previous }
        if ($ready) { return }
        Start-Sleep -Milliseconds 200
    }
    throw 'Isolated Core did not become ready; probes also need FCITX5_UI_TEST.'
}
function Stop-TestCore {
    if ($script:process -and !$script:process.HasExited) {
        Stop-Process -Id $script:process.Id
        $script:process.WaitForExit()
    }
    $script:process = $null
}
function Invoke-Probe([string]$Name, [string[]]$Arguments = @()) {
    & "$TsfBuild/tests/$Name.exe" @Arguments
    if ($LASTEXITCODE) { throw "$Name failed ($LASTEXITCODE)." }
}
try {
    Start-TestCore
    Invoke-Probe 'inputmethods_probe' @('--read-only-file', $settingsFile)
    Invoke-Probe 'ipc_probe'
    Invoke-Probe 'settings_probe'
    $dllName = if ($Architecture -eq 'x86') { 'fcitx5-x86.dll' } else { 'fcitx5-x86_64.dll' }
    Invoke-Probe 'tsf_probe' @("$TsfBuild/dll/$dllName")
    Invoke-Probe 'inputmethods_probe' @('--persistence-write')
    Stop-TestCore
    Start-TestCore
    Invoke-Probe 'inputmethods_probe' @('--persistence-read')
    Write-Output "PASS: punctuation modes, shared Pinyin/Shuangpin settings, storage failure, TSF routing/commits and restart persistence. Logs: $LogDirectory"
} finally {
    Stop-TestCore
    $env:SKIP_FCITX_USER_PATH = $oldSkipUser
}
