[CmdletBinding()]
param(
    [string]$TsfBuild = '',
    [string]$IsccPath = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$TsfBuild) { $TsfBuild = Join-Path $root 'win32/build/pinyin-tsf' }
if (!$IsccPath) { $IsccPath = [IO.Path]::GetFullPath("$env:LOCALAPPDATA/Programs/Inno Setup 7/ISCC.exe") }
$temporary = Join-Path $root "build/reboot-package-test $([guid]::NewGuid().ToString('N'))"
$prefix = Join-Path $temporary 'stage'
try {
    foreach ($directory in @('bin', 'tsf', 'settings', 'setup', 'licenses')) {
        New-Item -ItemType Directory -Force (Join-Path $prefix $directory) | Out-Null
    }
    foreach ($file in @('bin/Fcitx5.exe', 'settings/Fcitx5Settings.exe', 'setup/Fcitx5SetupHelper.exe')) {
        Copy-Item -LiteralPath "$TsfBuild/setup/Fcitx5SetupHelper.exe" -Destination (Join-Path $prefix $file)
    }
    Copy-Item -LiteralPath "$TsfBuild/setup/Fcitx5Upgrade.exe" -Destination "$prefix/setup/Fcitx5Upgrade.exe"
    Copy-Item -LiteralPath "$TsfBuild/tests/upgrade_fixture.dll" -Destination "$prefix/tsf/fcitx5-x86_64.dll"
    $dependencies = Join-Path $temporary 'dependencies'
    New-Item -ItemType Directory -Force $dependencies | Out-Null
    foreach ($file in @('vc_redist.x64.exe', 'WindowsAppRuntimeInstall-x64.exe')) {
        [IO.File]::WriteAllText((Join-Path $dependencies $file), 'Packaging fixture; must never execute.')
    }
    foreach ($bundled in @($false, $true)) {
        $options = @{
            Prefix = $prefix; Output = "$temporary/output"; Dependencies = $dependencies
            IsccPath = $IsccPath; PackageVersion = '1.2.3.4'; RebootUpgrade = $true
        }
        if (!$bundled) { $options.SkipRuntimes = $true }
        & "$PSScriptRoot/build-installer.ps1" @options | Out-Null
    }
    & "$PSScriptRoot/build-installer.ps1" -Prefix $prefix -Output "$temporary/output" `
        -IsccPath $IsccPath -PackageVersion '1.2.3.4' -PackageName 'default-no-runtime' -SkipRuntimes | Out-Null
    Write-Output 'PASS: real Inno compilation of both reboot-upgrade variants and the default package (fixture payload; no installation).'
} finally {
    $resolved = [IO.Path]::GetFullPath($temporary)
    if (!$resolved.StartsWith(($root + '\build\'), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected cleanup path: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
