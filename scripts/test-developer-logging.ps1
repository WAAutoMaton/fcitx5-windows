[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Prefix,
    [Parameter(Mandatory)][string]$TsfBuild,
    [string]$LogDirectory = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$Prefix = (Resolve-Path -LiteralPath $Prefix).Path
$TsfBuild = (Resolve-Path -LiteralPath $TsfBuild).Path
if (!$LogDirectory) { $LogDirectory = "$root/build/developer-logging-test/$([guid]::NewGuid().ToString('N'))" }
$LogDirectory = [IO.Path]::GetFullPath($LogDirectory)
foreach ($path in @($Prefix, $LogDirectory)) {
    if (!$path.StartsWith(($root + '\'), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Use an isolated test prefix and log directory inside the repository.'
    }
}
$corePath = "$Prefix/bin/Fcitx5.exe"
$probe = "$TsfBuild/tests/logging_probe.exe"
& $corePath --check-ui-test
if ($LASTEXITCODE -ne 0) { throw 'Core must be built with FCITX5_UI_TEST.' }
& $probe --check-ui-test
if ($LASTEXITCODE -ne 0) { throw 'TSF and probes must be built with FCITX5_UI_TEST.' }
New-Item -ItemType Directory -Force $LogDirectory | Out-Null
$before = @{}
foreach ($name in @('APPDATA', 'LOCALAPPDATA', 'SKIP_FCITX_USER_PATH', 'FCITX5_DIAGNOSTIC_TEST_DIRECTORY')) {
    $before[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$core = $null
function Invoke-Probe([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed ($LASTEXITCODE). Logs: $LogDirectory" }
}
function Start-TestCore {
    $script:core = Start-Process $corePath -WindowStyle Hidden -PassThru `
        -WorkingDirectory "$Prefix/bin" -ArgumentList @('--windows-settings-file',
        "`"$LogDirectory/windows.conf`"", '--windows-ui-settings-file', "`"$LogDirectory/windows-ui.conf`"") `
        -RedirectStandardOutput "$LogDirectory/core.stdout.log" -RedirectStandardError "$LogDirectory/core.stderr.log"
    for ($attempt = 0; $attempt -lt 50; ++$attempt) {
        Start-Sleep -Milliseconds 200
        if ($script:core.HasExited) { throw 'Isolated Core exited; its endpoint may be in use.' }
        $previousPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & "$TsfBuild/tests/ipc_probe.exe" --ready 2>$null
        } finally { $ErrorActionPreference = $previousPreference }
        if ($LASTEXITCODE -eq 0) { return }
    }
    throw 'Isolated Core did not become ready.'
}
function Stop-TestCore {
    if ($script:core -and !$script:core.HasExited) {
        $script:core.Kill()
        $script:core.WaitForExit()
    }
}
try {
    $env:APPDATA = $LogDirectory
    $env:LOCALAPPDATA = $LogDirectory
    $env:SKIP_FCITX_USER_PATH = '1'
    $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY = "$LogDirectory/diagnostics"
    Start-TestCore
    if (Test-Path -LiteralPath "$LogDirectory/diagnostics") { throw 'Disabled logging created a directory.' }
    Invoke-Probe $probe @('--enable')
    Invoke-Probe "$TsfBuild/tests/tsf_probe.exe" @("$TsfBuild/dll/fcitx5-x86_64.dll")
    Stop-TestCore
    Start-TestCore
    Invoke-Probe $probe @('--check-enabled')
    Invoke-Probe "$TsfBuild/tests/test_candidate_tsf.exe" @('--logging')
    Invoke-Probe $probe @('--disable')
    Start-Sleep -Milliseconds 500
    $logs = @(Get-ChildItem -LiteralPath "$LogDirectory/diagnostics" -File)
    if (!($logs | Where-Object Name -Like 'core-*.log*') -or
        !($logs | Where-Object Name -Like 'tsf-*.log*')) { throw 'Core or TSF log file missing.' }
    $events = @($logs | ForEach-Object {
        Get-Content -LiteralPath $_.FullName -Encoding utf8 | ForEach-Object { $_ | ConvertFrom-Json }
    })
    foreach ($event in $events) {
        foreach ($required in @('time', 'level', 'role', 'pid', 'tid', 'session', 'event', 'build')) {
            if ($null -eq $event.$required) { throw "Missing log field: $required" }
        }
        foreach ($forbidden in @('preedit', 'commit', 'text', 'payload', 'unicode', 'virtual_key', 'path', 'title')) {
            if ($event.PSObject.Properties.Name -contains $forbidden) { throw "Private log field: $forbidden" }
        }
    }
    $total = ($logs | Measure-Object Length -Sum).Sum
    Invoke-Probe $probe @('--disable')
    Start-Sleep -Milliseconds 500
    if ((Get-ChildItem -LiteralPath "$LogDirectory/diagnostics" -File | Measure-Object Length -Sum).Sum -ne $total) {
        throw 'Detailed events continued after disabling logging.'
    }
    Write-Output "PASS: isolated Core/TSF logging, persisted restart, Pinyin, reconnect, privacy and disable. Logs: $LogDirectory"
} finally {
    Stop-TestCore
    foreach ($name in $before.Keys) { [Environment]::SetEnvironmentVariable($name, $before[$name], 'Process') }
}
