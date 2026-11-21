<#
.SYNOPSIS
Build every additional plugin in plugins/ against an installed Core/libime SDK.
.DESCRIPTION
fcitx5-chinese-addons is already built by build-pinyin.ps1. Other plugins use the
generic Windows addon toolchain and are deployed in independent directories.
#>
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Low')]
param(
    [Parameter(Mandatory)][string]$CorePrefix,
    [Parameter(Mandatory)][string]$Prefix,
    [string]$BuildRoot = '',
    [string]$MSYS2Root = 'C:/msys64',
    [string]$DependencyPrefix = '',
    [string]$MozcData = '',
    [ValidateRange(1, 256)][int]$Jobs = 6
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$BuildRoot) { $BuildRoot = "$root/build/plugins" }
if (!$DependencyPrefix -and (Test-Path -LiteralPath "$root/build/deps/clang64")) {
    $DependencyPrefix = "$root/build/deps/clang64"
}
foreach ($name in @('CorePrefix', 'Prefix', 'BuildRoot', 'MSYS2Root', 'DependencyPrefix', 'MozcData')) {
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

function Resolve-PythonExecutable([string]$Sysroot) {
    foreach ($candidate in @('python', 'python3')) {
        foreach ($cmd in @(Get-Command $candidate -CommandType Application -All -ErrorAction SilentlyContinue)) {
            if ($cmd.Source -notlike '*\WindowsApps\*') { return $cmd.Source }
        }
    }
    foreach ($fallback in @("$Sysroot/bin/python.exe", "$Sysroot/bin/python3.exe")) {
        if (Test-Path -LiteralPath $fallback -PathType Leaf) { return $fallback }
    }
    throw 'Python 3 is required to build fcitx5-mozc.'
}

function Test-MozcDataHeader([string]$Path) {
    $expected = [byte[]]@(0xEF, 0x4D, 0x4F, 0x5A, 0x43, 0x0D, 0x0A)
    $stream = [IO.File]::OpenRead($Path)
    try {
        if ($stream.Length -lt $expected.Length) { return $false }
        $buffer = New-Object byte[] $expected.Length
        $read = $stream.Read($buffer, 0, $expected.Length)
        if ($read -ne $expected.Length) { return $false }
        for ($i = 0; $i -lt $expected.Length; $i++) {
            if ($buffer[$i] -ne $expected[$i]) { return $false }
        }
        return $true
    } finally { $stream.Dispose() }
}

function Assert-MozcDataValid([string]$Path, [string]$ExpectedHash) {
    if (!(Test-MozcDataHeader $Path)) {
        throw "Invalid Mozc data header: $Path"
    }
    if ($ExpectedHash) {
        $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $ExpectedHash) {
            throw "MozcData SHA256 mismatch for '$Path': expected $ExpectedHash, got $actual"
        }
    }
}

