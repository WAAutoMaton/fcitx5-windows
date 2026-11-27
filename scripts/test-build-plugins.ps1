[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$temporary = Join-Path $root "build/plugin-script-test $([guid]::NewGuid().ToString('N'))'"
$entry = "$temporary/scripts/build-plugins.ps1"
$sdk = "$temporary/sdk"
$prefix = "$temporary/runtime"
$buildRoot = "$temporary/build"
$msys2 = "$temporary/msys2"
$testState = @{ Builds = @{}; Calls = @(); GitCalls = @(); FailBuild = $false; FailLfs = $false }

# Simulate plugin installs, without executing a compiler or system actions.
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
        if ($project.Name -eq 'zhuyin-deps') {
            New-Item -ItemType Directory -Force "$($project.Stage)/bin", "$($project.Stage)/licenses/libpinyin" | Out-Null
            [IO.File]::WriteAllText("$($project.Stage)/bin/libzhuyin.dll", 'dependency fixture')
            [IO.File]::WriteAllText("$($project.Stage)/licenses/libpinyin/COPYING", 'dependency license')
        }
        if ($project.Name -eq 'skk-deps') {
            New-Item -ItemType Directory -Force "$($project.Stage)/bin", "$($project.Stage)/share/libskk/rules/default", "$($project.Stage)/licenses/libskk" | Out-Null
            [IO.File]::WriteAllText("$($project.Stage)/bin/libskk.dll", 'dependency fixture')
            [IO.File]::WriteAllText("$($project.Stage)/share/libskk/rules/default/metadata.json", '{"name":"Default"}')
            [IO.File]::WriteAllText("$($project.Stage)/licenses/libskk/COPYING", 'libskk license')
        }
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

function Invoke-ReadobjFixture {
    Set-Variable -Name LASTEXITCODE -Value 0 -Scope 1
}

try {
    New-Item -ItemType Directory -Force "$temporary/scripts", "$temporary/plugins/fcitx5-chinese-addons" | Out-Null
    Copy-Item -LiteralPath "$PSScriptRoot/build-plugins.ps1" -Destination $entry
    $fixtureScript = [IO.File]::ReadAllText($entry).Replace(
        '& "$sysroot/bin/llvm-readobj.exe" --coff-imports $file', 'Invoke-ReadobjFixture $file')
    [IO.File]::WriteAllText($entry, $fixtureScript)
    foreach ($name in @('Fcitx5Core', 'LibIMECore')) {
        New-Item -ItemType Directory -Force "$sdk/lib/cmake/$name" | Out-Null
        [IO.File]::WriteAllText("$sdk/lib/cmake/$name/${name}Config.cmake", 'fixture')
    }
    New-Item -ItemType Directory -Force "$msys2/clang64/share/rime-data" | Out-Null
    [IO.File]::WriteAllText("$msys2/clang64/share/rime-data/default.yaml", 'rime fixture')
    New-Item -ItemType Directory -Force "$msys2/clang64/share/opencc" | Out-Null
    [IO.File]::WriteAllText("$msys2/clang64/share/opencc/t2s.json", 'opencc fixture')
    foreach ($name in @('glib2', 'db', 'libgee', 'json-glib')) {
        New-Item -ItemType Directory -Force "$msys2/clang64/share/licenses/$name" | Out-Null
        [IO.File]::WriteAllText("$msys2/clang64/share/licenses/$name/COPYING", 'fixture license')
    }
    $environmentBefore = @(Get-ChildItem Env: | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" })
    & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot -WhatIf
    if ((Test-Path $prefix) -or (Test-Path $buildRoot) -or $testState.Calls.Count) {
        throw 'WhatIf changed the deployment or invoked CMake.'
    }
    Assert-Failure { & $entry -CorePrefix "$temporary/missing-sdk" -Prefix $prefix } 'matching installed Core/libime SDK'
    foreach ($name in @('first', 'fcitx5-rime', 'future-plugin')) {
        $source = "$temporary/plugins/$name"
        New-Item -ItemType Directory -Force $source | Out-Null
        [IO.File]::WriteAllText("$source/CMakeLists.txt", 'fixture')
        [IO.File]::WriteAllText("$source/LICENSE", 'plugin license')
        & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot -MSYS2Root $msys2 | Out-Null
        if ($name -eq 'fcitx5-rime') {
            if (!(Test-Path -LiteralPath "$prefix/plugins/fcitx5-rime/share/rime-data/opencc/t2s.json")) {
                throw 'Rime OpenCC data was not copied into the plugin package.'
            }
            if (!(Test-Path -LiteralPath "$prefix/plugins/fcitx5-rime/share/rime-data/default.yaml")) {
                throw 'Rime data was not copied into the plugin package.'
            }
            continue
        }
        foreach ($relative in @("plugins/$name/share/fcitx5/inputmethod/$name.conf", "plugins/$name/licenses/LICENSE")) {
            if (!(Test-Path -LiteralPath "$prefix/$relative")) { throw "Missing automatically discovered plugin output: $relative" }
        }
    }
    if ($testState.Builds.Values.Name -contains 'fcitx5-chinese-addons' -or (Test-Path "$prefix/plugins/fcitx5-chinese-addons")) {
        throw 'fcitx5-chinese-addons was rebuilt as an external plugin.'
    }
    $lfsCalls = @($testState.GitCalls | ForEach-Object { $_ -join ' ' } | Where-Object { $_ -match ' lfs pull$' })
    if ($testState.GitCalls.Count -ne 6 -or $lfsCalls.Count -ne 6) {
        throw 'Every plugin build must fetch Git LFS assets before building.'
    }
    New-Item -ItemType Directory -Force "$temporary/plugins/fcitx5-zhuyin", "$msys2/clang64/bin" | Out-Null
    [IO.File]::WriteAllText("$temporary/plugins/fcitx5-zhuyin/CMakeLists.txt", 'fixture')
    & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot -MSYS2Root $msys2 | Out-Null
    foreach ($relative in @('bin/libzhuyin.dll', 'licenses/libpinyin/COPYING', 'licenses/glib2/COPYING', 'licenses/db/COPYING')) {
        if (!(Test-Path -LiteralPath "$prefix/plugins/fcitx5-zhuyin/$relative")) { throw "Missing Zhuyin dependency output: $relative" }
    }
    $zhuyinSource = [IO.Path]::GetFullPath("$temporary/plugins/fcitx5-zhuyin")
    $zhuyinConfigure = @($testState.Calls | Where-Object {
        $_ -contains $zhuyinSource -and $_ -contains '-DLIBZHUYIN_DATABASE_FORMAT=BerkeleyDB'
    })
    if (!$zhuyinConfigure.Count) {
        throw 'Zhuyin did not use the native Berkeley DB backend.'
    }
    New-Item -ItemType Directory -Force "$temporary/cmake", "$temporary/plugins/fcitx5-mozc" | Out-Null
    [IO.File]::WriteAllText("$temporary/plugins/fcitx5-mozc/CMakeLists.txt", 'fixture')
    [IO.File]::WriteAllText("$temporary/plugins/fcitx5-mozc/LICENSE", 'mozc addon license')
    Assert-Failure { & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot -MSYS2Root $msys2 } 'nested submodules are not initialized'
    New-Item -ItemType Directory -Force "$temporary/plugins/fcitx5-mozc/mozc/src/build_tools",
        "$temporary/plugins/fcitx5-mozc/abseil-cpp", "$temporary/plugins/fcitx5-mozc/protobuf" | Out-Null
    # Keep the test self-contained when CI or local checkouts omit submodules.
    [IO.File]::WriteAllText("$temporary/plugins/fcitx5-mozc/mozc/src/build_tools/embed_file.py", @'
import argparse
import pathlib

parser = argparse.ArgumentParser()
parser.add_argument('--input', required=True)
parser.add_argument('--name', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
size = len(pathlib.Path(args.input).read_bytes())
pathlib.Path(args.output).write_text(
    f'constexpr unsigned char {args.name}_data[{size}] = {{}};\n'
    f'constexpr EmbeddedFile {args.name} = {{{args.name}_data, {size}}};\n',
    encoding='utf-8',
)
'@)
    foreach ($sub in @('mozc', 'abseil-cpp', 'protobuf')) {
        [IO.File]::WriteAllText("$temporary/plugins/fcitx5-mozc/$sub/LICENSE", "$sub license")
    }
    $mozcFixtureData = "$temporary/mozc-fixture.data"
    $mozcMagic = [byte[]]@(0xEF, 0x4D, 0x4F, 0x5A, 0x43, 0x0D, 0x0A)
    $mozcFixtureBytes = $mozcMagic + [Text.Encoding]::ASCII.GetBytes('FIXTURE')
    [IO.File]::WriteAllBytes($mozcFixtureData, $mozcFixtureBytes)
    $tokens = $null
    $parseErrors = $null
    $mozcAst = [Management.Automation.Language.Parser]::ParseFile(
        $entry, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count) { throw "Parse error in $entry" }
    foreach ($fnName in @('Test-MozcDataHeader', 'Assert-MozcDataValid', 'Resolve-MozcDataFile')) {
        $fnAst = $mozcAst.Find({ param($node)
            $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $fnName
        }, $true)
        . ([scriptblock]::Create($fnAst.Extent.Text))
    }
    $mozcAttempts = [Collections.Generic.List[string]]::new()
    function Invoke-PluginCommand {
        param($Executable, $Arguments)
        $targetUrl = $Arguments[-1]
        $outputIndex = [Array]::IndexOf($Arguments, '--output')
        $outputPath = $Arguments[$outputIndex + 1]
        $script:mozcAttempts.Add($targetUrl)
        if ($targetUrl -eq 'https://primary.invalid/mozc.data') {
            throw 'curl.exe failed (28): Connection timed out'
        }
        if ($targetUrl -eq 'https://fallback.invalid/mozc.data') {
            [IO.File]::WriteAllBytes($outputPath, $script:mozcFixtureBytes)
            return
        }
        throw "Unexpected Mozc URL: $targetUrl"
    }
    [IO.File]::WriteAllText("$temporary/cmake/pinyin-lock.json",
        (@{ mozcData = @{
            file = 'mozc.data'
            url = 'https://primary.invalid/mozc.data'
            fallbackUrls = @('https://fallback.invalid/mozc.data')
        } } | ConvertTo-Json -Depth 4))
    $resolvedMozc = Resolve-MozcDataFile $temporary '' 3>$null
    if ($resolvedMozc -ne "$temporary/build/mozc-data/mozc.data" -or
        ($mozcAttempts -join '|') -ne 'https://primary.invalid/mozc.data|https://fallback.invalid/mozc.data') {
        throw "Mozc fallback download failed: $($mozcAttempts -join ', ')"
    }
    Remove-Item -LiteralPath "$temporary/build/mozc-data" -Recurse -Force
    [IO.File]::WriteAllText("$temporary/cmake/pinyin-lock.json",
        (@{ mozcData = @{ file = 'mozc.data'; url = 'https://example.invalid/mozc.data' } } | ConvertTo-Json -Depth 4))
    $badMozcData = "$temporary/mozc-bad.data"
    [IO.File]::WriteAllBytes($badMozcData, [Text.Encoding]::ASCII.GetBytes('WRONG'))
    Assert-Failure { & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot -MSYS2Root $msys2 -MozcData $badMozcData } 'Invalid Mozc data header'
    & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot -MSYS2Root $msys2 -MozcData $mozcFixtureData | Out-Null
    $generatedInc = "$buildRoot/fcitx5-mozc/data_manager/oss/mozc_data.inc"
    if (!(Test-Path -LiteralPath $generatedInc -PathType Leaf)) {
        throw 'Mozc data header was not generated from mozc.data.'
    }
    $generatedText = [IO.File]::ReadAllText($generatedInc)
    if ($generatedText -notlike '*kOssMozcDataSet*' -or
        $generatedText -notlike "*$($mozcFixtureBytes.Length)*") {
        throw 'Mozc data header was not generated from mozc.data.'
    }
    foreach ($relative in @('licenses/LICENSE', 'licenses/mozc/LICENSE', 'licenses/abseil-cpp/LICENSE', 'licenses/protobuf/LICENSE')) {
        if (!(Test-Path -LiteralPath "$prefix/plugins/fcitx5-mozc/$relative")) { throw "Missing Mozc dependency output: $relative" }
    }
    Remove-Item -LiteralPath "$temporary/plugins/fcitx5-mozc" -Recurse -Force
    New-Item -ItemType Directory -Force "$temporary/plugins/fcitx5-skk" | Out-Null
    [IO.File]::WriteAllText("$temporary/plugins/fcitx5-skk/CMakeLists.txt", 'fixture')
    $skkFixtureJisyo = "$temporary/SKK-JISYO.L"
    [IO.File]::WriteAllText($skkFixtureJisyo, ";; okuri-ari entries.`n;; okuri-nasi entries.`n")
    $skkFixtureHash = (Get-FileHash -LiteralPath $skkFixtureJisyo -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$temporary/cmake/pinyin-lock.json",
        (@{ skkJisyo = @{
            file = 'SKK-JISYO.L'
            url = 'https://example.invalid/SKK-JISYO.L'
            sha256 = $skkFixtureHash
        } } | ConvertTo-Json -Depth 4))
    $badSkkJisyo = "$temporary/SKK-JISYO.bad"
    [IO.File]::WriteAllText($badSkkJisyo, 'corrupted')
    Assert-Failure { & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot -MSYS2Root $msys2 -SkkJisyo $badSkkJisyo } 'SKK-JISYO SHA256 mismatch'
    & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot -MSYS2Root $msys2 -SkkJisyo $skkFixtureJisyo | Out-Null
    foreach ($relative in @('bin/libskk.dll', 'share/libskk/rules/default/metadata.json', 'share/skk/SKK-JISYO.L',
            'licenses/libskk/COPYING', 'licenses/glib2/COPYING', 'licenses/libgee/COPYING', 'licenses/json-glib/COPYING')) {
        if (!(Test-Path -LiteralPath "$prefix/plugins/fcitx5-skk/$relative")) { throw "Missing SKK dependency output: $relative" }
    }
    $skkSource = [IO.Path]::GetFullPath("$temporary/plugins/fcitx5-skk")
    $skkConfigure = @($testState.Calls | Where-Object {
        $_ -contains $skkSource -and $_ -contains '-DENABLE_QT=OFF' -and $_ -contains '-DSKK_PATH=$XDG_DATA_DIRS/skk'
    })
    if (!$skkConfigure.Count) {
        throw 'SKK did not configure with ENABLE_QT=OFF and relocatable SKK_PATH.'
    }
    Remove-Item -LiteralPath "$temporary/plugins/fcitx5-skk" -Recurse -Force
    $testState.FailLfs = $true
    Assert-Failure { & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot -MSYS2Root $msys2 } 'git failed (1)'
    $testState.FailLfs = $false
    $testState.FailBuild = $true
    Assert-Failure { & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot -MSYS2Root $msys2 } 'cmake failed (9)'
    $testState.FailBuild = $false
    New-Item -ItemType Directory -Force "$temporary/plugins/uninitialized" | Out-Null
    Assert-Failure { & $entry -CorePrefix $sdk -Prefix $prefix -BuildRoot $buildRoot -MSYS2Root $msys2 } 'Plugin is not initialized'
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
