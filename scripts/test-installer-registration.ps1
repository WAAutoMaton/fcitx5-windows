[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$entry = Join-Path $PSScriptRoot 'test-installer.ps1'
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($entry, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw "Parse error in $entry" }

# Import only the read-only assertions; never run an installer or edit HKLM.
foreach ($name in @('Assert-Path', 'Assert-TsfRegistration')) {
    $definition = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $true)
    if (!$definition) { throw "Missing assertion: $name" }
    . ([scriptblock]::Create($definition.Extent.Text))
}

$clsid = '{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}'
$tip = "HKLM:\SOFTWARE\Microsoft\CTF\TIP\$clsid"
$server = "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32"
$profile = "$tip\LanguageProfile\0x00000804\{9A92B895-29B9-4F19-9627-9F626C9490F2}"
$dll = 'C:\Program Files\Fcitx5\tsf\fcitx5-x86_64.dll'
$registry = @{
    $server = [pscustomobject]@{ '(default)' = $dll; ThreadingModel = 'Apartment' }
    $profile = [pscustomobject]@{ Enable = 1; IconFile = $dll; IconIndex = 0 }
}
$desktopCategories = @(
    '{34745C63-B2F0-4784-8B67-5E12C8701A31}',
    '{046B8C80-1647-40F7-9B21-B93B81AABC1B}',
    '{25504FB4-7BAB-4BC1-9C69-CF81890F0EF5}'
)
$modernCategories = @(
    '{49D2F9CF-1F5E-11D7-A6D3-00065B84435C}',
    '{13A016DF-560B-46CD-947A-4C3AF1E0E35D}'
)

function Test-Path {
    param([string]$LiteralPath)
    return $registry.ContainsKey($LiteralPath)
}

function Get-ItemProperty {
    param([string]$LiteralPath)
    if (!$registry.ContainsKey($LiteralPath)) { throw "Unexpected registry read: $LiteralPath" }
    return $registry[$LiteralPath]
}

function Assert-Failure([scriptblock]$Action, [string]$Message) {
    $failure = $null
    try { & $Action | Out-Null } catch { $failure = $_.Exception.Message }
    if (!$failure -or $failure -notlike "*$Message*") {
        throw "Expected failure containing '$Message', got '$failure'."
    }
}

foreach ($category in $desktopCategories) {
    $registry["$tip\Category\Category\$category\$clsid"] = $true
    $registry["$tip\Category\Item\$clsid\$category"] = $true
}
Assert-TsfRegistration -DllPath $dll
# SECUREMODE is a different capability and cannot stand in for UIElement.
$secureMode = '{49D2F9CE-1F5E-11D7-A6D3-00065B84435C}'
$registry["$tip\Category\Category\$secureMode\$clsid"] = $true
$registry["$tip\Category\Item\$clsid\$secureMode"] = $true
Assert-Failure { Assert-TsfRegistration -DllPath $dll -ModernApps } 'Missing TSF category UIELEMENTENABLED'
$registry.Remove("$tip\Category\Category\$secureMode\$clsid")
$registry.Remove("$tip\Category\Item\$clsid\$secureMode")
foreach ($category in $modernCategories) {
    $registry["$tip\Category\Category\$category\$clsid"] = $true
    $registry["$tip\Category\Item\$clsid\$category"] = $true
}
Assert-TsfRegistration -DllPath $dll -ModernApps

foreach ($category in @($desktopCategories) + @($modernCategories)) {
    foreach ($path in @("$tip\Category\Category\$category\$clsid",
            "$tip\Category\Item\$clsid\$category")) {
        $registry.Remove($path)
        Assert-Failure { Assert-TsfRegistration -DllPath $dll -ModernApps } 'Missing TSF category'
        $registry[$path] = $true
    }
}
foreach ($path in @($server, $profile)) {
    $saved = $registry[$path]
    $registry.Remove($path)
    Assert-Failure { Assert-TsfRegistration -DllPath $dll -ModernApps } 'Expected installed path is missing'
    $registry[$path] = $saved
}
$registry[$server].'(default)' = 'C:\old\fcitx5-x86_64.dll'
Assert-Failure { Assert-TsfRegistration -DllPath $dll -ModernApps } 'TSF COM registration'
$registry[$server].'(default)' = $dll
$registry[$server].ThreadingModel = 'Both'
Assert-Failure { Assert-TsfRegistration -DllPath $dll -ModernApps } 'TSF COM registration'
$registry[$server].ThreadingModel = 'Apartment'
$registry[$profile].Enable = 0
Assert-Failure { Assert-TsfRegistration -DllPath $dll -ModernApps } 'TSF language profile'
$registry[$profile].Enable = 1
$registry[$profile].IconFile = 'C:\old\fcitx5-x86_64.dll'
Assert-Failure { Assert-TsfRegistration -DllPath $dll -ModernApps } 'TSF language profile'
$registry[$profile].IconFile = $dll
$registry[$profile].IconIndex = 1
Assert-Failure { Assert-TsfRegistration -DllPath $dll -ModernApps } 'TSF language profile'
$registry[$profile].IconIndex = 0
Assert-TsfRegistration -DllPath $dll.ToLowerInvariant() -ModernApps
$x86Tip = "HKLM:\SOFTWARE\Wow6432Node\Microsoft\CTF\TIP\$clsid"
$x86Server = "HKLM:\SOFTWARE\Classes\Wow6432Node\CLSID\$clsid\InprocServer32"
$x86Profile = "$x86Tip\LanguageProfile\0x00000804\{9A92B895-29B9-4F19-9627-9F626C9490F2}"
$x86Dll = 'C:\Program Files\Fcitx5\tsf\x86\fcitx5-x86.dll'
$registry[$x86Server] = [pscustomobject]@{ '(default)' = $x86Dll; ThreadingModel = 'Apartment' }
$registry[$x86Profile] = [pscustomobject]@{ Enable = 1; IconFile = $x86Dll; IconIndex = 0 }
foreach ($category in @($desktopCategories) + @($modernCategories)) {
    $registry["$x86Tip\Category\Category\$category\$clsid"] = $true
    $registry["$x86Tip\Category\Item\$clsid\$category"] = $true
}
Assert-TsfRegistration -DllPath $x86Dll -Architecture x86 -ModernApps
$registry[$x86Server].'(default)' = $dll
Assert-Failure { Assert-TsfRegistration -DllPath $x86Dll -Architecture x86 -ModernApps } 'TSF COM registration'
Assert-TsfRegistration -DllPath $dll -ModernApps
Write-Output 'PASS: desktop/modern categories, both category indexes, COM path/threading and enabled profile/icon checks (mock registry; no installation).'
