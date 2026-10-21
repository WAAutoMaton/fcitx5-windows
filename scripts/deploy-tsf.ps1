[CmdletBinding()]
param([string]$Prefix = '', [string]$TsfBuild = '', [string]$TsfX86Build = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Prefix) { $Prefix = "$root/dist/pinyin" }
if (!$TsfBuild) { $TsfBuild = "$root/win32/build/pinyin-tsf" }
$destination = [IO.Path]::GetFullPath($Prefix)
$repository = [IO.Path]::GetFullPath($root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (!$destination.StartsWith($repository, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The isolated deployment prefix must be inside this repository.'
}
if (!(Test-Path -LiteralPath "$destination/bin/Fcitx5.exe")) {
    throw 'Deploy Core and its resources to the prefix first.'
}
if (!$TsfX86Build -and (Test-Path -LiteralPath "$destination/tsf/x86/fcitx5-x86.dll")) {
    throw 'Updating a dual-architecture deployment requires -TsfX86Build.'
}
$dll = (Resolve-Path -LiteralPath "$TsfBuild/dll/fcitx5-x86_64.dll").Path
$helper = (Resolve-Path -LiteralPath "$TsfBuild/setup/Fcitx5SetupHelper.exe").Path
$upgrade = (Resolve-Path -LiteralPath "$TsfBuild/setup/Fcitx5Upgrade.exe").Path
& "$root/win32/scripts/check-tsf-runtime.ps1" -DllPath $dll -Architecture x64
& "$root/win32/scripts/check-tsf-runtime.ps1" -DllPath $helper -Architecture x64
& "$root/win32/scripts/check-tsf-runtime.ps1" -DllPath $upgrade -Architecture x64
if ($TsfX86Build) {
    $x86Dll = (Resolve-Path -LiteralPath "$TsfX86Build/dll/fcitx5-x86.dll").Path
    & "$root/win32/scripts/check-tsf-runtime.ps1" -DllPath $x86Dll -Architecture x86
}
$sources = [ordered]@{
    'tsf/fcitx5-x86_64.dll' = $dll
    'setup/Fcitx5SetupHelper.exe' = $helper
    'setup/Fcitx5Upgrade.exe' = $upgrade
}
if ($TsfX86Build) {
    $sources['tsf/x86/fcitx5-x86.dll'] = $x86Dll
}
$updates = @($sources.GetEnumerator() | ForEach-Object {
    [pscustomobject]@{ Path = (Join-Path $destination $_.Key); Data = [IO.File]::ReadAllBytes($_.Value)
        Existed = $false; Stream = $null; Original = $null }
})
$opened = @(); $publishing = $false; $success = $false
try {
    # Keep every destination exclusive until both architectures are published.
    foreach ($update in $updates) {
        New-Item -ItemType Directory -Force (Split-Path $update.Path) | Out-Null
        $update.Existed = Test-Path -LiteralPath $update.Path
        $mode = if ($update.Existed) { [IO.FileMode]::Open } else { [IO.FileMode]::CreateNew }
        try { $update.Stream = [IO.File]::Open($update.Path, $mode, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
        catch { throw "Deployment file is in use or not writable: $($update.Path). Choose an isolated prefix." }
        $opened += $update
        $update.Original = New-Object IO.MemoryStream
        $update.Stream.CopyTo($update.Original)
    }
    $publishing = $true
    foreach ($update in $opened) {
        $update.Stream.Position = 0
        $update.Stream.SetLength(0)
        $update.Stream.Write($update.Data, 0, $update.Data.Length)
        $update.Stream.Flush($true)
    }
    $success = $true
} catch {
    $failure = $_
    $rollbackFailures = @()
    if ($publishing) {
        foreach ($update in $opened) {
            if ($update.Existed) {
                try {
                    $update.Stream.Position = 0
                    $update.Stream.SetLength(0)
                    $update.Original.Position = 0
                    $update.Original.CopyTo($update.Stream)
                    $update.Stream.Flush($true)
                } catch { $rollbackFailures += "$($update.Path): $($_.Exception.Message)" }
            }
        }
    }
    if ($rollbackFailures.Count) { throw "Deployment failed: $failure. Restore failures: $($rollbackFailures -join '; ')" }
    throw $failure
} finally {
    foreach ($update in $opened) {
        $update.Stream.Dispose()
        if ($update.Original) { $update.Original.Dispose() }
        if (!$success -and !$update.Existed) { Remove-Item -LiteralPath $update.Path -Force }
    }
}
if (Test-Path -LiteralPath "$TsfBuild/share/locale") {
    New-Item -ItemType Directory -Force "$destination/share/locale" | Out-Null
    Copy-Item "$TsfBuild/share/locale/*" -Destination "$destination/share/locale" -Recurse -Force
}
Write-Output "Deployed $destination/tsf/fcitx5-x86_64.dll. Registration was not changed."
