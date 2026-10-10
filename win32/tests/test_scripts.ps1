[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$helpers = Join-Path $root 'win32/scripts'
$temporary = Join-Path $root "build/script-test $([guid]::NewGuid().ToString('N'))'"
[void][IO.Directory]::CreateDirectory($temporary)
$testState = [pscustomobject]@{
    Dll = Join-Path $temporary 'fcitx5-x86_64.dll'
    Starts = @(); Stops = @(); ExitCode = 0; Owners = @()
}
[IO.File]::WriteAllText($testState.Dll, 'test fixture; never loaded')

function Assert-True {
    param([bool]$Value, [string]$Message)
    if (!$Value) { throw $Message }
}

function Assert-Fails {
    param([scriptblock]$Action)
    $failed = $false
    try { & $Action | Out-Null } catch { $failed = $true }
    Assert-True $failed 'Expected script failure.'
}

# All mutation commands and process discovery are replaced in this script scope.
function Start-Process {
    param($FilePath, $ArgumentList, $Wait, $PassThru, $WindowStyle, $Verb, $ErrorAction)
    $testState.Starts += [pscustomobject]@{
        FilePath = $FilePath; Arguments = $ArgumentList; Wait = $Wait
        PassThru = $PassThru; WindowStyle = $WindowStyle; Verb = $Verb
    }
    return [pscustomobject]@{ ExitCode = $testState.ExitCode }
}

function Stop-Process {
    param($InputObject, [switch]$Force)
    $testState.Stops += $InputObject.Id
}

function Get-Process {
    param($Id, $ErrorAction)
    if ($PSBoundParameters.ContainsKey('Id')) {
        return $testState.Owners | Where-Object Id -EQ $Id
    }
    return $testState.Owners
}

function tasklist.exe {
    foreach ($owner in $testState.Owners) {
        '"{0}","{1}"' -f $owner.ProcessName, $owner.Id
    }
}

function Get-Command {
    param($Name, $ErrorAction)
    return $null
}

try {
    foreach ($name in @('tsf-common', 'install', 'uninstall', 'release-tsf')) {
        $tokens = $null
        $parseErrors = $null
        [void][Management.Automation.Language.Parser]::ParseFile(
            (Join-Path $helpers "$name.ps1"), [ref]$tokens, [ref]$parseErrors)
        Assert-True (!$parseErrors.Count) "Parse errors in $name.ps1"
    }
    . "$helpers/tsf-common.ps1"
    Push-Location $temporary
    try {
        $defaultDll = Resolve-TsfDll
        Assert-True ($defaultDll -ieq (Join-Path $root 'dist/pinyin/tsf/fcitx5-x86_64.dll')) `
            'Default DLL depended on the working directory.'
        Assert-True ((Resolve-TsfDll -DllPath './fcitx5-x86_64.dll') -eq $testState.Dll) `
            'Explicit relative DLL path did not resolve.'
        Assert-True ((Resolve-TsfDll -Prefix (Join-Path $root 'dist/pinyin')) -eq $defaultDll) `
            'Deployment prefix did not resolve.'
        & "$helpers/install.ps1" -WhatIf
        & "$helpers/uninstall.ps1" -WhatIf
    } finally {
        Pop-Location
    }
    Assert-True (!$testState.Starts.Count) '-WhatIf launched regsvr32.'
    & "$helpers/install.ps1" -DllPath $testState.Dll | Out-Null
    & "$helpers/uninstall.ps1" -DllPath $testState.Dll | Out-Null
    Assert-True ($testState.Starts.Count -eq 2) 'Registration commands were not dispatched.'
    Assert-True ($testState.Starts[0].Arguments -eq "/s `"$($testState.Dll)`"") 'Register arguments changed.'
    Assert-True ($testState.Starts[1].Arguments -eq "/s /u `"$($testState.Dll)`"") 'Unregister arguments changed.'
    foreach ($start in $testState.Starts) {
        Assert-True ($start.FilePath -match '\\(System32|Sysnative)\\regsvr32.exe$') 'Wrong registration binary.'
        Assert-True ($start.Wait -and $start.PassThru -and $start.WindowStyle -eq 'Hidden') `
            'Registration did not wait for the hidden process.'
    }
    $testState.ExitCode = 5
    Assert-Fails { & "$helpers/install.ps1" -DllPath $testState.Dll }
    Assert-Fails { & "$helpers/uninstall.ps1" -DllPath $testState.Dll }
    $testState.Starts = @()
    foreach ($name in @('install', 'uninstall', 'release-tsf')) {
        Assert-Fails { & "$helpers/$name.ps1" -Prefix $root -DllPath $testState.Dll -WhatIf }
        Assert-Fails { & "$helpers/$name.ps1" -DllPath "$temporary/missing.dll" -WhatIf }
        Assert-Fails { & "$helpers/$name.ps1" -DllPath $temporary -WhatIf }
        Assert-Fails { & "$helpers/$name.ps1" -DllPath "$helpers/tsf-common.ps1" -WhatIf }
    }
    Assert-True (!$testState.Starts.Count) 'Invalid parameters launched a process.'
    $testState.Owners = @(
        [pscustomobject]@{ Id = 41001; ProcessName = 'ctfmon'; Modules = @(
            [pscustomobject]@{ FileName = $testState.Dll }) },
        [pscustomobject]@{ Id = 41002; ProcessName = 'other-build'; Modules = @(
            [pscustomobject]@{ FileName = "$root/other/fcitx5-x86_64.dll" }) },
        [pscustomobject]@{ Id = 41003; ProcessName = 'unknown-path'; Modules = @() }
    )
    & "$helpers/release-tsf.ps1" -DllPath $testState.Dll -HandlePath 'missing-handle.exe' -Force -WhatIf
    Assert-True (!$testState.Starts.Count -and !$testState.Stops.Count) 'Release preview changed processes.'
    Assert-Fails { & "$helpers/release-tsf.ps1" -DllPath $testState.Dll -SkipUnregister }
    Assert-True (!$testState.Stops.Count) 'Release without Force stopped a process.'
    $testState.Owners[0].ProcessName = 'test-owner'
    & "$helpers/release-tsf.ps1" -DllPath $testState.Dll -SkipUnregister -Force
    Assert-True ($testState.Stops.Count -eq 1 -and $testState.Stops[0] -eq 41001) `
        'Release selected an unverified owner or a different DLL path.'
    Assert-True (!$testState.Starts.Count) 'SkipUnregister started a process.'
    Write-Output 'PASS: script parsing, paths, quoting, registration exit codes, WhatIf and exact-path owner selection (mocked system mutations).'
} finally {
    Remove-Item -LiteralPath $testState.Dll -Force
    Remove-Item -LiteralPath $temporary
}
