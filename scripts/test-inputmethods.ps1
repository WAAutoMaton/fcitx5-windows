[CmdletBinding()]
param([string]$Prefix = '', [string]$TsfBuild = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Prefix) { $Prefix = "$root/dist/inputmethods" }
if (!$TsfBuild) { $TsfBuild = "$root/build/inputmethods/tsf" }
if (Get-Process Fcitx5,Fcitx5Settings -ErrorAction SilentlyContinue) { throw 'Core or Settings is already running; isolated tests need a free service endpoint.' }
$log = "$root/build/inputmethods-test/$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force $log | Out-Null
$configuration = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Fcitx5/config/fcitx5'
$backups = @{}
foreach ($relative in @('conf/pinyin.conf', 'table/wbx.conf')) {
    $path = Join-Path $configuration $relative
    $backups[$path] = if (Test-Path -LiteralPath $path) { [IO.File]::ReadAllBytes($path) } else { $null }
}
$process = $null
function Start-TestCore {
    $script:process = Start-Process "$Prefix/bin/Fcitx5.exe" -WindowStyle Hidden -PassThru -WorkingDirectory "$Prefix/bin" `
        -ArgumentList @('--windows-settings-file', "`"$log/windows.conf`"") -RedirectStandardOutput "$log/core.stdout.log" -RedirectStandardError "$log/core.stderr.log"
    for ($attempt = 0; $attempt -lt 60; ++$attempt) {
        Start-Sleep -Milliseconds 200
        if ($script:process.HasExited) { throw "Core exited: $log/core.stderr.log" }
        $previous = $ErrorActionPreference
        try { $ErrorActionPreference = 'Continue'; & "$TsfBuild/tests/ipc_probe.exe" --ready 2>$null; $ready = $LASTEXITCODE -eq 0 }
        finally { $ErrorActionPreference = $previous }
        if ($ready) { return }
    }
    throw 'Core did not become ready.'
}
function Stop-TestCore {
    if ($script:process -and !$script:process.HasExited) { Stop-Process -Id $script:process.Id; $script:process.WaitForExit() }
    $script:process = $null
}
try {
    Start-TestCore
    & "$TsfBuild/tests/inputmethods_probe.exe"
    if ($LASTEXITCODE) { throw 'Input method regression failed.' }
    & "$TsfBuild/tests/ipc_probe.exe"
    if ($LASTEXITCODE) { throw 'Pinyin IPC regression failed.' }
    & "$TsfBuild/tests/settings_probe.exe"
    if ($LASTEXITCODE) { throw 'Legacy settings regression failed.' }
    & "$TsfBuild/tests/tsf_probe.exe" "$TsfBuild/dll/fcitx5-x86_64.dll"
    if ($LASTEXITCODE) { throw 'TSF regression failed.' }
    & "$TsfBuild/tests/inputmethods_probe.exe" --persistence-write
    if ($LASTEXITCODE) { throw 'Persistence setup failed.' }
    if ((Get-Content -Raw "$log/windows.conf") -notmatch '(?m)^PageSize=7') {
        throw 'Authoritative settings omitted a default value.'
    }
    Stop-TestCore
    Start-TestCore
    & "$TsfBuild/tests/inputmethods_probe.exe" --persistence-read
    if ($LASTEXITCODE) { throw 'Persistence check failed.' }
    & "$TsfBuild/tests/tsf_probe.exe" "$TsfBuild/dll/fcitx5-x86_64.dll" --cycles
    if ($LASTEXITCODE) { throw 'Multi-method TSF shortcut regression failed.' }
    Write-Output "Logs: $log"
} finally {
    Stop-TestCore
    foreach ($path in $backups.Keys) {
        if ($null -ne $backups[$path]) { [IO.File]::WriteAllBytes($path, $backups[$path]) }
        elseif (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
    }
}
