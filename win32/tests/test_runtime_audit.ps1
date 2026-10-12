[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$StaticDllPath,
    [Parameter(Mandatory)][string]$DynamicDllPath,
    [Parameter(Mandatory)][string]$DelayDllPath
)

$ErrorActionPreference = 'Stop'
$checker = Join-Path $PSScriptRoot 'check-tsf-runtime.ps1'
& $checker -DllPath $StaticDllPath
foreach ($dll in @($DynamicDllPath, $DelayDllPath)) {
    $failure = $null
    try { & $checker -DllPath $dll | Out-Null }
    catch { $failure = $_.Exception.Message }
    if (!$failure -or $failure -notlike '*imports:*MSVCP140*.dll*') {
        throw "Expected MSVCP140.dll rejection for '$dll', got '$failure'."
    }
}
Write-Output 'PASS: runtime audit accepts static CRT and rejects normal and delayed MSVCP140 imports.'
