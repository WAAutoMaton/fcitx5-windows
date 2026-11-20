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
foreach ($name in @('FCITX5_TEST_USER_ROOT', 'SKIP_FCITX_USER_PATH', 'SKIP_FCITX_SYSTEM_PATH', 'FCITX5_DIAGNOSTIC_TEST_DIRECTORY')) {
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
function Find-EngineLog([string]$Pattern) {
    Get-ChildItem -LiteralPath $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY -Filter 'fcitx5-*.log*' -File |
        Select-String -Pattern $Pattern -CaseSensitive
}
function Wait-EngineLog([string]$Pattern, [int]$PreviousCount = 0) {
    for ($attempt = 0; $attempt -lt 25; ++$attempt) {
        $count = @(Find-EngineLog $Pattern).Count
        if ($count -gt $PreviousCount) { return $count }
        Start-Sleep -Milliseconds 200
    }
    throw "Missing engine log matching $Pattern"
}
try {
    $env:FCITX5_TEST_USER_ROOT = $LogDirectory
    $env:SKIP_FCITX_USER_PATH = '0'
    $env:SKIP_FCITX_SYSTEM_PATH = '1'
    $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY = "$LogDirectory/diagnostics"
    $uiSettings = "$LogDirectory/windows-ui.conf"
    Set-Content -LiteralPath $uiSettings -Value 'Invalid test configuration' -Encoding Ascii
    New-Item -ItemType Directory -Force $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY | Out-Null
    $expired = "$env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY/fcitx5-20000101.log"
    Set-Content -LiteralPath $expired -Value 'Expired engine log' -Encoding Ascii
    (Get-Item -LiteralPath $expired).LastWriteTimeUtc = [DateTime]::UtcNow.AddDays(-8)
    Start-TestCore
    $startupInfo = 'Z Iinstance\.cpp:\d+\] Starting fcitx5 '
    $activationDebug = 'Z Dinstance\.cpp:\d+\] Instance::activateInputMethod'
    Wait-EngineLog 'Z Wmain\.cpp:\d+\] Cannot read UI language settings; using system language' | Out-Null
    Wait-EngineLog $startupInfo | Out-Null
    if (Find-EngineLog 'Z D[^:]+:\d+\]') {
        throw 'Engine debug log recorded with detailed logging disabled by default.'
    }
    if (Test-Path -LiteralPath $expired) { throw 'Expired engine log survived startup.' }
    if (Get-ChildItem -LiteralPath $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY -Filter 'core-*.log*' -File) {
        throw 'Core detailed log created with detailed logging disabled.'
    }
    [IO.File]::WriteAllText($uiSettings, "[General]`nVersion=1`nLanguage=en`nRevision=1`n")
    Invoke-Probe $probe @('--enable')
    Wait-EngineLog $activationDebug | Out-Null
    Stop-TestCore
    $previousDebug = @(Find-EngineLog $activationDebug).Count
    Start-TestCore
    Wait-EngineLog $activationDebug $previousDebug | Out-Null
    Invoke-Probe $probe @('--check-enabled')
    Invoke-Probe "$TsfBuild/tests/tsf_probe.exe" @("$TsfBuild/dll/fcitx5-x86_64.dll")
    Invoke-Probe "$TsfBuild/tests/test_candidate_tsf.exe" @('--logging')
    Invoke-Probe $probe @('--disable')
    Start-Sleep -Milliseconds 500
    $debugCount = @(Find-EngineLog 'Z D[^:]+:\d+\]').Count
    $logs = @(Get-ChildItem -LiteralPath "$LogDirectory/diagnostics" -File |
        Where-Object Name -NotLike 'fcitx5-*.log*')
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
    if (!($events | Where-Object { $_.event -eq 'fcitx_log' -and $_.level -eq 'debug' })) {
        throw 'Engine debug messages were not marked as debug in the Core detailed log.'
    }
    $total = ($logs | Measure-Object Length -Sum).Sum
    Invoke-Probe $probe @('--disable')
    Start-Sleep -Milliseconds 500
    if (@(Find-EngineLog 'Z D[^:]+:\d+\]').Count -ne $debugCount) {
        throw 'Engine debug messages continued after disabling detailed logging.'
    }
    if (Find-EngineLog 'Z Dinstance\.cpp:\d+\] KeyEvent:') {
        throw 'The engine key_trace category was enabled.'
    }
    if ((Get-ChildItem -LiteralPath "$LogDirectory/diagnostics" -File |
        Where-Object Name -NotLike 'fcitx5-*.log*' | Measure-Object Length -Sum).Sum -ne $total) {
        throw 'Detailed events continued after disabling logging.'
    }
    Write-Output "PASS: engine Info/Debug switching, retention, Core/TSF detailed logging, persisted restart, Pinyin, reconnect, privacy and disable. Logs: $LogDirectory"
} finally {
    Stop-TestCore
    foreach ($name in $before.Keys) { [Environment]::SetEnvironmentVariable($name, $before[$name], 'Process') }
}
