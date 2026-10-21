[CmdletBinding(SupportsShouldProcess)]
param(
    [ValidateSet('x64', 'x86')][string]$Architecture = 'x64',
    [string]$BuildRoot = '',
    [string]$LLVMRoot = 'C:/Program Files/LLVM',
    [string]$BoostSource = '',
    [string]$BoostHeaders = '',
    [ValidateRange(1, 256)][int]$Jobs = 6,
    [switch]$ModernApps,
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$BuildRoot) { $BuildRoot = "$root/build/tsf-$Architecture" }
$BuildRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildRoot)
$LLVMRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($LLVMRoot)
if ($BoostSource) { $BoostSource = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BoostSource) }
if ($BoostHeaders) { $BoostHeaders = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BoostHeaders) }
if (!$PSCmdlet.ShouldProcess($BuildRoot, "Build and test $Architecture TSF in a separate CMake tree")) { return }
$cache = Join-Path $BuildRoot 'CMakeCache.txt'
if (Test-Path -LiteralPath $cache) {
    $cached = Select-String -LiteralPath $cache -Pattern '^TSF_ARCH:STRING=(.+)$'
    if (!$cached -or $cached.Matches[0].Groups[1].Value -ne $Architecture) {
        throw 'Build tree architecture is unknown or different; choose a new -BuildRoot.'
    }
}
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.ATLMFC -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'Visual Studio C++ tools, Windows SDK and ATL are required.' }
$cmake = (Get-Command cmake.exe -CommandType Application | Select-Object -First 1).Source
$ninja = (Get-Command ninja.exe -CommandType Application | Select-Object -First 1).Source
$before = @{}
Get-ChildItem Env: | ForEach-Object { $before[$_.Name] = $_.Value }
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
function Invoke-TsfBuild([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed ($LASTEXITCODE)." }
}
try {
    $target = if ($Architecture -eq 'x86') { 'x86' } else { 'amd64' }
    $variables = & cmd.exe /d /c "`"$vs/Common7/Tools/VsDevCmd.bat`" -arch=$target -host_arch=amd64 >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'Visual Studio environment initialization failed.' }
    foreach ($line in $variables) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
    $options = @()
    if ($BoostSource) { $options += "-DFETCHCONTENT_SOURCE_DIR_NATIVE_BOOST=$BoostSource" }
    if ($BoostHeaders) { $options += "-DNATIVE_BOOST_HEADERS=$BoostHeaders" }
    Invoke-TsfBuild $cmake (@('-S', "$root/win32", '-B', $BuildRoot, '-G', 'Ninja',
        "-DCMAKE_TOOLCHAIN_FILE=$root/cmake/tsf-$Architecture.cmake", '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_C_COMPILER=$LLVMRoot/bin/clang.exe", "-DCMAKE_CXX_COMPILER=$LLVMRoot/bin/clang++.exe",
        "-DCMAKE_RC_COMPILER=$LLVMRoot/bin/llvm-rc.exe", "-DCMAKE_MAKE_PROGRAM=$ninja",
        "-DTSF_ARCH=$Architecture", "-DENABLE_MODERN_TSF_CATEGORIES=$($ModernApps.IsPresent.ToString().ToUpperInvariant())") + $options)
    Invoke-TsfBuild $cmake @('--build', $BuildRoot, '-j', "$Jobs")
    $name = if ($Architecture -eq 'x86') { 'fcitx5-x86.dll' } else { 'fcitx5-x86_64.dll' }
    & "$root/win32/scripts/check-tsf-runtime.ps1" -DllPath "$BuildRoot/dll/$name" -Architecture $Architecture
    if (!$SkipTests) {
        Invoke-TsfBuild (Join-Path (Split-Path $cmake) 'ctest.exe') @('--test-dir', $BuildRoot, '--output-on-failure')
    }
} finally {
    foreach ($variable in @(Get-ChildItem Env:)) {
        if (!$before.ContainsKey($variable.Name)) { [Environment]::SetEnvironmentVariable($variable.Name, $null, 'Process') }
    }
    foreach ($name in $before.Keys) {
        if (![FcitxBuildEnvironment]::SetEnvironmentVariable($name, $before[$name])) {
            throw "Failed to restore environment variable: $name"
        }
    }
}
