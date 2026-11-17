[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$IsccPath)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$temporary = Join-Path $root "build/installer-restart-test $([guid]::NewGuid().ToString('N'))"
$passed = $false
try {
    New-Item -ItemType Directory -Path $temporary | Out-Null
    $source = Get-Content -LiteralPath "$root/installer/fcitx5.iss" -Encoding utf8 -Raw
    $callback = [regex]::Match($source, '(?s)function NextButtonClick\(CurPageID: Integer\): Boolean;.*?(?=function GetCustomSetupExitCode)')
    if (!$callback.Success) { throw 'Cannot locate the real restart callback.' }
    # Exercise the real routing callback with a fake helper and silent-state
    # input. The test aborts at wpReady and never requests a Windows restart.
    $code = $callback.Value.Replace('NextButtonClick', 'UpgradeNextButtonClick').Replace('WizardSilent', 'TestSilent').Replace('MsgBox(', 'TestMessage(')
    $codePath = Join-Path $temporary 'restart.iss'
    Set-Content -LiteralPath $codePath -Value $code -Encoding utf8
    & $IsccPath "/DTestOutput=$temporary" "/DTestCode=$codePath" "$root/installer/tests/restart.iss" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Restart choice test compilation failed ($LASTEXITCODE)." }
    $log = Join-Path $temporary 'test.log'
    $process = Start-Process -FilePath "$temporary/restart-choice-test.exe" -WindowStyle Hidden -Wait -PassThru -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$log`""
    )
    $messages = Get-Content -LiteralPath $log -Raw
    if ($process.ExitCode -ne 1 -or $messages -notmatch 'FCITX5-RESTART-CHOICE-TEST-PASSED' -or
        $messages -match 'Runtime error|Internal error|Exception message') {
        Write-Output $messages
        throw "Restart choice test failed ($($process.ExitCode)). See $log"
    }
    Write-Output 'PASS: immediate/later/silent/fresh restart routing and helper failure (fake helper; no installation or reboot).'
    $passed = $true
} finally {
    $resolved = [IO.Path]::GetFullPath($temporary)
    if (!$resolved.StartsWith(($root + '\build\'), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected cleanup path: $resolved"
    }
    if ($passed -and (Test-Path -LiteralPath $resolved)) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
