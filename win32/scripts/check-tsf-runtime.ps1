[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DllPath,
    [string]$ReadObjPath = '',
    [ValidateSet('x86', 'x64')][string]$Architecture = ''
)

$ErrorActionPreference = 'Stop'
$dll = (Resolve-Path -LiteralPath $DllPath -ErrorAction Stop).Path
if ($Architecture) {
    . "$PSScriptRoot/pe-machine.ps1"
    Assert-PeMachine $dll $Architecture
}
if (!$ReadObjPath) {
    $reader = Get-Command llvm-readobj.exe -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($reader) { $ReadObjPath = $reader.Source }
    else { $ReadObjPath = Join-Path $env:ProgramFiles 'LLVM/bin/llvm-readobj.exe' }
}
$output = & $ReadObjPath --coff-imports $dll
if ($LASTEXITCODE -ne 0) { throw "PE import inspection failed for '$dll' ($LASTEXITCODE)." }
if (!($output -match '^Format: COFF-')) { throw "Expected a COFF PE image: '$dll'." }

# llvm-readobj emits both Import and DelayImport records with this option.
$imports = @($output | ForEach-Object {
    if ($_ -match '^\s+Name: (.+\.dll)\s*$') { $matches[1].Trim() }
})
if (!$imports.Count) { throw "No DLL imports were found in '$dll'." }
$forbidden = @($imports | Where-Object {
    $_ -match '^(msvcp140.*|vcruntime140.*|ucrtbase[d]?|api-ms-win-crt-.*)\.dll$'
})
if ($forbidden.Count) {
    throw "TSF must use static CRT; '$dll' imports: $($forbidden -join ', ')."
}
Write-Output "PASS: '$dll' has no dynamic MSVC/UCRT imports (including delay imports)."
