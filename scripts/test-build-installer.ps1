[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$entry = Join-Path $PSScriptRoot 'build-installer.ps1'
$temporary = Join-Path $root "build/installer-script-test $([guid]::NewGuid().ToString('N'))'"
$prefix = Join-Path $temporary 'stage'
$dependencies = Join-Path $temporary 'dependencies'
$output = Join-Path $temporary 'output'
$testState = @{ CompilerCalls = @(); CompilerExitCode = 0 }

function Assert-Failure([scriptblock]$Action, [string]$Message) {
    $failure = $null
    try { & $Action | Out-Null } catch { $failure = $_.Exception.Message }
    if (!$failure -or $failure -notlike "*$Message*") {
        throw "Expected failure containing '$Message', got '$failure'."
    }
}

# Exercise packaging without executing a compiler or any installer.
function Get-Command {
    param($Name, $ErrorAction)
    if ($Name -eq 'ISCC.exe') { return [pscustomobject]@{ Source = 'Invoke-TestIscc' } }
    Microsoft.PowerShell.Core\Get-Command @PSBoundParameters
}

function Invoke-TestIscc {
    $testState.CompilerCalls += ,@($args)
    $defines = @{}
    foreach ($argument in $args) {
        if ($argument -match '^/D([^=]+)=(.*)$') { $defines[$matches[1]] = $matches[2] }
    }
    if ($testState.CompilerExitCode -eq 0) {
        Set-Content -LiteralPath (Join-Path $defines.OutputDirectory "$($defines.PackageName).exe") -Value 'mock installer' -Encoding ascii
    }
    Set-Variable -Name LASTEXITCODE -Value $testState.CompilerExitCode -Scope 1
}

try {
    $tokens = $null
    $parseErrors = $null
    [void][Management.Automation.Language.Parser]::ParseFile($entry, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count) { throw "Parse error in $entry" }
    foreach ($file in @('bin/Fcitx5.exe', 'tsf/fcitx5-x86_64.dll',
            'setup/Fcitx5SetupHelper.exe', 'settings/Fcitx5Settings.exe')) {
        $path = Join-Path $prefix $file
        New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
        Set-Content -LiteralPath $path -Value 'fixture' -Encoding ascii
    }
    Push-Location $temporary
    try {
        & $entry -Prefix './stage' -Dependencies './dependencies' -Output './output' -SkipRuntimes -WhatIf
        if ((Test-Path $output) -or (Test-Path "$prefix/setup/managed-install") -or $testState.CompilerCalls.Count) {
            throw 'WhatIf wrote packaging files or invoked the compiler.'
        }
        Assert-Failure { & $entry -Prefix $prefix -Dependencies $dependencies -Output $output -WhatIf } 'Missing installer dependency: vc_redist.x64.exe'
        & $entry -Prefix $prefix -Dependencies $dependencies -Output $output -PackageVersion '0.1.2.0' -SkipRuntimes | Out-Null
        $lite = 'Fcitx5-0.1.2.0-x64-no-runtime-setup'
        if ($testState.CompilerCalls.Count -ne 1 -or $testState.CompilerCalls[0] -notcontains '/DBundleRuntimes=0') {
            throw 'No-runtime package did not disable runtime bundling.'
        }
        if (Test-Path $dependencies) { throw 'No-runtime packaging created the dependency directory.' }
        New-Item -ItemType Directory -Force $dependencies | Out-Null
        Set-Content -LiteralPath "$dependencies/vc_redist.x64.exe" -Value 'fixture' -Encoding ascii
        Assert-Failure { & $entry -Prefix $prefix -Dependencies $dependencies -Output $output -WhatIf } 'Missing installer dependency: WindowsAppRuntimeInstall-x64.exe'
        Set-Content -LiteralPath "$dependencies/WindowsAppRuntimeInstall-x64.exe" -Value 'fixture' -Encoding ascii
        & $entry -Prefix $prefix -Dependencies $dependencies -Output $output -PackageVersion '0.1.2.0' | Out-Null
        $bundled = 'Fcitx5-0.1.2.0-x64-setup'
        if ($testState.CompilerCalls.Count -ne 2 -or $testState.CompilerCalls[1] -notcontains '/DBundleRuntimes=1') {
            throw 'Default package did not enable runtime bundling.'
        }
        foreach ($package in @($lite, $bundled)) {
            $runtimeFlag = if ($package -eq $lite) { 0 } else { 1 }
            $manifest = Get-Content -LiteralPath "$output/$package.manifest.txt"
            if ($manifest -notcontains "package=$package" -or $manifest -notcontains "bundle-runtimes=$runtimeFlag") {
                throw "Incorrect or overwritten manifest for $package."
            }
            $hash = (Get-FileHash -LiteralPath "$output/$package.exe" -Algorithm SHA256).Hash.ToLowerInvariant()
            if ((Get-Content -LiteralPath "$output/$package.exe.sha256").Trim() -ne "$hash  $package.exe") {
                throw "Incorrect checksum for $package."
            }
        }
        & $entry -Prefix $prefix -Dependencies $dependencies -Output $output -PackageName 'custom-no-runtime' -SkipRuntimes | Out-Null
        if (!(Test-Path -LiteralPath "$output/custom-no-runtime.manifest.txt")) { throw 'Custom package name was ignored.' }
        $testState.CompilerExitCode = 9
        Assert-Failure { & $entry -Prefix $prefix -Dependencies $dependencies -Output $output -SkipRuntimes } 'ISCC failed (9)'
        Write-Output 'PASS: runtime variants, missing dependencies, distinct manifests/checksums, custom names, compiler failure, arbitrary-CWD/space/quote paths and WhatIf.'
    } finally { Pop-Location }
} finally {
    $resolved = [IO.Path]::GetFullPath($temporary)
    if (!$resolved.StartsWith(($root + '\build\'), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected cleanup path: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
