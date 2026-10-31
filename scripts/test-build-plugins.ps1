[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$temporary = Join-Path $root "build/plugin-script-test $([guid]::NewGuid().ToString('N'))'"
$entry = "$temporary/scripts/build-plugins.ps1"
$sdk = "$temporary/sdk"
$prefix = "$temporary/runtime"
$buildRoot = "$temporary/build"
$testState = @{ Builds = @{}; Calls = @(); GitCalls = @(); FailBuild = $false; FailLfs = $false }

# Simulate resource-only plugins, without executing a compiler or system actions.
function cmake {
    $testState.Calls += ,@($args)
    if ($args -contains '-S') {
        $source = $args[[Array]::IndexOf($args, '-S') + 1]
        $build = $args[[Array]::IndexOf($args, '-B') + 1]
        $stage = ($args | Where-Object { $_ -like '-DCMAKE_INSTALL_PREFIX=*' }).Substring(23)
        $testState.Builds[$build] = @{ Name = Split-Path $source -Leaf; Stage = $stage }
    } elseif ($args[0] -eq '--install') {
        $project = $testState.Builds[$args[1]]
        $data = "$($project.Stage)/share/fcitx5/inputmethod"
        New-Item -ItemType Directory -Force $data | Out-Null
        [IO.File]::WriteAllText("$data/$($project.Name).conf", 'plugin fixture')
    }
    $status = if ($testState.FailBuild -and $args[0] -eq '--build') { 9 } else { 0 }
    Set-Variable -Name LASTEXITCODE -Value $status -Scope 1
}

function git {
    $testState.GitCalls += ,@($args)
    $status = if ($testState.FailLfs) { 1 } else { 0 }
    Set-Variable -Name LASTEXITCODE -Value $status -Scope 1
}

function Assert-Failure([scriptblock]$Action, [string]$Message) {
    $failure = $null
    try { & $Action | Out-Null } catch { $failure = $_.Exception.Message }
    if (!$failure -or $failure -notlike "*$Message*") {
        throw "Expected failure containing '$Message', got '$failure'."
    }
}

try {
    New-Item -ItemType Directory -Force "$temporary/scripts", "$temporary/plugins/fcitx5-chinese-addons" | Out-Null
    Copy-Item -LiteralPath "$PSScriptRoot/build-plugins.ps1" -Destination $entry
    foreach ($name in @('Fcitx5Core', 'LibIMECore')) {
        New-Item -ItemType Directory -Force "$sdk/lib/cmake/$name" | Out-Null
        [IO.File]::WriteAllText("$sdk/lib/cmake/$name/${name}Config.cmake", 'fixture')
    }
    $environmentBefore = @(Get-ChildItem Env: | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" })
    & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot -WhatIf
    if ((Test-Path $prefix) -or (Test-Path $buildRoot) -or $testState.Calls.Count) {
        throw 'WhatIf changed the deployment or invoked CMake.'
    }
    Assert-Failure { & $entry -CorePrefix "$temporary/missing-sdk" -Prefix $prefix } 'matching installed Core/libime SDK'
    foreach ($name in @('first', 'future-plugin')) {
        $source = "$temporary/plugins/$name"
        New-Item -ItemType Directory -Force $source | Out-Null
        [IO.File]::WriteAllText("$source/CMakeLists.txt", 'fixture')
        [IO.File]::WriteAllText("$source/LICENSE", 'plugin license')
        & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot | Out-Null
        foreach ($relative in @("plugins/$name/share/fcitx5/inputmethod/$name.conf", "plugins/$name/licenses/LICENSE")) {
            if (!(Test-Path -LiteralPath "$prefix/$relative")) { throw "Missing automatically discovered plugin output: $relative" }
        }
    }
    if ($testState.Builds.Values.Name -contains 'fcitx5-chinese-addons' -or (Test-Path "$prefix/plugins/fcitx5-chinese-addons")) {
        throw 'fcitx5-chinese-addons was rebuilt as an external plugin.'
    }
    $lfsCalls = @($testState.GitCalls | ForEach-Object { $_ -join ' ' } | Where-Object { $_ -match ' lfs pull$' })
    if ($testState.GitCalls.Count -ne 3 -or $lfsCalls.Count -ne 3) {
        throw 'Every plugin build must fetch Git LFS assets before building.'
    }
    $testState.FailLfs = $true
    Assert-Failure { & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot } 'git failed (1)'
    $testState.FailLfs = $false
    $testState.FailBuild = $true
    Assert-Failure { & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot } 'cmake failed (9)'
    $testState.FailBuild = $false
    New-Item -ItemType Directory -Force "$temporary/plugins/uninitialized" | Out-Null
    Assert-Failure { & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot } 'Plugin is not initialized'
    $environmentAfter = @(Get-ChildItem Env: | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" })
    if (Compare-Object $environmentBefore $environmentAfter) { throw 'Plugin build changed the environment.' }
    Write-Output 'PASS: automatic discovery of future plugins, base exclusion, runtime/license deployment, missing SDK/submodules, build failure, WhatIf and environment restoration.'
} finally {
    $resolved = [IO.Path]::GetFullPath($temporary)
    if (!$resolved.StartsWith(($root + '\build\'), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected cleanup path: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
