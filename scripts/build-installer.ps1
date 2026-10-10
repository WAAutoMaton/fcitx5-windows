[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$Prefix = '',
    [string]$Dependencies = '',
    [string]$Output = '',
    [string]$PackageVersion = '0.1.0.0',
    [string]$PackageName = '',
    [string]$IsccPath = '',
    [switch]$SkipRuntimes
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Prefix) { $Prefix = "$root/dist/installer-stage" }
if (!$Dependencies) { $Dependencies = "$root/build/installer-deps" }
if (!$Output) { $Output = "$root/dist/installer" }
$Prefix = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Prefix)
$Dependencies = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Dependencies)
$Output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
foreach ($path in @($Prefix, $Dependencies, $Output)) {
    if (!$path.StartsWith(([IO.Path]::GetFullPath($root) + [IO.Path]::DirectorySeparatorChar),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Installer paths must be inside the repository: $path"
    }
}
foreach ($file in @('bin/Fcitx5.exe', 'tsf/fcitx5-x86_64.dll',
        'setup/Fcitx5SetupHelper.exe', 'settings/Fcitx5Settings.exe')) {
    if (!(Test-Path -LiteralPath (Join-Path $Prefix $file) -PathType Leaf)) {
        throw "Missing staged installer file: $file"
    }
}
if (!$SkipRuntimes) {
    foreach ($file in @('vc_redist.x64.exe', 'WindowsAppRuntimeInstall-x64.exe')) {
        if (!(Test-Path -LiteralPath (Join-Path $Dependencies $file) -PathType Leaf)) {
            throw "Missing installer dependency: $file"
        }
    }
}
if (!$PSCmdlet.ShouldProcess($Output, 'Build Fcitx5 Inno Setup installer')) { return }
$iscc = if ($IsccPath) { Get-Command $IsccPath -ErrorAction Stop } else { Get-Command ISCC.exe -ErrorAction SilentlyContinue }
if (!$iscc) {
    $iscc = Get-ChildItem 'C:/Program Files (x86)/Inno Setup 7',
        'C:/Program Files/Inno Setup 7', "$env:LOCALAPPDATA/Programs/Inno Setup 7" -Filter ISCC.exe -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
}
if (!$iscc) { throw 'ISCC.exe was not found. Install the pinned Inno Setup compiler.' }
$isccPath = if ($iscc -is [Array]) { $iscc[0].FullName } elseif ($iscc.PSObject.Properties['Source']) { $iscc.Source } else { $iscc.FullName }
$compilerVersion = (& $isccPath --version | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $compilerVersion -notmatch '^7\.\d+\.\d+(?:\..*)?(?:-.*)?$') {
    throw "Inno Setup 7 is required. Compiler: $isccPath; version output: $compilerVersion"
}
Write-Verbose "Using Inno Setup $compilerVersion at $isccPath"
if (!$PackageName) {
    $PackageName = if ($SkipRuntimes) { "Fcitx5-$PackageVersion-x64-no-runtime-setup" } else { "Fcitx5-$PackageVersion-x64-setup" }
}
$bundleRuntimes = if ($SkipRuntimes) { 0 } else { 1 }
$manifest = Join-Path $Output "$PackageName.manifest.txt"
New-Item -ItemType Directory -Force $Output | Out-Null
$license = Join-Path $Prefix 'licenses/Fcitx5-Windows-LICENSE.txt'
New-Item -ItemType Directory -Force (Split-Path $license) | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $license -Force
foreach ($source in @(
        @{ Path = 'fcitx5/COPYING'; Name = 'fcitx5-COPYING' },
        @{ Path = 'libime/LICENSE'; Name = 'libime-LICENSE' },
        @{ Path = 'chinese-addons/LICENSE'; Name = 'chinese-addons-LICENSE' },
        @{ Path = 'windows-cross/LICENSE'; Name = 'windows-cross-LICENSE' })) {
    $sourcePath = Join-Path $root $source.Path
    if (Test-Path -LiteralPath $sourcePath -PathType Leaf) {
        Copy-Item -LiteralPath $sourcePath -Destination (Join-Path (Split-Path $license) $source.Name) -Force
    }
}
Set-Content -LiteralPath (Join-Path $Prefix 'setup/managed-install') -Value 'FCITX5-MANAGED-1' -NoNewline -Encoding ascii
@(
    "version=$PackageVersion"
    "package=$PackageName"
    "bundle-runtimes=$bundleRuntimes"
    "commit=$((& git -C $root rev-parse HEAD).Trim())"
    "prefix=$Prefix"
    "inno=$compilerVersion"
) | Set-Content -LiteralPath $manifest -Encoding ascii
& $isccPath "/DPrefix=$Prefix" "/DDependencies=$Dependencies" "/DBundleRuntimes=$bundleRuntimes" "/DOutputDirectory=$Output" "/DPackageVersion=$PackageVersion" "/DPackageName=$PackageName" (Join-Path $root 'installer/fcitx5.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)." }
$setup = Join-Path $Output "$PackageName.exe"
if (!(Test-Path -LiteralPath $setup -PathType Leaf)) { throw "Installer was not produced: $setup" }
Get-FileHash -LiteralPath $setup -Algorithm SHA256 | ForEach-Object {
    "$($_.Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($setup))"
} | Set-Content -LiteralPath "$setup.sha256" -Encoding ascii
Write-Output $setup
