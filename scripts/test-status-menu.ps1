[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Prefix,
    [Parameter(Mandatory)][string]$TsfBuild,
    [string]$PeerTsfBuild = '',
    [switch]$Rime
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$Prefix = (Resolve-Path -LiteralPath $Prefix).Path
$TsfBuild = (Resolve-Path -LiteralPath $TsfBuild).Path
$corePath = Join-Path $Prefix 'bin/Fcitx5.exe'
$probe = Join-Path $TsfBuild 'tests/statusmenu_probe.exe'
& $corePath --check-ui-test
if ($LASTEXITCODE) { throw 'Use an isolated Core built with FCITX5_UI_TEST.' }
& $probe --check-ui-test
if ($LASTEXITCODE) { throw 'Use probes built with FCITX5_UI_TEST.' }
$run = Join-Path $root "build/status-menu-tests/$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force $run | Out-Null
$variables = @('FCITX5_TEST_USER_ROOT', 'SKIP_FCITX_USER_PATH', 'SKIP_FCITX_SYSTEM_PATH', 'FCITX5_DIAGNOSTIC_TEST_DIRECTORY')
$before = @{}
foreach ($name in $variables) { $before[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$process = $null
try {
    $env:FCITX5_TEST_USER_ROOT = $run
    $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY = Join-Path $run 'diagnostics'
    $env:SKIP_FCITX_USER_PATH = '0'
    $env:SKIP_FCITX_SYSTEM_PATH = '1'
    [IO.File]::WriteAllText("$run/ui.conf", "[General]`nVersion=1`nLanguage=en`nRevision=1`n", [Text.UTF8Encoding]::new($false))
    if ($Rime) {
        $data = Join-Path $Prefix 'plugins/fcitx5-rime/share/rime-data'
        foreach ($file in @('default.yaml', 'luna_pinyin.schema.yaml', 'luna_pinyin_simp.schema.yaml', 'luna_pinyin.dict.yaml')) {
            if (!(Test-Path -LiteralPath "$data/$file")) { throw "Missing Rime fixture: $file" }
        }
        $user = Join-Path $run 'roaming/fcitx5/rime'
        New-Item -ItemType Directory -Force $user | Out-Null
        [IO.File]::WriteAllText("$user/default.custom.yaml", "patch:`n  schema_list:`n    - schema: luna_pinyin`n    - schema: luna_pinyin_simp`n", [Text.UTF8Encoding]::new($false))
    }
    $process = Start-Process $corePath -WindowStyle Hidden -PassThru -WorkingDirectory "$Prefix/bin" `
        -ArgumentList @('--windows-settings-file', "`"$run/windows.conf`"", '--windows-ui-settings-file', "`"$run/ui.conf`"") `
        -RedirectStandardOutput "$run/core.stdout.log" -RedirectStandardError "$run/core.stderr.log"
    $ready = $false
    for ($attempt = 0; $attempt -lt 60; ++$attempt) {
        Start-Sleep -Milliseconds 200
        if ($process.HasExited) { throw "Isolated Core exited: $run/core.stderr.log" }
        $previous = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & $probe --ready 2>$null
            $connected = $LASTEXITCODE -eq 0
        } finally { $ErrorActionPreference = $previous }
        if ($connected) { $ready = $true; break }
    }
    if (!$ready) { throw 'Isolated Core did not become ready.' }
    $arguments = @()
    if ($Rime) { $arguments += '--rime' }
    & $probe @arguments
    if ($LASTEXITCODE) { throw "Status menu probe failed: $run/core.stderr.log" }
    if ($PeerTsfBuild) {
        & "$PeerTsfBuild/tests/statusmenu_probe.exe" --check-ui-test
        if ($LASTEXITCODE) { throw 'Use peer probes built with FCITX5_UI_TEST.' }
        & "$PeerTsfBuild/tests/statusmenu_probe.exe" @arguments
        if ($LASTEXITCODE) { throw 'Peer architecture status menu probe failed.' }
        & "$PSScriptRoot/test-cross-arch.ps1" -TsfBuild $TsfBuild -PeerTsfBuild $PeerTsfBuild
    }
    if ($Rime -and !(Test-Path -LiteralPath "$run/roaming/fcitx5/rime/build/luna_pinyin.schema.yaml")) {
        throw 'Rime did not deploy under the isolated user root.'
    }
    Write-Output "PASS: isolated status menu probes. Logs: $run"
} finally {
    if ($process -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name, $before[$name], 'Process') }
}
