[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$DllPath = '',
    [string]$HandlePath = '',
    [switch]$Force,
    [switch]$SkipUnregister
)

$ErrorActionPreference = 'Stop'
$win32Root = Split-Path $PSScriptRoot -Parent
if (!$DllPath) {
    $DllPath = Join-Path $win32Root 'build/pinyin-tsf/dll/fcitx5-x86_64.dll'
}
$dll = (Resolve-Path -LiteralPath $DllPath).Path
$dllName = Split-Path $dll -Leaf

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated PowerShell.'
}

function Find-HandleTool {
    if ($HandlePath) {
        return (Resolve-Path -LiteralPath $HandlePath).Path
    }
    $command = Get-Command handle64.exe, handle.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($command) {
        return $command.Source
    }
    return $null
}

function Find-DllOwners {
    param([string]$Path, [string]$Name, [string]$Tool)

    $owners = @{}
    if ($Tool) {
        $output = (& $Tool -accepteula -nobanner $Name 2>&1 | Out-String)
        foreach ($line in ($output -split "`r?`n")) {
            $match = [regex]::Match($line, '^\s*(\S+)\s+pid:\s*(\d+)',
                                    [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
            if ($match.Success) {
                $owners[[int]$match.Groups[2].Value] =
                    $match.Groups[1].Value
            }
        }
    }

    if (!$owners.Count) {
        $output = (& tasklist.exe /m $Name /fo csv /nh 2>$null | Out-String)
        foreach ($line in ($output -split "`r?`n")) {
            $match = [regex]::Match($line, '^"([^"]+)","(\d+)"')
            if ($match.Success) {
                $owners[[int]$match.Groups[2].Value] =
                    $match.Groups[1].Value
            }
        }
    }

    if (!$owners.Count) {
        foreach ($process in Get-Process) {
            try {
                $module = $process.Modules |
                    Where-Object { $_.FileName -ieq $Path } |
                    Select-Object -First 1
                if ($module) {
                    $owners[$process.Id] = $process.ProcessName
                }
            } catch {
            }
        }
    }
    return $owners
}

function Test-DllReleased {
    param([string]$Path)
    try {
        $stream = [System.IO.File]::Open(
            $Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None)
        $stream.Dispose()
        return $true
    } catch {
        return $false
    }
}

$regsvr32 = Join-Path $env:WINDIR 'System32/regsvr32.exe'
if (!$SkipUnregister -and $PSCmdlet.ShouldProcess($dll, 'Unregister TSF')) {
    $arguments = "/s /u `"$dll`""
    $unregister = Start-Process -FilePath $regsvr32 -ArgumentList $arguments `
        -Wait -PassThru -WindowStyle Hidden
    if ($unregister.ExitCode -ne 0) {
        throw "DllUnregisterServer failed with exit code $($unregister.ExitCode)."
    }
    Write-Host "Unregistered $dll"
}

Start-Sleep -Milliseconds 200
$handleTool = Find-HandleTool
if (!$handleTool) {
    Write-Warning 'Handle was not found; using tasklist and accessible module lists.'
} else {
    Write-Host "Using handle tool: $handleTool"
}
$owners = Find-DllOwners $dll $dllName $handleTool

if ($owners.Count) {
    Write-Host 'Processes holding the TSF DLL:'
    foreach ($owner in $owners.GetEnumerator()) {
        Write-Host ("  {0} (PID {1})" -f $owner.Value, $owner.Key)
    }
    if (!$Force) {
        throw 'The DLL is still loaded. Re-run with -Force to stop only these processes.'
    }
    $restartCtfmon = $false
    foreach ($owner in $owners.GetEnumerator()) {
        $process = Get-Process -Id $owner.Key -ErrorAction SilentlyContinue
        if (!$process) {
            continue
        }
        if ($process.ProcessName -ieq 'ctfmon' -or
            $process.ProcessName -ieq 'ctfmon.exe') {
            $restartCtfmon = $true
        }
        if ($PSCmdlet.ShouldProcess(
                "$($process.ProcessName) (PID $($process.Id))", 'Stop process')) {
            Stop-Process -Id $process.Id -Force
        }
    }
    if ($restartCtfmon) {
        Start-Sleep -Milliseconds 500
        Start-Process (Join-Path $env:WINDIR 'System32/ctfmon.exe')
    }
}

for ($attempt = 0; $attempt -lt 20; ++$attempt) {
    if (Test-DllReleased $dll) {
        Write-Host "Released $dll"
        exit 0
    }
    Start-Sleep -Milliseconds 250
}

throw "The DLL is still locked: $dll"
