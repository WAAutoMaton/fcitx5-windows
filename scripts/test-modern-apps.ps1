[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Prefix,
    [Parameter(Mandatory)][string]$TsfBuild,
    [Parameter(Mandatory)][string]$AppContainerProfile,
    [string]$LogDirectory = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$Prefix = (Resolve-Path -LiteralPath $Prefix).Path
$TsfBuild = (Resolve-Path -LiteralPath $TsfBuild).Path
if (!$Prefix.StartsWith(($root + '\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The test prefix must be inside the repository.'
}
if (!$LogDirectory) { $LogDirectory = "$root/build/modern-apps-test/$([guid]::NewGuid().ToString('N'))" }
$LogDirectory = [IO.Path]::GetFullPath($LogDirectory)
if (!$LogDirectory.StartsWith(($root + '\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The test logs must be inside the repository.'
}
$corePath = Join-Path $Prefix 'bin/Fcitx5.exe'
$probe = Join-Path $TsfBuild 'tests/modern_input_probe.exe'
& $corePath --check-ui-test
if ($LASTEXITCODE -ne 0) { throw 'Core must be built with FCITX5_UI_TEST; no service was started.' }
& $probe --check-ui-test
if ($LASTEXITCODE -ne 0) { throw 'TSF and probes must be built with FCITX5_UI_TEST.' }

function Invoke-Probe([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed ($LASTEXITCODE). Logs: $LogDirectory" }
}

New-Item -ItemType Directory -Force $LogDirectory | Out-Null
& "$PSScriptRoot/deploy-tsf.ps1" -Prefix $Prefix -TsfBuild $TsfBuild
Copy-Item -LiteralPath $probe -Destination "$Prefix/tsf/modern_input_probe.exe"
$before = @{}
foreach ($name in @('APPDATA', 'LOCALAPPDATA', 'SKIP_FCITX_USER_PATH')) {
    $before[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$core = $null
try {
    $env:APPDATA = $LogDirectory
    $env:LOCALAPPDATA = $LogDirectory
    $env:SKIP_FCITX_USER_PATH = '1'
    $core = Start-Process $corePath -WindowStyle Hidden -PassThru -WorkingDirectory "$Prefix/bin" `
        -ArgumentList @('--windows-settings-file', "`"$LogDirectory/windows.conf`"", '--windows-ui-settings-file', "`"$LogDirectory/ui.conf`"") `
        -RedirectStandardOutput "$LogDirectory/core.stdout.log" -RedirectStandardError "$LogDirectory/core.stderr.log"
    $ready = $false
    for ($attempt = 0; $attempt -lt 30; ++$attempt) {
        Start-Sleep -Milliseconds 200
        if ($core.HasExited) { throw "Isolated Core exited ($($core.ExitCode)); the test endpoint may be in use." }
        $previousPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & "$TsfBuild/tests/ipc_probe.exe" --ready 2>$null
        } finally { $ErrorActionPreference = $previousPreference }
        if ($LASTEXITCODE -eq 0) { $ready = $true; break }
    }
    if (!$ready) { throw 'Isolated Core did not become ready.' }
    Invoke-Probe "$Prefix/tsf/modern_input_probe.exe" @($AppContainerProfile)
    Invoke-Probe "$TsfBuild/tests/ipc_probe.exe" @()
    Invoke-Probe "$TsfBuild/tests/settings_probe.exe" @()
    Invoke-Probe "$TsfBuild/tests/tsf_probe.exe" @("$Prefix/tsf/fcitx5-x86_64.dll")
    Write-Output "PASS: AppContainer engine/TSF/UIElement behavior and desktop regressions. Logs: $LogDirectory"
} finally {
    if ($core -and !$core.HasExited) { $core.Kill(); $core.WaitForExit() }
    foreach ($name in $before.Keys) { [Environment]::SetEnvironmentVariable($name, $before[$name], 'Process') }
}
