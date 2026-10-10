<#
.SYNOPSIS
Registers the deployed TSF DLL. Requests elevation when necessary.
.EXAMPLE
./win32/scripts/install.ps1 -WhatIf
.EXAMPLE
./win32/scripts/install.ps1 -Prefix ./dist/pinyin
#>
[CmdletBinding(SupportsShouldProcess)]
param([string]$Prefix = '', [string]$DllPath = '')

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/tsf-common.ps1"
$dll = Resolve-TsfDll -Prefix $Prefix -DllPath $DllPath
if ($PSCmdlet.ShouldProcess($dll, 'Register TSF (administrator privileges required)')) {
    Invoke-TsfRegistration -DllPath $dll
    Write-Output "Registered $dll. Reopen host applications and select Fcitx5 with Win+Space."
}
