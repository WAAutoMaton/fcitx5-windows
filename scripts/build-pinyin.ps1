[CmdletBinding()]
param(
    [string]$MSYS2Root = 'C:/msys64',
    [string]$DependencyPrefix = '',
    [string]$Prefix = '',
    [ValidateSet('Source', 'Prebuilt')][string]$DataMode = 'Source',
    [string]$DataArchive = '',
    [int]$Jobs = 6
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Prefix) { $Prefix = "$root/dist/pinyin" }
$Prefix = [IO.Path]::GetFullPath($Prefix).Replace('\', '/')
if (!$Prefix.StartsWith(([IO.Path]::GetFullPath($root).Replace('\', '/') + '/'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The installation prefix must be inside this repository.'
}
$sysroot = "$MSYS2Root/clang64"
if (!(Test-Path "$sysroot/bin/clang++.exe")) { throw 'MSYS2 clang64 is required.' }
$lock = Get-Content "$root/cmake/pinyin-lock.json" -Raw | ConvertFrom-Json

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments
        $status = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    if ($status -ne 0) { throw "$Executable failed ($status): $($Arguments -join ' ')" }
}

function Get-VerifiedArchive([string]$Url, [string]$Destination, [string]$Hash) {
    if ((Test-Path $Destination) -and (Get-FileHash $Destination -Algorithm SHA256).Hash -eq $Hash) { return }
    Invoke-Checked 'curl.exe' @('-fL', '--retry', '3', '--connect-timeout', '20', '--max-time', '1200', '-o', "$Destination.download", $Url)
    if ((Get-FileHash "$Destination.download" -Algorithm SHA256).Hash -ne $Hash) {
        throw "SHA256 mismatch: $Url"
    }
    Move-Item -LiteralPath "$Destination.download" -Destination $Destination -Force
}

& "$PSScriptRoot/apply-pinyin-patches.ps1"
$oldPath = $env:PATH
$oldMSYS2Root = $env:MSYS2_ROOT
try {
    $env:MSYS2_ROOT = $MSYS2Root
    $runtimeDirectories = @("$sysroot/bin")
    if ($DependencyPrefix) { $runtimeDirectories = @("$DependencyPrefix/bin") + $runtimeDirectories }
    $env:PATH = "$Prefix/bin;" + ($runtimeDirectories -join ';') + ";$MSYS2Root/usr/bin;$oldPath"
    $common = @('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_INSTALL_PREFIX=$Prefix",
        "-DCMAKE_C_COMPILER=$sysroot/bin/clang.exe", "-DCMAKE_CXX_COMPILER=$sysroot/bin/clang++.exe",
        "-DCMAKE_MAKE_PROGRAM=$sysroot/bin/ninja.exe")
    Invoke-Checked 'cmake' (@('-S', $root, '-B', "$root/build/pinyin-core", '-DARCH=AMD64',
        '-DENABLE_WINDOWS_ASCII_FALLBACK=OFF', '-DENABLE_KEYBOARD=OFF') + $common)
    Invoke-Checked 'cmake' @('--build', "$root/build/pinyin-core", '-j', "$Jobs")
    Invoke-Checked 'cmake' @('--install', "$root/build/pinyin-core")

    if ($DataMode -eq 'Source') {
        foreach ($data in $lock.sourceData) {
            Get-VerifiedArchive "https://download.fcitx-im.org/data/$($data.file)" "$root/libime/data/$($data.file)" $data.sha256
        }
    }
    $dependencyOptions = @("-DCMAKE_TOOLCHAIN_FILE=$root/cmake/pinyin-toolchain.cmake",
        "-DWINDOWS_DEPENDENCY_PREFIX=$DependencyPrefix", '-DCMAKE_CXX_FLAGS=-fexperimental-library',
        '-DENABLE_TEST=OFF')
    $enableData = if ($DataMode -eq 'Source') { 'ON' } else { 'OFF' }
    Invoke-Checked 'cmake' (@('-S', "$root/libime", '-B', "$root/build/pinyin-libime",
        "-DENABLE_DATA=$enableData", '-DENABLE_TOOLS=ON') + $common + $dependencyOptions)
    Invoke-Checked 'cmake' @('--build', "$root/build/pinyin-libime", '-j', "$Jobs")
    Invoke-Checked 'cmake' @('--install', "$root/build/pinyin-libime")

    if ($DataMode -eq 'Prebuilt') {
        $cache = "$root/build/pinyin-data"
        New-Item -ItemType Directory -Force $cache | Out-Null
        if (!$DataArchive) {
            $DataArchive = "$cache/$($lock.prebuiltData.file)"
            Get-VerifiedArchive $lock.prebuiltData.url $DataArchive $lock.prebuiltData.sha256
        }
        $DataArchive = (Resolve-Path $DataArchive).Path
        if ((Get-FileHash $DataArchive -Algorithm SHA256).Hash -ne $lock.prebuiltData.sha256) {
            throw 'Unexpected prebuilt data archive hash.'
        }
        Push-Location $cache
        try {
            Invoke-Checked 'cmake' @('-E', 'tar', 'xf', $DataArchive, 'usr/share/libime', 'usr/lib/libime')
        } finally { Pop-Location }
        Invoke-Checked 'cmake' @('-E', 'copy_directory', "$cache/usr/share/libime", "$Prefix/share/libime")
        Invoke-Checked 'cmake' @('-E', 'copy_directory', "$cache/usr/lib/libime", "$Prefix/lib/libime")
    }
    Invoke-Checked 'cmake' (@('-S', "$root/chinese-addons", '-B', "$root/build/pinyin-addons",
        '-DENABLE_GUI=OFF', '-DENABLE_OPENCC=OFF', '-DENABLE_CLOUDPINYIN=OFF',
        '-DENABLE_DATA=ON', '-DENABLE_TOOLS=OFF') + $common + $dependencyOptions)
    Invoke-Checked 'cmake' @('--build', "$root/build/pinyin-addons", '-j', "$Jobs")
    Invoke-Checked 'cmake' @('--install', "$root/build/pinyin-addons")

    $pending = [Collections.Generic.Queue[string]]::new()
    $visited = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    Get-ChildItem "$Prefix/bin", "$Prefix/lib/fcitx5" -File | Where-Object Extension -in '.dll', '.exe' |
        ForEach-Object { $pending.Enqueue($_.FullName) }
    while ($pending.Count) {
        $file = $pending.Dequeue()
        if (!$visited.Add($file)) { continue }
        $imports = & "$sysroot/bin/llvm-readobj.exe" --coff-imports $file
        if ($LASTEXITCODE -ne 0) { throw "Cannot read PE imports: $file" }
        foreach ($line in $imports) {
            if ($line -notmatch '^\s+Name: (.+\.dll)$') { continue }
            $name = $matches[1]
            $destination = "$Prefix/bin/$name"
            if (Test-Path $destination) { $pending.Enqueue($destination); continue }
            $source = $runtimeDirectories | ForEach-Object { "$_/$name" } |
                Where-Object { Test-Path $_ } | Select-Object -First 1
            if ($source) {
                Copy-Item -LiteralPath $source -Destination $destination
                $pending.Enqueue($destination)
            } elseif ($name -notmatch '^(api-|ext-)' -and !(Test-Path "$env:SystemRoot/System32/$name")) {
                throw "Unresolved runtime dependency $name in $file"
            }
        }
    }
    foreach ($resource in @('share/libime/sc.dict', 'lib/libime/zh_CN.lm', 'lib/fcitx5/libpinyin.dll')) {
        if (!(Test-Path "$Prefix/$resource")) { throw "Missing Pinyin resource: $resource" }
    }
    Write-Host "Pinyin Core is ready: $Prefix/bin/Fcitx5.exe"
} finally {
    $env:PATH = $oldPath
    $env:MSYS2_ROOT = $oldMSYS2Root
}
