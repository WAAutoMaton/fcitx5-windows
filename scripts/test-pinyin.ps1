[CmdletBinding()]
param([string]$Prefix = '', [string]$TsfBuild = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Prefix) { $Prefix = "$root/dist/pinyin" }
if (!$TsfBuild) { $TsfBuild = "$root/win32/build/pinyin-tsf" }
$core = (Resolve-Path "$Prefix/bin/Fcitx5.exe").Path
$probe = (Resolve-Path "$TsfBuild/tests/ipc_probe.exe").Path
$tsfProbe = (Resolve-Path "$TsfBuild/tests/tsf_probe.exe").Path
$dll = (Resolve-Path "$TsfBuild/dll/fcitx5-x86_64.dll").Path
$log = "$root/build/pinyin-test"
New-Item -ItemType Directory -Force $log | Out-Null
if (Get-Process Fcitx5 -ErrorAction SilentlyContinue) { throw 'Stop the running Core before this isolated test.' }
$process = Start-Process -FilePath $core -WindowStyle Hidden -PassThru -WorkingDirectory "$Prefix/bin" `
    -RedirectStandardOutput "$log/core.stdout.log" -RedirectStandardError "$log/core.stderr.log"
try {
    $connected = $false
    for ($attempt = 0; $attempt -lt 30; ++$attempt) {
        Start-Sleep -Milliseconds 200
        if ($process.HasExited) { throw "Core exited ($($process.ExitCode)); inspect $log/core.stderr.log" }
        $previous = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & $probe --ready 2>$null
            $ready = $LASTEXITCODE -eq 0
        } finally { $ErrorActionPreference = $previous }
        if ($ready) { $connected = $true; break }
    }
    if (!$connected) { throw 'Pinyin IPC integration failed.' }
    & $probe
    if ($LASTEXITCODE -ne 0) { throw 'Pinyin IPC regression failed.' }
    & $tsfProbe $dll
    if ($LASTEXITCODE -ne 0) { throw 'Pinyin TSF integration failed.' }
    & ctest --test-dir $TsfBuild --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }
} finally {
    if (!$process.HasExited) { Stop-Process -Id $process.Id }
}
