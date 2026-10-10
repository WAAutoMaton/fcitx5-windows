[CmdletBinding()]
param([string]$Prefix = '', [string]$TsfBuild = '')

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
$dll = (Resolve-Path -LiteralPath "$TsfBuild/dll/fcitx5-x86_64.dll").Path
$helper = (Resolve-Path -LiteralPath "$TsfBuild/setup/Fcitx5SetupHelper.exe").Path
New-Item -ItemType Directory -Force "$destination/tsf" | Out-Null
New-Item -ItemType Directory -Force "$destination/setup" | Out-Null
Copy-Item -LiteralPath $dll -Destination "$destination/tsf/fcitx5-x86_64.dll"
Copy-Item -LiteralPath $helper -Destination "$destination/setup/Fcitx5SetupHelper.exe"
Write-Output "Deployed $destination/tsf/fcitx5-x86_64.dll. Registration was not changed."
