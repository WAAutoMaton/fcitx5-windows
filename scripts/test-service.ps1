[CmdletBinding()]
param([string]$Prefix = '', [string]$TsfBuild = '', [switch]$WithSettings)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Prefix) { $Prefix = "$root/dist/pinyin" }
if (!$TsfBuild) { $TsfBuild = "$root/win32/build/pinyin-tsf" }
if (Get-Process Fcitx5,Fcitx5Settings -ErrorAction SilentlyContinue) {
    throw 'Stop existing Core and settings processes before this isolated service test.'
}
$probe = (Resolve-Path -LiteralPath "$TsfBuild/tests/service_probe.exe").Path
$dll = (Resolve-Path -LiteralPath "$Prefix/tsf/fcitx5-x86_64.dll").Path
$probeArguments = @($dll)
if ($WithSettings) { $probeArguments += '--settings' }
& $probe @probeArguments
if ($LASTEXITCODE -ne 0) { throw 'TSF service lifecycle validation failed.' }
