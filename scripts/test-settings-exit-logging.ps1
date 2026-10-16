[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SettingsBuild,
    [string]$LogDirectory = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$executable = (Resolve-Path -LiteralPath "$SettingsBuild/Fcitx5Settings.exe").Path
if (!$executable.StartsWith(($root + '\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Use an isolated Settings build inside the repository.'
}
if (!$LogDirectory) { $LogDirectory = "$root/build/settings-exit-logging/$([guid]::NewGuid().ToString('N'))" }
$LogDirectory = [IO.Path]::GetFullPath($LogDirectory)
if (!$LogDirectory.StartsWith(($root + '\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Use an isolated log directory inside the repository.'
}
$probe = Start-Process $executable -WindowStyle Hidden -PassThru -ArgumentList '--check-settings-diagnostics-test'
try {
    if (!$probe.WaitForExit(10000) -or $probe.ExitCode -ne 0) { throw 'Settings must be built with -UiTest.' }
} finally {
    if (!$probe.HasExited) { Stop-Process -Id $probe.Id; $probe.WaitForExit() }
    $probe.Dispose()
}
$previousDirectory = $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY
try {
    foreach ($mode in @('normal', 'off', 'async', 'terminate')) {
        $directory = Join-Path $LogDirectory $mode
        $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY = $directory
        $process = Start-Process $executable -WindowStyle Hidden -PassThru -ArgumentList "--diagnostic-test-$mode"
        try {
            if (!$process.WaitForExit(15000)) { throw "Diagnostic test $mode did not exit." }
            $fatal = $mode -in @('async', 'terminate')
            if (($process.ExitCode -ne 0) -ne $fatal) { throw "Unexpected $mode exit code: $($process.ExitCode)" }
            if ($mode -eq 'off') {
                if (Test-Path -LiteralPath $directory) { throw 'Disabled logging created a directory.' }
                continue
            }
            $files = @(Get-ChildItem -LiteralPath $directory -Filter "settings-$($process.Id)-*.log*" -File)
            if (!$files) { throw "$mode produced no Settings logs." }
            $raw = ($files | ForEach-Object { Get-Content -LiteralPath $_.FullName -Encoding UTF8 }) -join "`n"
            if ($raw -match 'private-diagnostic-test-value') { throw 'Exception message leaked into diagnostics.' }
            $events = @($raw -split "`n" | ConvertFrom-Json)
            $exits = @($events | Where-Object event -eq 'settings_exit')
            $errors = @($events | Where-Object event -eq 'settings_exception')
            if ($errors.Count -ne 1 -or [bool]$errors[0].fatal -ne $fatal) { throw "Missing $mode exception record." }
            if ($mode -eq 'terminate') {
                if ($errors[0].operation -ne 'std_terminate' -or $errors[0].kind -ne 'std_exception') { throw 'Terminate cause missing.' }
            } else {
                if ($errors[0].hr -ne 2147942405 -or $errors[0].kind -ne 'hresult_error') { throw "$mode HRESULT missing." }
                $begin = @($events | Where-Object event -eq 'settings_operation_begin')
                $end = @($events | Where-Object event -eq 'settings_operation_end')
                $stage = @($events | Where-Object event -eq 'settings_operation_stage')
                if ($begin.Count -ne 1 -or $end.Count -ne 1 -or $stage.Count -ne 1 -or
                    $begin[0].operation_id -ne $end[0].operation_id -or
                    $stage[0].operation_id -ne $end[0].operation_id -or
                    !$end[0].unwinding) { throw "$mode operation correlation or exception unwind missing." }
                if ($mode -eq 'async' -and $stage[0].tid -eq $begin[0].tid) { throw 'Async test did not switch threads.' }
            }
            if ($fatal) {
                if ($exits.Count) { throw 'Fatal termination was reported as a normal exit.' }
            } elseif ($exits.Count -ne 1 -or $exits[0].exit_code -ne 0 -or $exits[0].reason -ne 'test_completed') {
                throw 'Normal exit marker missing.'
            }
        } finally {
            if (!$process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit() }
            $process.Dispose()
        }
    }
    Write-Output "PASS: normal exit, recoverable HRESULT, async fail-fast, std::terminate, privacy and disabled logging. Logs: $LogDirectory"
} finally {
    $env:FCITX5_DIAGNOSTIC_TEST_DIRECTORY = $previousDirectory
}
