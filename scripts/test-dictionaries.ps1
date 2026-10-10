[CmdletBinding()]
param([string]$Prefix = '', [string]$TsfBuild = '', [string]$LogDirectory = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Prefix) { $Prefix = "$root/dist/pinyin" }
if (!$TsfBuild) { $TsfBuild = "$root/win32/build/pinyin-tsf" }
if (!$LogDirectory) { $LogDirectory = "$root/build/dictionaries-test" }
if (Get-Process Fcitx5,Fcitx5Settings -ErrorAction SilentlyContinue) {
    throw 'Stop existing Core and settings before this isolated dictionary test.'
}
New-Item -ItemType Directory -Force $LogDirectory | Out-Null
$converter = (Resolve-Path "$Prefix/bin/libime_pinyindict.exe").Path
$probe = (Resolve-Path "$TsfBuild/tests/dictionaries_probe.exe").Path
$ready = (Resolve-Path "$TsfBuild/tests/ipc_probe.exe").Path
$utf8 = [Text.UTF8Encoding]::new($false)
$first = -join ([char[]](0x591c,0x6765,0x98ce,0x96e8,0x58f0))
$second = -join ([char[]](0x79cb,0x6765,0x98ce,0x96e8,0x7b19))
[IO.File]::WriteAllText("$LogDirectory/initial.txt", "$first ye'lai'feng'yu'sheng 0`n", $utf8)
[IO.File]::WriteAllText("$LogDirectory/replacement.txt", "$second qiu'lai'feng'yu'sheng 0`n", $utf8)
foreach ($fixture in @('initial', 'replacement')) {
    & $converter "$LogDirectory/$fixture.txt" "$LogDirectory/$fixture.dict"
    if ($LASTEXITCODE -ne 0) { throw "Dictionary fixture conversion failed: $fixture" }
}
$directory = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Fcitx5/pinyin/dictionaries'
New-Item -ItemType Directory -Force $directory | Out-Null
$startup = Join-Path $directory "fcitx-startup-test-$([guid]::NewGuid().ToString('N')).dict"
$process = $null
try {
    Copy-Item -LiteralPath "$LogDirectory/initial.dict" -Destination $startup
    $settings = [IO.Path]::GetFullPath("$LogDirectory/windows-$([guid]::NewGuid().ToString('N')).conf")
    $process = Start-Process "$Prefix/bin/Fcitx5.exe" -WindowStyle Hidden -PassThru `
        -WorkingDirectory "$Prefix/bin" -ArgumentList @('--windows-settings-file', "`"$settings`"") `
        -RedirectStandardOutput "$LogDirectory/core.stdout.log" -RedirectStandardError "$LogDirectory/core.stderr.log"
    $connected = $false
    for ($attempt = 0; $attempt -lt 50; ++$attempt) {
        Start-Sleep -Milliseconds 100
        if ($process.HasExited) { throw 'Core exited before the dictionary test.' }
        $previous = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & $ready --ready 2>$null
            $isReady = $LASTEXITCODE -eq 0
        } finally { $ErrorActionPreference = $previous }
        if ($isReady) { $connected = $true; break }
    }
    if (!$connected) { throw 'Core connection timed out.' }
    & $probe "$LogDirectory/initial.dict" "$LogDirectory/replacement.dict" $startup
    if ($LASTEXITCODE -ne 0) { throw 'Third-party dictionary integration failed.' }
} finally {
    if ($process -and !$process.HasExited) {
        Stop-Process -Id $process.Id
        $process.WaitForExit()
    }
    if (Test-Path -LiteralPath $startup) { Remove-Item -LiteralPath $startup }
}