function Resolve-MozcDataFile([string]$Root, [string]$ExplicitPath) {
    $lockPath = "$Root/cmake/pinyin-lock.json"
    if (!(Test-Path -LiteralPath $lockPath -PathType Leaf)) {
        throw "Missing Mozc data lock file: $lockPath"
    }
    $spec = (Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json).mozcData
    if (!$spec -or !$spec.file -or !$spec.url) {
        throw "Missing mozcData metadata in $lockPath"
    }
    $expectedHash = if ($spec.PSObject.Properties['sha256'] -and $spec.sha256) {
        $spec.sha256.ToLowerInvariant()
    } else { '' }
    if ($ExplicitPath) {
        if (!(Test-Path -LiteralPath $ExplicitPath -PathType Leaf)) {
            throw "MozcData file was not found: $ExplicitPath"
        }
        Assert-MozcDataValid $ExplicitPath $expectedHash
        return $ExplicitPath
    }
    $cacheDir = "$Root/build/mozc-data"
    $cached = "$cacheDir/$($spec.file)"
    if ((Test-Path -LiteralPath $cached -PathType Leaf) -and (Test-MozcDataHeader $cached)) {
        if (!$expectedHash -or
            ((Get-FileHash -LiteralPath $cached -Algorithm SHA256).Hash.ToLowerInvariant() -eq $expectedHash)) {
            return $cached
        }
    }
    New-Item -ItemType Directory -Force $cacheDir | Out-Null
    $temp = "$cached.tmp.$([guid]::NewGuid().ToString('N'))"
    try {
        Invoke-PluginCommand 'curl.exe' @('--fail', '--location', '--retry', '3',
            '--connect-timeout', '30', '--max-time', '600', '--silent', '--show-error',
            '--output', $temp, $spec.url)
        Assert-MozcDataValid $temp $expectedHash
        Move-Item -LiteralPath $temp -Destination $cached -Force
    } finally {
        if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue }
    }
    return $cached
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
        if ($plugin.Name -eq 'fcitx5-chinese-addons') { continue }
        if (!(Test-Path -LiteralPath "$($plugin.FullName)/CMakeLists.txt")) {
            throw "Plugin is not initialized or has no CMakeLists.txt: $($plugin.Name)"
        }
        Invoke-PluginCommand 'git' @('-C', $plugin.FullName, 'lfs', 'pull')
        $build = "$BuildRoot/$($plugin.Name)"
        $stage = "$build/prefix"
        $pluginDependencies = $DependencyPrefix
        $pluginOptions = @()
        if ($plugin.Name -eq 'fcitx5-mozc') {
            $embedScript = "$($plugin.FullName)/mozc/src/build_tools/embed_file.py"
            if (!(Test-Path -LiteralPath $embedScript -PathType Leaf)) {
                throw 'fcitx5-mozc nested submodules are not initialized. Run git submodule update --init --recursive.'
            }
            $mozcDataFile = Resolve-MozcDataFile $root $MozcData
            $mozcDataInc = "$build/data_manager/oss/mozc_data.inc"
            New-Item -ItemType Directory -Force (Split-Path $mozcDataInc -Parent) | Out-Null
            $needsEmbed = !(Test-Path -LiteralPath $mozcDataInc -PathType Leaf) -or
                ((Get-Item -LiteralPath $mozcDataInc).Length -eq 0) -or
                ((Get-Item -LiteralPath $mozcDataInc).LastWriteTimeUtc -lt (Get-Item -LiteralPath $mozcDataFile).LastWriteTimeUtc)
            if ($needsEmbed) {
                $python = Resolve-PythonExecutable $sysroot
                Invoke-PluginCommand $python @($embedScript, '--input', $mozcDataFile, '--name', 'kOssMozcDataSet', '--output', $mozcDataInc)
            }
        }
        if ($plugin.Name -eq 'fcitx5-zhuyin') {
            $dependencyBuild = "$BuildRoot/zhuyin-deps"
            $zhuyinPrefix = "$dependencyBuild/prefix"
            $env:PATH = ($runtimeDirectories -join ';') + ";$oldPath"
            Invoke-PluginCommand 'cmake' @('-S', "$root/cmake/zhuyin-deps", '-B', $dependencyBuild, '-G', 'Ninja',
                "-DCMAKE_TOOLCHAIN_FILE=$root/cmake/pinyin-toolchain.cmake",
                "-DWINDOWS_DEPENDENCY_PREFIX=$DependencyPrefix", "-DCMAKE_INSTALL_PREFIX=$zhuyinPrefix",
                '-DARCH=AMD64', '-DCMAKE_BUILD_TYPE=Release',
                "-DCMAKE_CXX_COMPILER=$sysroot/bin/clang++.exe", "-DCMAKE_MAKE_PROGRAM=$sysroot/bin/ninja.exe")
            Invoke-PluginCommand 'cmake' @('--build', $dependencyBuild, '-j', "$Jobs")
            Invoke-PluginCommand 'cmake' @('--install', $dependencyBuild)
            $pluginDependencies = (@($zhuyinPrefix, $DependencyPrefix) | Where-Object { $_ }) -join ';'
            $env:PKG_CONFIG_PATH = "$zhuyinPrefix/lib/pkgconfig;$DependencyPrefix/lib/pkgconfig;$sysroot/lib/pkgconfig"
            $runtimeDirectories = @("$zhuyinPrefix/bin") + $runtimeDirectories
            $pluginOptions += '-DLIBZHUYIN_DATABASE_FORMAT=BerkeleyDB'
        }
        $env:PATH = "$CorePrefix/bin;$build/bin;" + ($runtimeDirectories -join ';') + ";$MSYS2Root/usr/bin;$oldPath"
        Invoke-PluginCommand 'cmake' (@('-S', $plugin.FullName, '-B', $build, '-G', 'Ninja',
            "-DCMAKE_TOOLCHAIN_FILE=$root/cmake/windows-addon-toolchain.cmake",
            "-DFCITX5_WINDOWS_SDK=$CorePrefix", "-DWINDOWS_DEPENDENCY_PREFIX=$pluginDependencies",
            '-DARCH=AMD64', '-DCMAKE_BUILD_TYPE=Release', '-DENABLE_TEST=OFF',
            "-DCMAKE_INSTALL_PREFIX=$stage", '-DCMAKE_INSTALL_LIBDIR=lib',
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$build/bin", "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$build/bin",
            "-DCMAKE_C_COMPILER=$sysroot/bin/clang.exe", "-DCMAKE_CXX_COMPILER=$sysroot/bin/clang++.exe",
            "-DCMAKE_MAKE_PROGRAM=$sysroot/bin/ninja.exe", '-DCMAKE_CXX_FLAGS=-fexperimental-library') + $pluginOptions)
        Invoke-PluginCommand 'cmake' @('--build', $build, '-j', "$Jobs")
        Invoke-PluginCommand 'cmake' @('--install', $build)
        if ($plugin.Name -eq 'fcitx5-rime') {
            $rimeData = @("$sysroot/share/rime-data", "$sysroot/share/rime/data") |
                Where-Object { Test-Path -LiteralPath $_ -PathType Container } |
                Select-Object -First 1
            if (!$rimeData) {
                throw "fcitx5-rime requires the MSYS2 Rime data directory."
            }
            $rimeStage = "$stage/share/rime-data"
            New-Item -ItemType Directory -Force $rimeStage | Out-Null
            Copy-Item -Path "$rimeData/*" -Destination $rimeStage -Recurse -Force
            # librime resolves schema OpenCC configurations relative to its
            # shared data tree in a relocatable Windows package.
            $openccData = "$sysroot/share/opencc"
            if (Test-Path -LiteralPath $openccData -PathType Container) {
                New-Item -ItemType Directory -Force "$rimeStage/opencc" | Out-Null
                Copy-Item -Path "$openccData/*" -Destination "$rimeStage/opencc" -Recurse -Force
            }
        }
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
        if ($plugin.Name -eq 'fcitx5-mozc') {
            foreach ($submodule in @('mozc', 'abseil-cpp', 'protobuf')) {
                $license = "$($plugin.FullName)/$submodule/LICENSE"
                if (!(Test-Path -LiteralPath $license -PathType Leaf)) {
                    throw "Missing fcitx5-mozc dependency license: $submodule/LICENSE"
                }
                New-Item -ItemType Directory -Force "$destination/licenses/$submodule" | Out-Null
                Copy-Item -LiteralPath $license -Destination "$destination/licenses/$submodule/LICENSE" -Force
            }
        }
        if ($plugin.Name -eq 'fcitx5-zhuyin') {
            New-Item -ItemType Directory -Force "$destination/bin", "$destination/licenses" | Out-Null
            Copy-Item -LiteralPath "$zhuyinPrefix/bin/libzhuyin.dll" -Destination "$destination/bin" -Force
            Copy-Item -LiteralPath "$zhuyinPrefix/licenses/libpinyin" -Destination "$destination/licenses" -Recurse -Force
            foreach ($dependency in @('glib2', 'db')) {
                $license = @($DependencyPrefix, $sysroot) | Where-Object { $_ } |
                    ForEach-Object { "$_/share/licenses/$dependency" } |
                    Where-Object { Test-Path -LiteralPath $_ -PathType Container } | Select-Object -First 1
                if (!$license) { throw "Missing $dependency dependency license." }
                Copy-Item -LiteralPath $license -Destination "$destination/licenses" -Recurse -Force
            }
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
