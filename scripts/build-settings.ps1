[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$Prefix = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio with MSBuild and C++/WinRT tools is required.' }
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'MSBuild was not found.' }
$kits = (Get-ItemProperty 'HKLM:/SOFTWARE/Microsoft/Windows Kits/Installed Roots').KitsRoot10
$sdk = Get-ChildItem "$kits/References" -Directory | Where-Object { $_.Name -match '^10\.0\.' } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1 -ExpandProperty Name
if (!$sdk -or [version]$sdk -lt [version]'10.0.22000.0') { throw 'Windows SDK 10.0.22000.0 or later is required.' }
& $msbuild "$root/win32/settings/Fcitx5Settings.vcxproj" /restore /m /nologo /verbosity:minimal `
    "/p:Configuration=$Configuration" /p:Platform=x64 "/p:WindowsTargetPlatformVersion=$sdk" "/p:TargetPlatformVersion=$sdk"
if ($LASTEXITCODE -ne 0) { throw 'WinUI settings build failed.' }
if ($Prefix) {
    $destination = [IO.Path]::GetFullPath($Prefix)
    $repository = [IO.Path]::GetFullPath($root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (!$destination.StartsWith($repository, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The isolated deployment prefix must be inside this repository.'
    }
    New-Item -ItemType Directory -Force "$destination/settings" | Out-Null
    Get-ChildItem "$root/win32/build/settings/$Configuration" -File |
        Where-Object { $_.Extension -in '.exe', '.dll', '.pri', '.winmd' } |
        Copy-Item -Destination "$destination/settings" -Force
}
Write-Output 'Framework-dependent deployment requires the matching x64 Windows App SDK 1.8 runtime and Visual C++ runtime.'
