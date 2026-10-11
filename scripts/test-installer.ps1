[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,
    [string]$InstallRoot = '',
    [string]$LogPath = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$installer = (Resolve-Path -LiteralPath $InstallerPath).Path
if ([IO.Path]::GetExtension($installer) -ine '.exe') {
    throw "InstallerPath must point to an .exe: $installer"
}

$testId = [guid]::NewGuid().ToString('N')
if (!$InstallRoot) {
    $InstallRoot = Join-Path $root "build/installer-smoke/$testId/Fcitx5 install"
}
$InstallRoot = [IO.Path]::GetFullPath($InstallRoot)
$rootFull = ([IO.Path]::GetFullPath($root)).TrimEnd([IO.Path]::DirectorySeparatorChar)
if (!$InstallRoot.StartsWith($rootFull + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "InstallRoot must be inside the repository: $InstallRoot"
}
if (Test-Path -LiteralPath $InstallRoot) {
    throw "InstallRoot already exists: $InstallRoot"
}

if (!$LogPath) {
    $LogPath = Join-Path $root "build/installer-smoke/$testId/installer.log"
}
$LogPath = [IO.Path]::GetFullPath($LogPath)
New-Item -ItemType Directory -Force (Split-Path $LogPath -Parent) | Out-Null
New-Item -ItemType Directory -Force (Split-Path $InstallRoot -Parent) | Out-Null

$installerKey = 'HKLM:\SOFTWARE\Fcitx5\Installer'
$uninstallKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{0D07698A-591B-44B4-940F-BBD38AA62F0E}_is1'
$clsidKey = 'HKLM:\SOFTWARE\Classes\CLSID\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}\InprocServer32'
$helper = Join-Path $InstallRoot 'setup/Fcitx5SetupHelper.exe'
$installed = $false

function Invoke-ProcessChecked {
    param(
        [Parameter(Mandatory = $true)] [string]$FilePath,
        [Parameter(Mandatory = $true)] [string[]]$ArgumentList,
        [int[]]$AllowedExitCodes = @(0)
    )
    $process = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList -WindowStyle Hidden -Wait -PassThru
    if ($AllowedExitCodes -notcontains $process.ExitCode) {
        throw "Process failed ($($process.ExitCode)): $FilePath $($ArgumentList -join ' ')"
    }
    return $process.ExitCode
}

function Assert-Path {
    param([string]$Path)
    if (!(Test-Path -LiteralPath $Path)) { throw "Expected installed path is missing: $Path" }
}

function Get-InstallerRoot {
    if (!(Test-Path -LiteralPath $installerKey)) { return '' }
    return [string](Get-ItemProperty -LiteralPath $installerKey -Name InstallRoot -ErrorAction SilentlyContinue).InstallRoot
}

try {
    $existingRoot = Get-InstallerRoot
    if ($existingRoot) {
        throw "An existing Fcitx5 installation is registered at $existingRoot"
    }
    if (Test-Path -LiteralPath $uninstallKey) {
        throw 'An existing Fcitx5 Windows uninstall registration is present.'
    }
    if (Test-Path -LiteralPath $clsidKey) {
        throw 'An existing Fcitx5 COM registration is present.'
    }

    $installed = $true
    $installArguments = @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/MACHINEONLY=1',
        "/LOG=`"$LogPath`"", "/DIR=`"$InstallRoot`""
    )
    Invoke-ProcessChecked -FilePath $installer -ArgumentList $installArguments -AllowedExitCodes @(0, 3010) | Out-Null

    foreach ($path in @(
            (Join-Path $InstallRoot 'bin/Fcitx5.exe'),
            (Join-Path $InstallRoot 'tsf/fcitx5-x86_64.dll'),
            (Join-Path $InstallRoot 'settings/Fcitx5Settings.exe'),
            $helper,
            (Join-Path $InstallRoot 'setup/managed-install'),
            (Join-Path $InstallRoot 'setup/install-state'))) {
        Assert-Path $path
    }
    if ((Get-Content -LiteralPath (Join-Path $InstallRoot 'setup/install-state') -Raw).Trim() -ne 'installed') {
        throw 'Installer did not leave the expected installed state.'
    }

    $properties = Get-ItemProperty -LiteralPath $installerKey
    if ([IO.Path]::GetFullPath([string]$properties.InstallRoot) -ine $InstallRoot) {
        throw "Installer registry root does not match the install directory: $($properties.InstallRoot)"
    }
    Assert-Path ([string]$properties.NativeUninstaller)
    Assert-Path $uninstallKey

    $uninstallArguments = @('uninstall', '--silent', '--prefix', "`"$InstallRoot`"")
    $uninstallCode = Invoke-ProcessChecked -FilePath $helper -ArgumentList $uninstallArguments -AllowedExitCodes @(0, 3010)
    Start-Sleep -Milliseconds 500
    if (Test-Path -LiteralPath $installerKey) {
        throw 'Installer registry state remained after uninstall.'
    }
    if (Test-Path -LiteralPath $uninstallKey) {
        throw 'Windows uninstall registration remained after uninstall.'
    }
    if (Test-Path -LiteralPath $InstallRoot) {
        $remaining = Get-ChildItem -LiteralPath $InstallRoot -Force -Recurse -ErrorAction SilentlyContinue
        $unexpected = @($remaining | Where-Object {
                !$_.PSIsContainer -and $_.Name -ne 'Fcitx5SetupHelper.exe' -and
                !$_.Name.StartsWith('unins', [StringComparison]::OrdinalIgnoreCase)
            })
        if ($unexpected) {
            throw "Installer left payload files after uninstall (exit code $uninstallCode): $($unexpected[0].FullName)"
        }
        if ($remaining) { Write-Warning 'Uninstaller left only self-cleanup files; Windows may remove them after process exit or restart.' }
    }
    $installed = $false
    Write-Output "Installer smoke test passed: install and uninstall completed (uninstall exit code $uninstallCode)."
}
finally {
    if ($installed -and (Test-Path -LiteralPath $helper)) {
        try {
            Invoke-ProcessChecked -FilePath $helper -ArgumentList @('uninstall', '--silent', '--prefix', "`"$InstallRoot`"") -AllowedExitCodes @(0, 3010) | Out-Null
        } catch {
            Write-Warning "Cleanup uninstall failed: $($_.Exception.Message)"
        }
    }
    if (Test-Path -LiteralPath $InstallRoot) {
        Remove-Item -LiteralPath $InstallRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
