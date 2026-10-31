[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$Prefix = '',
    [string]$Dependencies = '',
    [string]$Output = '',
    [string]$PackageVersion = '0.1.0.0',
    [string]$PackageName = '',
    [string]$IsccPath = '',
    [switch]$SkipRuntimes,
    [switch]$RebootUpgrade = $true
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
foreach ($file in @('bin/Fcitx5.exe', 'tsf/fcitx5-x86_64.dll', 'tsf/x86/fcitx5-x86.dll',
        'setup/Fcitx5SetupHelper.exe', 'setup/Fcitx5Upgrade.exe', 'settings/Fcitx5Settings.exe')) {
    if (!(Test-Path -LiteralPath (Join-Path $Prefix $file) -PathType Leaf)) {
        throw "Missing staged installer file: $file"
    }
}
if ($PackageVersion -notmatch '^\d{1,5}\.\d{1,5}\.\d{1,5}\.\d{1,5}$' -or
    @($PackageVersion.Split('.') | Where-Object { [int]$_ -gt 65535 }).Count) {
    throw 'PackageVersion must have four numeric components in the range 0..65535.'
}
if ($PackageName -and $PackageName -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$') {
    throw 'PackageName must be a simple ASCII package basename without directory components.'
}
if (!$SkipRuntimes) {
    foreach ($file in @('vc_redist.x64.exe', 'WindowsAppRuntimeInstall-x64.exe')) {
        if (!(Test-Path -LiteralPath (Join-Path $Dependencies $file) -PathType Leaf)) {
            throw "Missing installer dependency: $file"
        }
    }
}
if (!$PSCmdlet.ShouldProcess($Output, 'Build Fcitx5 Inno Setup installer')) { return }
. "$root/win32/scripts/pe-machine.ps1"
Assert-PeMachine "$Prefix/tsf/fcitx5-x86_64.dll" x64
Assert-PeMachine "$Prefix/tsf/x86/fcitx5-x86.dll" x86
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
        @{ Path = 'plugins/chinese-addons/LICENSE'; Name = 'chinese-addons-LICENSE' },
        @{ Path = 'windows-cross/LICENSE'; Name = 'windows-cross-LICENSE' })) {
    $sourcePath = Join-Path $root $source.Path
    if (Test-Path -LiteralPath $sourcePath -PathType Leaf) {
        Copy-Item -LiteralPath $sourcePath -Destination (Join-Path (Split-Path $license) $source.Name) -Force
    }
}
Set-Content -LiteralPath (Join-Path $Prefix 'setup/managed-install') -Value "FCITX5-MANAGED-1`n" -NoNewline -Encoding ascii
$metadata = Join-Path $Output 'metadata'
New-Item -ItemType Directory -Force $metadata | Out-Null
$runtimeManifest = Join-Path $metadata "$PackageName.runtime.json"
$runtimeFiles = Join-Path $metadata "$PackageName.files.iss"
$runtime = @()
foreach ($directory in @('bin', 'lib', 'share', 'tsf', 'settings', 'licenses', 'plugins')) {
    $path = Join-Path $Prefix $directory
    if (Test-Path -LiteralPath $path) {
        $linked = @(Get-ChildItem -LiteralPath $path -Recurse -Force | Where-Object {
            $_.Attributes -band [IO.FileAttributes]::ReparsePoint
        })
        if ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint -or $linked.Count) {
            throw "Linked payload directory: $directory"
        }
        $runtime += @(Get-ChildItem -LiteralPath $path -File -Recurse)
    }
}
$runtime += Get-Item -LiteralPath "$Prefix/setup/Fcitx5SetupHelper.exe", "$Prefix/setup/managed-install"
$prefixBase = $Prefix.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
$records = @($runtime | Sort-Object FullName | ForEach-Object {
    if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked payload file: $($_.Name)" }
    $relative = $_.FullName.Substring($prefixBase.Length).Replace('\', '/')
    [ordered]@{ path = $relative; size = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
$dependencyRecords = @()
if (!$SkipRuntimes) {
    $dependencyRecords = @(foreach ($name in @('vc_redist.x64.exe', 'WindowsAppRuntimeInstall-x64.exe')) {
        $file = Get-Item -LiteralPath (Join-Path $Dependencies $name)
        [ordered]@{ path = $name; size = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}
$protocol = Select-String -LiteralPath "$root/win32/ipc/protocol.h" -Pattern 'constexpr uint16_t kVersion = (\d+);'
if (!$protocol) { throw 'Cannot determine the IPC protocol version.' }
$document = [ordered]@{
    schemaVersion = 1; architecture = 'x64'; protocolVersion = [int]$protocol.Matches[0].Groups[1].Value
    version = $PackageVersion; package = $PackageName; files = $records; dependencies = $dependencyRecords
}
$utf8 = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText($runtimeManifest, ($document | ConvertTo-Json -Depth 6), $utf8)
$entries = @($records | ForEach-Object {
    $source = Join-Path $Prefix $_.path
    $parent = Split-Path $_.path -Parent
    $destination = '{code:RuntimeDestination}'
    if ($parent) { $destination += '\' + $parent.Replace('/', '\') }
    'Source: "' + $source.Replace('"', '""') + '"; DestDir: "' + $destination + '"; Flags: ignoreversion uninsrestartdelete; Check: CopyRuntimeFiles'
})
[IO.File]::WriteAllLines($runtimeFiles, $entries, $utf8)
@(
    "version=$PackageVersion"
    "package=$PackageName"
    "bundle-runtimes=$bundleRuntimes"
    "commit=$((& git -C $root rev-parse HEAD).Trim())"
    "prefix=$Prefix"
    "inno=$compilerVersion"
    "reboot-upgrade=$([int]$RebootUpgrade.IsPresent)"
) | Set-Content -LiteralPath $manifest -Encoding ascii
& $isccPath "/DPrefix=$Prefix" "/DDependencies=$Dependencies" "/DBundleRuntimes=$bundleRuntimes" "/DRuntimeManifest=$runtimeManifest" "/DRuntimeFiles=$runtimeFiles" "/DRebootUpgrade=$([int]$RebootUpgrade.IsPresent)" "/DOutputDirectory=$Output" "/DPackageVersion=$PackageVersion" "/DPackageName=$PackageName" (Join-Path $root 'installer/fcitx5.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)." }
$setup = Join-Path $Output "$PackageName.exe"
if (!(Test-Path -LiteralPath $setup -PathType Leaf)) { throw "Installer was not produced: $setup" }
Get-FileHash -LiteralPath $setup -Algorithm SHA256 | ForEach-Object {
    "$($_.Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($setup))"
} | Set-Content -LiteralPath "$setup.sha256" -Encoding ascii
Write-Output $setup
