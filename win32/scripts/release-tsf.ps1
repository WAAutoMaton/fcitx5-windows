<#
.SYNOPSIS
Unregisters the deployed TSF DLL and checks whether applications still hold it.
.DESCRIPTION
-Force terminates only processes verified to load the exact DLL path.
This helper does not stop Core/settings or remove deployment files.
.EXAMPLE
./win32/scripts/release-tsf.ps1 -Force -WhatIf
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$DllPath = '',
    [string]$HandlePath = '',
    [string]$Prefix = '',
    [switch]$Force,
    [switch]$SkipUnregister
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/tsf-common.ps1"
$dll = Resolve-TsfDll -Prefix $Prefix -DllPath $DllPath
$dllName = Split-Path $dll -Leaf

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
try {
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (!$WhatIfPreference -and !$SkipUnregister -and
        !$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script from an elevated PowerShell, or preview with -WhatIf.'
    }
} finally {
    $identity.Dispose()
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
        $output = (& $Tool -nobanner $Path 2>&1 | Out-String)
        foreach ($line in ($output -split "`r?`n")) {
            $match = [regex]::Match($line, '^\s*(\S+)\s+pid:\s*(\d+)',
                                    [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
            if ($match.Success) {
                $owners[[int]$match.Groups[2].Value] =
                    $match.Groups[1].Value
            }
        }
    }

    $output = (& tasklist.exe /m $Name /fo csv /nh 2>$null | Out-String)
    $rows = $output -split "`r?`n" | Where-Object { $_.StartsWith('"') } |
        ConvertFrom-Csv -Header 'ImageName', 'ProcessId', 'Modules'
    foreach ($row in $rows) {
        $ownerId = 0
        if ([int]::TryParse($row.ProcessId, [ref]$ownerId)) {
            $owners[$ownerId] = $row.ImageName
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
    $verified = @{}
    foreach ($owner in $owners.GetEnumerator()) {
        $process = Get-Process -Id $owner.Key -ErrorAction SilentlyContinue
        if (!$process) { continue }
        try {
            $modules = @($process.Modules)
            if ($modules | Where-Object { $_.FileName -ieq $Path }) {
                $verified[$process.Id] = $process.ProcessName
            } elseif (!$modules.Count) {
                Write-Warning "Cannot verify DLL path for $($owner.Value) (PID $($owner.Key)); it will not be terminated."
            }
        } catch {
            Write-Warning "Cannot verify DLL path for $($owner.Value) (PID $($owner.Key)); it will not be terminated."
        }
    }
    return $verified
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

if (!$SkipUnregister -and $PSCmdlet.ShouldProcess($dll, 'Unregister TSF')) {
    Invoke-TsfRegistration -DllPath $dll -Unregister
    Write-Host "Unregistered $dll"
}

Start-Sleep -Milliseconds 200
$handleTool = if ($WhatIfPreference) { $null } else { Find-HandleTool }
if (!$handleTool) {
    Write-Verbose 'Using tasklist and accessible module lists; Handle is unavailable or skipped for WhatIf.'
} else {
    Write-Host "Using handle tool: $handleTool"
}
$owners = Find-DllOwners $dll $dllName $handleTool

if ($owners.Count) {
    Write-Host 'Processes holding the TSF DLL:'
    foreach ($owner in $owners.GetEnumerator()) {
        Write-Host ("  {0} (PID {1})" -f $owner.Value, $owner.Key)
    }
    if (!$Force -and !$WhatIfPreference) {
        throw 'The DLL is still loaded. Re-run with -Force to stop only these processes.'
    }
    $restartCtfmon = $false
    foreach ($owner in $owners.GetEnumerator()) {
        if (!$Force) { continue }
        $process = Get-Process -Id $owner.Key -ErrorAction SilentlyContinue
        if (!$process) {
            continue
        }
        if ($PSCmdlet.ShouldProcess(
                "$($process.ProcessName) (PID $($process.Id))", 'Stop process')) {
            if (!($process.Modules | Where-Object { $_.FileName -ieq $dll })) {
                continue
            }
            $wasCtfmon = $process.ProcessName -ieq 'ctfmon'
            Stop-Process -InputObject $process -Force
            $restartCtfmon = $restartCtfmon -or $wasCtfmon
        }
    }
    if ($restartCtfmon -and $PSCmdlet.ShouldProcess('ctfmon.exe', 'Restart text services monitor')) {
        Start-Sleep -Milliseconds 500
        Start-Process (Join-Path $env:WINDIR 'System32/ctfmon.exe') -WindowStyle Hidden
    }
}

if ($WhatIfPreference) {
    Write-Output "Preview complete for $dll. No registration or processes were changed."
    return
}

for ($attempt = 0; $attempt -lt 20; ++$attempt) {
    if (Test-DllReleased $dll) {
        Write-Host "Released $dll"
        return
    }
    Start-Sleep -Milliseconds 250
}

throw "The DLL is still locked: $dll"
