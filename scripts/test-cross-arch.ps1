[CmdletBinding()]
param([Parameter(Mandatory)][string]$TsfBuild, [Parameter(Mandatory)][string]$PeerTsfBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. "$root/win32/scripts/pe-machine.ps1"
$probes = @((Resolve-Path "$TsfBuild/tests/ipc_probe.exe").Path,
    (Resolve-Path "$PeerTsfBuild/tests/ipc_probe.exe").Path)
$machines = @($probes | ForEach-Object { Get-PeMachine $_ } | Sort-Object -Unique)
if ($machines.Count -ne 2 -or 0x14c -notin $machines -or 0x8664 -notin $machines) {
    throw 'Cross-architecture regression requires both x86 and x64 probes.'
}
foreach ($pair in @(@($TsfBuild, $PeerTsfBuild), @($PeerTsfBuild, $TsfBuild))) {
    $name = if ((Get-PeMachine "$($pair[1])/tests/ipc_probe.exe") -eq 0x14c) { 'fcitx5-x86.dll' } else { 'fcitx5-x86_64.dll' }
    & "$($pair[0])/tests/test_tsf_runtime_host.exe" "$($pair[1])/dll/$name" --wrong-arch
    if ($LASTEXITCODE -ne 0) { throw 'Wrong-architecture DLL rejection failed.' }
}
$temporary = Join-Path $root "build/cross-arch-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force $temporary | Out-Null
$events = @(); $children = @()
try {
    foreach ($index in 0..1) {
        $base = "Local\Fcitx5CrossArch-$([guid]::NewGuid().ToString('N'))"
        $ready = New-Object Threading.EventWaitHandle($false, [Threading.EventResetMode]::ManualReset, "$base-ready")
        $stop = New-Object Threading.EventWaitHandle($false, [Threading.EventResetMode]::ManualReset, "$base-stop")
        $events += $ready, $stop
        $children += Start-Process -FilePath $probes[$index] -ArgumentList @('--hold', "$base-ready", "$base-stop") `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput "$temporary/$index.out" -RedirectStandardError "$temporary/$index.err"
        if (!$ready.WaitOne(5000)) { throw "Client $index failed to connect; inspect $temporary." }
    }
    $servers = @(0..1 | ForEach-Object { (Get-Content "$temporary/$_.out" -Raw).Trim() })
    if ($servers[0] -notmatch '^server_pid=[1-9][0-9]*$' -or $servers[0] -ne $servers[1]) {
        throw 'Clients did not connect to the same Core PID.'
    }
    $null = $events[1].Set()
    if (!$children[0].WaitForExit(5000) -or $children[0].ExitCode -ne 0) { throw 'First client cleanup failed.' }
    Start-Sleep -Milliseconds 200
    if ($children[1].HasExited) { throw 'Second client was interrupted by the first disconnect.' }
    $null = $events[3].Set()
    if (!$children[1].WaitForExit(5000) -or $children[1].ExitCode -ne 0) { throw 'Second client cleanup failed.' }
    Write-Output "PASS: x86/x64 Pipe clients share $($servers[0]); disconnect preserves the peer."
} finally {
    foreach ($event in $events) { $null = $event.Set() }
    foreach ($child in $children) {
        if (!$child.HasExited -and !$child.WaitForExit(12000)) {
            Stop-Process -Id $child.Id
            $child.WaitForExit()
        }
        $child.Dispose()
    }
    foreach ($event in $events) { $event.Dispose() }
}
