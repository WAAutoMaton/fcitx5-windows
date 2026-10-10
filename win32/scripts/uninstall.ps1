<#
.SYNOPSIS
Unregisters the deployed TSF DLL. Does not unload DLLs or delete deployment files.
.EXAMPLE
./win32/scripts/uninstall.ps1 -WhatIf
.EXAMPLE
./win32/scripts/uninstall.ps1 -Prefix ./dist/pinyin
#>
[CmdletBinding(SupportsShouldProcess)]
param([string]$Prefix = '', [string]$DllPath = '')

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/tsf-common.ps1"
$dll = Resolve-TsfDll -Prefix $Prefix -DllPath $DllPath
if ($PSCmdlet.ShouldProcess($dll, 'Unregister TSF (administrator privileges required)')) {
    Invoke-TsfRegistration -DllPath $dll -Unregister
    Write-Output "Unregistered $dll. Existing host applications may keep the DLL loaded. Core and settings were not stopped."
}
