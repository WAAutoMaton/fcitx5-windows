[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$entry = Join-Path $PSScriptRoot 'build-and-deploy.ps1'
$temporary = Join-Path $root "build/build-script-test $([guid]::NewGuid().ToString('N'))'"
New-Item -ItemType Directory -Force $temporary | Out-Null

function Assert-Failure([scriptblock]$Action, [string]$Message) {
    $failure = $null
    try { & $Action | Out-Null } catch { $failure = $_.Exception.Message }
    if (!$failure -or $failure -notlike "*$Message*") {
        throw "Expected failure containing '$Message', got '$failure'."
    }
}

try {
    foreach ($file in @($entry, "$PSScriptRoot/build-pinyin.ps1")) {
        $tokens = $null
        $parseErrors = $null
        [void][Management.Automation.Language.Parser]::ParseFile($file, [ref]$tokens, [ref]$parseErrors)
        if ($parseErrors.Count) { throw "Parse error in $file" }
    }
    $archiveAst = [Management.Automation.Language.Parser]::ParseFile(
        "$PSScriptRoot/build-pinyin.ps1", [ref]$tokens, [ref]$parseErrors)
    $archiveFunction = $archiveAst.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq 'Get-VerifiedArchive'
    }, $true)
    . ([scriptblock]::Create($archiveFunction.Extent.Text))
    $downloadCalls = 0
    function Invoke-Checked {
        param($Executable, $Arguments)
        ++$script:downloadCalls
        throw 'Unexpected network download in cache test.'
    }
    $validArchive = Join-Path $temporary 'libime-data.pkg.tar.zst'
    $invalidArchive = Join-Path $temporary 'invalid.pkg.tar.zst'
    $destination = Join-Path $temporary 'cached.pkg.tar.zst'
    [IO.File]::WriteAllText($validArchive, 'valid archive fixture')
    [IO.File]::WriteAllText($invalidArchive, 'wrong version fixture')
    $hash = (Get-FileHash -LiteralPath $validArchive -Algorithm SHA256).Hash
    Get-VerifiedArchive 'https://unused.invalid' $destination $hash @($invalidArchive, $validArchive)
    if ((Get-FileHash -LiteralPath $destination).Hash -ne $hash) { throw 'Local archive reuse failed.' }
    $timestamp = [IO.File]::GetLastWriteTimeUtc($destination)
    Get-VerifiedArchive 'https://unused.invalid' $destination $hash @()
    if ([IO.File]::GetLastWriteTimeUtc($destination) -ne $timestamp -or $downloadCalls) {
        throw 'Verified cache was rewritten or downloaded again.'
    }
    [IO.File]::WriteAllText($destination, 'corrupt cache')
    Get-VerifiedArchive 'https://unused.invalid' $destination $hash @($validArchive)
    if ((Get-FileHash -LiteralPath $destination).Hash -ne $hash) { throw 'Corrupt cache was not repaired.' }
    Assert-Failure { Get-VerifiedArchive 'https://unused.invalid' $destination ('0' * 64) @($invalidArchive) } 'Unexpected network download'
    if ($downloadCalls -ne 1) { throw 'Unverified local archives were accepted.' }
    Push-Location $temporary
    try {
        $buildRoot = Join-Path $temporary 'build'
        $prefix = Join-Path $temporary 'deployment'
        $environmentBefore = @(Get-ChildItem Env: | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" })
        & $entry -Prefix './deployment' -BuildRoot './build' -WhatIf
        if ((Test-Path $prefix) -or (Test-Path $buildRoot)) { throw 'WhatIf created build/deployment files.' }
        $environmentAfter = @(Get-ChildItem Env: | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" })
        if (Compare-Object $environmentBefore $environmentAfter) { throw 'WhatIf changed the environment.' }
        Assert-Failure { & $entry -Prefix $root -WhatIf } 'inside this repository'
        Assert-Failure { & $entry -Prefix "$root/../outside" -WhatIf } 'inside this repository'
        Assert-Failure { & $entry -Prefix $buildRoot -BuildRoot $buildRoot -WhatIf } 'without overlap'
        Assert-Failure { & $entry -Prefix "$buildRoot/prefix" -BuildRoot $buildRoot -WhatIf } 'without overlap'
        Assert-Failure { & $entry -Prefix $prefix -BuildRoot "$prefix/build" -WhatIf } 'without overlap'
        Assert-Failure { & $entry -Jobs 0 -WhatIf } 'Jobs'
        New-Item -ItemType Directory -Force $prefix | Out-Null
        $locked = Join-Path $prefix 'locked.dll'
        [IO.File]::WriteAllText($locked, 'fixture; never loaded')
        $handle = [IO.File]::Open($locked, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
        try {
            Assert-Failure { & $entry -Prefix $prefix -BuildRoot $buildRoot } 'file is in use or not writable'
        } finally { $handle.Dispose() }
        if (Test-Path $buildRoot) { throw 'Occupied deployment started a build.' }
        $attributes = [IO.File]::GetAttributes($locked)
        [IO.File]::SetAttributes($locked, $attributes -bor [IO.FileAttributes]::ReadOnly)
        try {
            Assert-Failure { & $entry -Prefix $prefix -BuildRoot $buildRoot } 'file is in use or not writable'
        } finally { [IO.File]::SetAttributes($locked, $attributes) }
        if ([IO.File]::ReadAllText($locked) -ne 'fixture; never loaded') { throw 'Preflight changed the file.' }
        Write-Output 'PASS: archive reuse/hash rejection/cache repair, parsing, arbitrary-CWD/space/quote paths, WhatIf, environment preservation, invalid arguments, locked and read-only deployment rejection.'
    } finally { Pop-Location }
} finally {
    # Delete only this test's verified unique workspace directory.
    $resolved = [IO.Path]::GetFullPath($temporary)
    if (!$resolved.StartsWith(($root + '\build\'), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected cleanup path: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
