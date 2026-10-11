[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$Prefix = '',
    [string]$BuildRoot = '',
    [string]$BoostSource = '',
    [string]$BoostHeaders = '',
    [switch]$UiTest
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio with MSBuild and C++/WinRT tools is required.' }
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'MSBuild was not found.' }
$visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$developerCommand = Join-Path $visualStudio 'Common7/Tools/VsDevCmd.bat'
if (!$BuildRoot) { $BuildRoot = "$root/win32/build/settings" }
$i18nBuild = [IO.Path]::GetFullPath("$BuildRoot/i18n/$Configuration")
$before = @{}
Get-ChildItem Env: | ForEach-Object { $before[$_.Name] = $_.Value }
try {
    $developerEnvironment = & cmd.exe /d /c "`"$developerCommand`" -arch=amd64 -host_arch=amd64 >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'Visual Studio environment initialization failed.' }
    foreach ($line in $developerEnvironment) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
    $i18nOptions = @('-S', "$root/win32/i18n", '-B', $i18nBuild, '-G', 'Ninja',
        "-DCMAKE_BUILD_TYPE=$Configuration", '-DCMAKE_CXX_COMPILER=cl.exe',
        "-DCMAKE_MAKE_PROGRAM=$((Get-Command ninja.exe -CommandType Application | Select-Object -First 1).Source)")
    if ($BoostSource) { $i18nOptions += "-DFETCHCONTENT_SOURCE_DIR_NATIVE_BOOST=$([IO.Path]::GetFullPath($BoostSource))" }
    if ($BoostHeaders) { $i18nOptions += "-DNATIVE_BOOST_HEADERS=$([IO.Path]::GetFullPath($BoostHeaders))" }
    & cmake @i18nOptions
    if ($LASTEXITCODE -ne 0) { throw 'Settings translation library configuration failed.' }
    & cmake --build $i18nBuild -j 6
    if ($LASTEXITCODE -ne 0) { throw 'Settings translation library build failed.' }
} finally {
    foreach ($variable in @(Get-ChildItem Env:)) {
        if (!$before.ContainsKey($variable.Name)) { [Environment]::SetEnvironmentVariable($variable.Name, $null, 'Process') }
    }
    foreach ($name in $before.Keys) { [Environment]::SetEnvironmentVariable($name, $before[$name], 'Process') }
}
$kits = (Get-ItemProperty 'HKLM:/SOFTWARE/Microsoft/Windows Kits/Installed Roots').KitsRoot10
$sdk = Get-ChildItem "$kits/References" -Directory | Where-Object { $_.Name -match '^10\.0\.' } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1 -ExpandProperty Name
if (!$sdk -or [version]$sdk -lt [version]'10.0.22000.0') { throw 'Windows SDK 10.0.22000.0 or later is required.' }
& $msbuild "$root/win32/settings/Fcitx5Settings.vcxproj" /restore /m /nologo /verbosity:minimal `
    "/p:Configuration=$Configuration" /p:Platform=x64 "/p:WindowsTargetPlatformVersion=$sdk" "/p:TargetPlatformVersion=$sdk" "/p:I18nBuildDir=$i18nBuild" "/p:OutDir=$([IO.Path]::GetFullPath("$BuildRoot/$Configuration"))/" "/p:IntDir=$([IO.Path]::GetFullPath("$BuildRoot/obj/$Configuration"))/" "/p:UiTest=$($UiTest.IsPresent.ToString().ToLowerInvariant())"
if ($LASTEXITCODE -ne 0) { throw 'WinUI settings build failed.' }
if ($Prefix) {
    $destination = [IO.Path]::GetFullPath($Prefix)
    $repository = [IO.Path]::GetFullPath($root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (!$destination.StartsWith($repository, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The isolated deployment prefix must be inside this repository.'
    }
    New-Item -ItemType Directory -Force "$destination/settings" | Out-Null
    Get-ChildItem "$BuildRoot/$Configuration" -File |
        Where-Object { $_.Extension -in '.exe', '.dll', '.pri', '.winmd' } |
        Copy-Item -Destination "$destination/settings" -Force
    New-Item -ItemType Directory -Force "$destination/share/locale" | Out-Null
    Copy-Item "$i18nBuild/share/locale/*" "$destination/share/locale" -Recurse -Force
    & cmake --install $i18nBuild --prefix $destination
    if ($LASTEXITCODE -ne 0) { throw 'Translation resource deployment failed.' }
}
Write-Output 'Framework-dependent deployment requires the matching x64 Windows App SDK 1.8 runtime and Visual C++ runtime.'
