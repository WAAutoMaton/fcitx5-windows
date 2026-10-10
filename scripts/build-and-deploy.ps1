<#
.SYNOPSIS
Build and deploy the AMD64 Pinyin Core, TSF DLL and WinUI settings application.
.DESCRIPTION
Requires installed build dependencies. Builds Release binaries into a staging
prefix, runs default TSF CTest tests, then copies the runtime tree to Prefix.
Does not install tools/runtimes, register TSF, stop applications or start services.
.EXAMPLE
./scripts/build-and-deploy.ps1
.EXAMPLE
./scripts/build-and-deploy.ps1 -Prefix ./dist/another -WhatIf
#>
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Low')]
param(
    [string]$Prefix = '',
    [string]$BuildRoot = '',
    [string]$MSYS2Root = 'C:/msys64',
    [string]$LLVMRoot = 'C:/Program Files/LLVM',
    [string]$DependencyPrefix = '',
    [ValidateSet('Source', 'Prebuilt')][string]$DataMode = 'Prebuilt',
    [string]$DataArchive = '',
    [ValidateRange(1, 256)][int]$Jobs = 6,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

function Resolve-BuildPath([string]$Path) {
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path).TrimEnd('\', '/')
}

function Assert-RepositoryPath([string]$Path) {
    if (!$Path.StartsWith(($root + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Build and deployment paths must be inside this repository: $Path"
    }
}

function Assert-DeploymentWritable([string]$Directory) {
    if (!(Test-Path -LiteralPath $Directory)) { return }
    if (!(Get-Item -LiteralPath $Directory).PSIsContainer) {
        throw "Expected a deployment directory: $Directory"
    }
    foreach ($file in Get-ChildItem -LiteralPath $Directory -Recurse -File) {
        try {
            $stream = [IO.File]::Open($file.FullName, [IO.FileMode]::Open,
                [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
            $stream.Dispose()
        } catch {
            throw "Cannot update '$($file.FullName)': file is in use or not writable. Stop Core/Settings using the input indicator, close applications using this DLL, or choose another -Prefix. No process was stopped."
        }
    }
}

function Invoke-BuildCommand([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable failed ($LASTEXITCODE): $($Arguments -join ' ')"
    }
}

function Require-BuildTool([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required build tool was not found: $Path"
    }
    return $Path.Replace('\', '/')
}

if (!$Prefix) { $Prefix = Join-Path $root 'dist/pinyin' }
if (!$BuildRoot) { $BuildRoot = Join-Path $root 'build/all' }
$Prefix = Resolve-BuildPath $Prefix
$BuildRoot = Resolve-BuildPath $BuildRoot
Assert-RepositoryPath $Prefix
Assert-RepositoryPath $BuildRoot
if ($Prefix -ieq $BuildRoot -or
    $Prefix.StartsWith(($BuildRoot + '\'), [StringComparison]::OrdinalIgnoreCase) -or
    $BuildRoot.StartsWith(($Prefix + '\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Prefix and BuildRoot must be separate directories without overlap.'
}
$staging = Join-Path $BuildRoot 'prefix'
$tsfBuild = Join-Path $BuildRoot 'tsf'
if (!$PSCmdlet.ShouldProcess($Prefix,
    "Build AMD64 Release Core/TSF/Settings in '$BuildRoot', run CTest, and deploy the runtime tree")) {
    return
}

Assert-DeploymentWritable $Prefix
Assert-DeploymentWritable $staging
$MSYS2Root = Resolve-BuildPath $MSYS2Root
$LLVMRoot = Resolve-BuildPath $LLVMRoot
if (!$DependencyPrefix -and (Test-Path -LiteralPath "$root/build/deps/clang64" -PathType Container)) {
    $DependencyPrefix = "$root/build/deps/clang64"
}
if ($DependencyPrefix) { $DependencyPrefix = Resolve-BuildPath $DependencyPrefix }
if ($DataArchive) { $DataArchive = Resolve-BuildPath $DataArchive }

$cmake = (Get-Command cmake.exe -CommandType Application -ErrorAction Stop).Source
$git = (Get-Command git.exe -CommandType Application -ErrorAction Stop).Source
$null = Get-Command magick.exe -CommandType Application -ErrorAction Stop
$ninja = Require-BuildTool "$MSYS2Root/clang64/bin/ninja.exe"
$null = Require-BuildTool "$MSYS2Root/clang64/bin/clang++.exe"
$clang = Require-BuildTool "$LLVMRoot/bin/clang.exe"
$clangxx = Require-BuildTool "$LLVMRoot/bin/clang++.exe"
$resourceCompiler = Require-BuildTool "$LLVMRoot/bin/llvm-rc.exe"
$vswhere = Require-BuildTool "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$cmakeVersion = & $cmake --version
if ($LASTEXITCODE -ne 0 -or ($cmakeVersion -join "`n") -notmatch 'cmake version (\d+\.\d+\.\d+)' -or
    [version]$matches[1] -lt [version]'3.27.0') {
    throw 'CMake 3.27 or later is required.'
}
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild `
    Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.ATLMFC -property installationPath
if ($LASTEXITCODE -ne 0 -or !$visualStudio) {
    throw 'Visual Studio with MSBuild, x64 C++ tools, Windows SDK and ATL is required.'
}
$developerCommand = Require-BuildTool "$visualStudio/Common7/Tools/VsDevCmd.bat"
if (!('FcitxBuildEnvironment' -as [type])) {
    Add-Type @'
using System.Runtime.InteropServices;
public static class FcitxBuildEnvironment {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetEnvironmentVariable(string name, string value);
}
'@
}
$environmentBefore = @{}
Get-ChildItem Env: | ForEach-Object { $environmentBefore[$_.Name] = $_.Value }
$buildLock = $null
Push-Location $root
try {
    New-Item -ItemType Directory -Force "$root/build" | Out-Null
    try {
        $buildLock = [IO.File]::Open("$root/build/build-and-deploy.lock", [IO.FileMode]::OpenOrCreate,
            [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    } catch {
        throw 'Another build-and-deploy invocation is running, or the build lock is not writable.'
    }
    Write-Host '[1/5] Initialize pinned submodules and build Pinyin Core into staging.'
    Invoke-BuildCommand $git @('submodule', 'update', '--init', '--recursive')
    $env:MSYSTEM_PREFIX = "$MSYS2Root/clang64"
    $coreParameters = @{
        MSYS2Root = $MSYS2Root; DependencyPrefix = $DependencyPrefix
        Prefix = $staging; BuildRoot = $BuildRoot
        DataMode = $DataMode; DataArchive = $DataArchive; Jobs = $Jobs
    }
    & "$PSScriptRoot/build-pinyin.ps1" @coreParameters

    Write-Host '[2/5] Initialize Visual Studio x64 environment and build TSF.'
    $developerEnvironment = & cmd.exe /d /c "`"$developerCommand`" -arch=amd64 -host_arch=amd64 >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'Visual Studio environment initialization failed.' }
    foreach ($line in $developerEnvironment) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
    Invoke-BuildCommand $cmake @('-S', "$root/win32", '-B', $tsfBuild, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_C_COMPILER=$clang", "-DCMAKE_CXX_COMPILER=$clangxx",
        "-DCMAKE_RC_COMPILER=$resourceCompiler", "-DCMAKE_MAKE_PROGRAM=$ninja",
        '-DENABLE_PINYIN_INTEGRATION_TESTS=OFF')
    Invoke-BuildCommand $cmake @('--build', $tsfBuild, '-j', "$Jobs")

    Write-Host '[3/5] Build WinUI Settings and stage TSF/Settings.'
    & "$PSScriptRoot/build-settings.ps1" -Configuration Release -Prefix $staging
    & "$PSScriptRoot/deploy-tsf.ps1" -Prefix $staging -TsfBuild $tsfBuild

    Write-Host '[4/5] Validate staged outputs and run default CTest tests.'
    foreach ($file in @('bin/Fcitx5.exe', 'lib/fcitx5/libpinyin.dll', 'share/libime/sc.dict',
            'lib/libime/zh_CN.lm', 'tsf/fcitx5-x86_64.dll', 'settings/Fcitx5Settings.exe')) {
        if (!(Test-Path -LiteralPath "$staging/$file" -PathType Leaf)) {
            throw "Missing staged output: $file"
        }
    }
    if (!$SkipTests) {
        $ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
        $null = Require-BuildTool $ctest
        Invoke-BuildCommand $ctest @('--test-dir', $tsfBuild, '--output-on-failure')
    }

    Write-Host '[5/5] Deploy the complete runtime tree.'
    Assert-DeploymentWritable $Prefix
    New-Item -ItemType Directory -Force $Prefix | Out-Null
    foreach ($directory in @('bin', 'lib', 'share', 'tsf', 'settings')) {
        Copy-Item -LiteralPath "$staging/$directory" -Destination $Prefix -Recurse -Force
    }
    Write-Output "Deployment ready: $Prefix"
    Write-Output "TSF tests/probes: $tsfBuild/tests"
    Write-Output 'Services were not started. TSF registration and system runtimes were not changed.'
} finally {
    if ($buildLock) { $buildLock.Dispose() }
    Pop-Location
    foreach ($variable in @(Get-ChildItem Env:)) {
        if (!$environmentBefore.ContainsKey($variable.Name)) {
            [Environment]::SetEnvironmentVariable($variable.Name, $null, 'Process')
        }
    }
    foreach ($name in $environmentBefore.Keys) {
        # .NET removes empty values; preserve originally present empty variables.
        if (![FcitxBuildEnvironment]::SetEnvironmentVariable($name, $environmentBefore[$name])) {
            throw "Failed to restore environment variable: $name"
        }
    }
}
