<#
.SYNOPSIS
Build every additional plugin in plugins/ against an installed Core/libime SDK.
.DESCRIPTION
chinese-addons is already built by build-pinyin.ps1. Other plugins use the
generic Windows addon toolchain and are deployed in independent directories.
#>
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Low')]
param(
    [Parameter(Mandatory)][string]$CorePrefix,
    [Parameter(Mandatory)][string]$Prefix,
    [string]$BuildRoot = '',
    [string]$MSYS2Root = 'C:/msys64',
    [string]$DependencyPrefix = '',
    [ValidateRange(1, 256)][int]$Jobs = 6
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$BuildRoot) { $BuildRoot = "$root/build/plugins" }
if (!$DependencyPrefix -and (Test-Path -LiteralPath "$root/build/deps/clang64")) {
    $DependencyPrefix = "$root/build/deps/clang64"
}
foreach ($name in @('CorePrefix', 'Prefix', 'BuildRoot', 'MSYS2Root', 'DependencyPrefix')) {
    $value = Get-Variable -Name $name -ValueOnly
    if ($value) {
        Set-Variable -Name $name -Value ($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($value).Replace('\', '/')) -WhatIf:$false
    }
}
if (!$PSCmdlet.ShouldProcess($Prefix, "Build all additional plugins using SDK '$CorePrefix'")) { return }
foreach ($package in @('Fcitx5Core', 'LibIMECore')) {
    if (!(Test-Path -LiteralPath "$CorePrefix/lib/cmake/$package/${package}Config.cmake")) {
        throw "CorePrefix must contain the matching installed Core/libime SDK: $package"
    }
}

function Invoke-PluginCommand([string]$Executable, [string[]]$Arguments) {
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments
        $status = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    if ($status -ne 0) { throw "$Executable failed ($status): $($Arguments -join ' ')" }
}

$oldPath = $env:PATH
$oldRoot = $env:MSYS2_ROOT
$oldPkgConfig = $env:PKG_CONFIG_PATH
try {
    $sysroot = "$MSYS2Root/clang64"
    $env:MSYS2_ROOT = $MSYS2Root
    $env:PKG_CONFIG_PATH = "$DependencyPrefix/lib/pkgconfig;$sysroot/lib/pkgconfig"
    $runtimeDirectories = @("$CorePrefix/bin", "$sysroot/bin")
    if ($DependencyPrefix) { $runtimeDirectories = @("$DependencyPrefix/bin") + $runtimeDirectories }
    foreach ($plugin in Get-ChildItem -LiteralPath "$root/plugins" -Directory | Sort-Object Name) {
        if ($plugin.Name -eq 'chinese-addons') { continue }
        if (!(Test-Path -LiteralPath "$($plugin.FullName)/CMakeLists.txt")) {
            throw "Plugin is not initialized or has no CMakeLists.txt: $($plugin.Name)"
        }
        $build = "$BuildRoot/$($plugin.Name)"
        $stage = "$build/prefix"
        $env:PATH = "$CorePrefix/bin;$build/bin;" + ($runtimeDirectories -join ';') + ";$MSYS2Root/usr/bin;$oldPath"
        Invoke-PluginCommand 'cmake' @('-S', $plugin.FullName, '-B', $build, '-G', 'Ninja',
            "-DCMAKE_TOOLCHAIN_FILE=$root/cmake/windows-addon-toolchain.cmake",
            "-DFCITX5_WINDOWS_SDK=$CorePrefix", "-DWINDOWS_DEPENDENCY_PREFIX=$DependencyPrefix",
            '-DARCH=AMD64', '-DCMAKE_BUILD_TYPE=Release', '-DENABLE_TEST=OFF',
            "-DCMAKE_INSTALL_PREFIX=$stage", '-DCMAKE_INSTALL_LIBDIR=lib',
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$build/bin", "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$build/bin",
            "-DCMAKE_C_COMPILER=$sysroot/bin/clang.exe", "-DCMAKE_CXX_COMPILER=$sysroot/bin/clang++.exe",
            "-DCMAKE_MAKE_PROGRAM=$sysroot/bin/ninja.exe", '-DCMAKE_CXX_FLAGS=-fexperimental-library')
        Invoke-PluginCommand 'cmake' @('--build', $build, '-j', "$Jobs")
        Invoke-PluginCommand 'cmake' @('--install', $build)
        $destination = "$Prefix/plugins/$($plugin.Name)"
        New-Item -ItemType Directory -Force $destination | Out-Null
        foreach ($directory in @('bin', 'lib', 'share', 'licenses')) {
            if (Test-Path -LiteralPath "$stage/$directory") {
                Copy-Item -LiteralPath "$stage/$directory" -Destination $destination -Recurse -Force
            }
        }
        $licenses = @(Get-ChildItem -LiteralPath $plugin.FullName | Where-Object Name -Match '^(LICENSE|COPYING|COPYRIGHT)')
        if ($licenses.Count) {
            New-Item -ItemType Directory -Force "$destination/licenses" | Out-Null
            $licenses | Copy-Item -Destination "$destination/licenses" -Recurse -Force
        }
    }

    # Include dependencies of private plugin DLLs as well as the addon DLLs.
    $pending = [Collections.Generic.Queue[string]]::new()
    $visited = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $pluginRoot = "$Prefix/plugins"
    $privateBins = @()
    if (Test-Path -LiteralPath $pluginRoot) {
        $privateBins = @(Get-ChildItem -LiteralPath $pluginRoot -Directory | ForEach-Object { "$($_.FullName)/bin" })
        Get-ChildItem -LiteralPath $pluginRoot -Recurse -File | Where-Object Extension -in '.dll', '.exe' |
            ForEach-Object { $pending.Enqueue($_.FullName) }
    }
    while ($pending.Count) {
        $file = $pending.Dequeue()
        if (!$visited.Add($file)) { continue }
        $imports = & "$sysroot/bin/llvm-readobj.exe" --coff-imports $file
        if ($LASTEXITCODE -ne 0) { throw "Cannot read PE imports: $file" }
        foreach ($line in $imports) {
            if ($line -notmatch '^\s+Name: (.+\.dll)$') { continue }
            $name = $matches[1]
            $existing = @("$Prefix/bin") + $privateBins | ForEach-Object { "$_/$name" } |
                Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
            if ($existing) { $pending.Enqueue($existing); continue }
            $source = $runtimeDirectories | ForEach-Object { "$_/$name" } |
                Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
            if ($source) {
                Copy-Item -LiteralPath $source -Destination "$Prefix/bin/$name"
                $pending.Enqueue("$Prefix/bin/$name")
            } elseif ($name -notmatch '^(api-|ext-)' -and !(Test-Path -LiteralPath "$env:SystemRoot/System32/$name")) {
                throw "Unresolved runtime dependency $name in $file"
            }
        }
    }
    Write-Output "Full plugin runtime ready: $Prefix"
} finally {
    $env:PATH = $oldPath
    $env:MSYS2_ROOT = $oldRoot
    $env:PKG_CONFIG_PATH = $oldPkgConfig
}
