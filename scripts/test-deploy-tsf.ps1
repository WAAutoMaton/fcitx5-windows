[CmdletBinding()]
param([Parameter(Mandatory)][string]$TsfBuild, [Parameter(Mandatory)][string]$TsfX86Build)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$temporary = Join-Path $root "build/deploy-tsf-test-$([guid]::NewGuid().ToString('N'))"
$prefix = Join-Path $temporary 'prefix'
function Assert-Failure([scriptblock]$Action, [string]$Message) {
    $failure = $null
    try { & $Action | Out-Null } catch { $failure = $_.Exception.Message }
    if (!$failure -or $failure -notlike "*$Message*") { throw "Expected '$Message', got '$failure'." }
}
try {
    New-Item -ItemType Directory -Force "$prefix/bin" | Out-Null
    [IO.File]::WriteAllText("$prefix/bin/Fcitx5.exe", 'Deployment fixture; never execute.')
    $options = @{ Prefix = $prefix; TsfBuild = $TsfBuild; TsfX86Build = $TsfX86Build }
    & "$PSScriptRoot/deploy-tsf.ps1" @options | Out-Null
    foreach ($pair in @(@("$TsfBuild/dll/fcitx5-x86_64.dll", "$prefix/tsf/fcitx5-x86_64.dll"),
            @("$TsfX86Build/dll/fcitx5-x86.dll", "$prefix/tsf/x86/fcitx5-x86.dll"))) {
        if ((Get-FileHash $pair[0]).Hash -ne (Get-FileHash $pair[1]).Hash) { throw 'Deployment digest mismatch.' }
    }
    [IO.File]::WriteAllText("$prefix/tsf/fcitx5-x86_64.dll", 'Old x64 fixture')
    $locked = [IO.File]::Open("$prefix/tsf/x86/fcitx5-x86.dll", [IO.FileMode]::Open,
        [IO.FileAccess]::Read, [IO.FileShare]::None)
    try {
        Assert-Failure { & "$PSScriptRoot/deploy-tsf.ps1" @options } 'in use or not writable'
        if ([IO.File]::ReadAllText("$prefix/tsf/fcitx5-x86_64.dll") -ne 'Old x64 fixture') {
            throw 'A locked x86 DLL allowed partial publication of x64.'
        }
    } finally { $locked.Dispose() }
    Assert-Failure { & "$PSScriptRoot/deploy-tsf.ps1" -Prefix $prefix -TsfBuild $TsfBuild } 'requires -TsfX86Build'
    $wrong = Join-Path $temporary 'wrong/dll'
    New-Item -ItemType Directory -Force $wrong | Out-Null
    Copy-Item -LiteralPath "$TsfBuild/dll/fcitx5-x86_64.dll" -Destination "$wrong/fcitx5-x86.dll"
    Assert-Failure { & "$PSScriptRoot/deploy-tsf.ps1" -Prefix $prefix -TsfBuild $TsfBuild -TsfX86Build "$temporary/wrong" } 'PE machine mismatch'
    if ([IO.File]::ReadAllText("$prefix/tsf/fcitx5-x86_64.dll") -ne 'Old x64 fixture') {
        throw 'Machine mismatch changed the deployment.'
    }
    Write-Output 'PASS: dual-architecture deployment, digests, x86-only lock preserves x64, partial update and wrong-machine rejection (no registration or service startup).'
} finally {
    $resolved = [IO.Path]::GetFullPath($temporary)
    if (!$resolved.StartsWith(($root + '\build\'), [StringComparison]::OrdinalIgnoreCase)) { throw "Unexpected cleanup path: $resolved" }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
