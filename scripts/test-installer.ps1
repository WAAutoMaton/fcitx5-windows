[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,
    [string]$InstallRoot = '',
    [string]$LogPath = '',
    [switch]$ModernApps
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
$tipKey = 'HKLM:\SOFTWARE\Microsoft\CTF\TIP\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}'
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

function Assert-TsfRegistration {
    param([string]$DllPath, [switch]$ModernApps,
        [ValidateSet('x64', 'x86')][string]$Architecture = 'x64',
        [string]$ProfileDllPath = '')

    $clsid = '{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}'
    $tip = "HKLM:\SOFTWARE\Microsoft\CTF\TIP\$clsid"
    $server = "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32"
    if (!$ProfileDllPath) { $ProfileDllPath = $DllPath }
    if ($Architecture -eq 'x86') {
        $server = "HKLM:\SOFTWARE\Classes\Wow6432Node\CLSID\$clsid\InprocServer32"
    }
    Assert-Path $server
    $properties = Get-ItemProperty -LiteralPath $server
    if ($properties.'(default)' -ine $DllPath -or $properties.ThreadingModel -ine 'Apartment') {
        throw 'TSF COM registration does not match the installed DLL and Apartment threading model.'
    }

    $profile = "$tip\LanguageProfile\0x00000804\{9A92B895-29B9-4F19-9627-9F626C9490F2}"
    Assert-Path $profile
    $properties = Get-ItemProperty -LiteralPath $profile
    if ($properties.Enable -ne 1 -or $properties.IconFile -ine $ProfileDllPath -or $properties.IconIndex -ne 0) {
        throw 'TSF language profile is disabled or does not reference the installed DLL icon.'
    }

    $categories = [ordered]@{
        KEYBOARD = '{34745C63-B2F0-4784-8B67-5E12C8701A31}'
        DISPLAYATTRIBUTEPROVIDER = '{046B8C80-1647-40F7-9B21-B93B81AABC1B}'
        SYSTRAYSUPPORT = '{25504FB4-7BAB-4BC1-9C69-CF81890F0EF5}'
    }
    if ($ModernApps) {
        $categories.UIELEMENTENABLED = '{49D2F9CF-1F5E-11D7-A6D3-00065B84435C}'
        $categories.IMMERSIVESUPPORT = '{13A016DF-560B-46CD-947A-4C3AF1E0E35D}'
    }
    foreach ($category in $categories.GetEnumerator()) {
        # Check both indexes written by ITfCategoryMgr, not only COM registration.
        foreach ($path in @("$tip\Category\Category\$($category.Value)\$clsid",
                "$tip\Category\Item\$clsid\$($category.Value)")) {
            if (!(Test-Path -LiteralPath $path)) {
                throw "Missing TSF category $($category.Key): $path"
            }
        }
    }
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
    if (Test-Path -LiteralPath $tipKey) {
        throw 'An existing Fcitx5 TIP registration is present.'
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
            (Join-Path $InstallRoot 'tsf/x86/fcitx5-x86.dll'),
            (Join-Path $InstallRoot 'settings/Fcitx5Settings.exe'),
            $helper,
            (Join-Path $InstallRoot 'setup/maintenance-host/Fcitx5Upgrade.exe'),
            (Join-Path $InstallRoot 'setup/runtime-manifest.json'),
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
    $x86Dll = Join-Path $InstallRoot 'tsf/x86/fcitx5-x86.dll'
    $x64Dll = Join-Path $InstallRoot 'tsf/fcitx5-x86_64.dll'
    # CTF TIP profile/category keys are shared; COM servers remain view-specific.
    # The x86 registrar runs first in the machine registration helper.
    Assert-TsfRegistration -DllPath $x64Dll -ProfileDllPath $x86Dll -ModernApps:$ModernApps
    Assert-TsfRegistration -DllPath $x86Dll -Architecture x86 `
        -ProfileDllPath $x86Dll -ModernApps:$ModernApps

    # Each registrar must leave the other architecture's registration usable.
    $x86Registrar = Join-Path $env:WINDIR 'SysWOW64/regsvr32.exe'
    $x64Registrar = Join-Path $env:WINDIR 'System32/regsvr32.exe'
    Invoke-ProcessChecked -FilePath $x86Registrar -ArgumentList @('/s', '/u', "`"$x86Dll`"") | Out-Null
    Assert-TsfRegistration -DllPath $x64Dll -ProfileDllPath $x86Dll -ModernApps:$ModernApps
    Invoke-ProcessChecked -FilePath $x86Registrar -ArgumentList @('/s', "`"$x86Dll`"") | Out-Null
    Invoke-ProcessChecked -FilePath $x64Registrar -ArgumentList @('/s', '/u', "`"$x64Dll`"") | Out-Null
    Assert-TsfRegistration -DllPath $x86Dll -Architecture x86 -ProfileDllPath $x86Dll -ModernApps:$ModernApps
    Invoke-ProcessChecked -FilePath $x64Registrar -ArgumentList @('/s', "`"$x64Dll`"") | Out-Null
    Assert-TsfRegistration -DllPath $x64Dll -ProfileDllPath $x86Dll -ModernApps:$ModernApps

    $uninstallArguments = @('uninstall', '--silent', '--prefix', "`"$InstallRoot`"")
    $uninstallCode = Invoke-ProcessChecked -FilePath $helper -ArgumentList $uninstallArguments -AllowedExitCodes @(0, 3010)
    Start-Sleep -Milliseconds 500
    if (Test-Path -LiteralPath $installerKey) {
        throw 'Installer registry state remained after uninstall.'
    }
    if (Test-Path -LiteralPath $uninstallKey) {
        throw 'Windows uninstall registration remained after uninstall.'
    }
    if ((Test-Path -LiteralPath $clsidKey) -or (Test-Path -LiteralPath $tipKey) -or
        (Test-Path -LiteralPath "HKLM:\SOFTWARE\Classes\Wow6432Node\CLSID\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}") -or
        (Test-Path -LiteralPath "HKLM:\SOFTWARE\Wow6432Node\Microsoft\CTF\TIP\{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}")) {
        throw 'TSF COM or TIP registration remained after uninstall.'
    }
    if (Test-Path -LiteralPath $InstallRoot) {
        $remaining = Get-ChildItem -LiteralPath $InstallRoot -Force -Recurse -ErrorAction SilentlyContinue
        $unexpected = @($remaining | Where-Object {
                !$_.PSIsContainer -and $_.Name -notin @('Fcitx5SetupHelper.exe', 'Fcitx5Upgrade.exe') -and
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
