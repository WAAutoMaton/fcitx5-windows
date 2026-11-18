[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$IsccPath)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$temporary = Join-Path $root "build/installer-restart-test $([guid]::NewGuid().ToString('N'))"
$passed = $false
try {
    New-Item -ItemType Directory -Path $temporary | Out-Null
    $source = Get-Content -LiteralPath "$root/installer/fcitx5.iss" -Encoding utf8 -Raw
    $callback = [regex]::Match($source, '(?s)procedure DeinitializeSetup;.*?(?=function InitializeUninstall)')
    if (!$callback.Success) { throw 'Cannot locate the real staging, restart and finished-page callbacks.' }
    # Run the real NeedRestart event in Inno's installation lifecycle, using a
    # fake helper and simulated silent state. /NORESTART always blocks reboot.
    $code = $callback.Value.Replace('NextButtonClick', 'UpgradeNextButtonClick').Replace('CurPageChanged', 'UpgradeCurPageChanged').Replace('WizardSilent', 'TestSilent').Replace('MsgBox(', 'TestMessage(')
    $codePath = Join-Path $temporary 'restart.iss'
    Set-Content -LiteralPath $codePath -Value $code -Encoding utf8
    & $IsccPath "/DTestOutput=$temporary" "/DTestCode=$codePath" "$root/installer/tests/restart.iss" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Restart choice test compilation failed ($LASTEXITCODE)." }
    foreach ($scenario in @('routing', 'stage', 'silent', 'fresh', 'failure', 'zero')) {
        $log = Join-Path $temporary "$scenario.log"
        $process = Start-Process -FilePath "$temporary/restart-choice-test.exe" -WindowStyle Hidden -PassThru -ArgumentList @(
            '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/SCENARIO=$scenario", "/LOG=`"$log`""
        )
        if (!$process.WaitForExit(30000)) {
            $process.Kill()
            throw "Restart test timed out ($scenario). See $log"
        }
        $process.WaitForExit()
        $messages = Get-Content -LiteralPath $log -Raw
        $expectedCode = switch ($scenario) { 'routing' { 1 } 'fresh' { 0 } 'failure' { 5 } 'zero' { 1 } default { 3010 } }
        $marker = if ($scenario -eq 'routing') { 'FCITX5-RESTART-CHOICE-TEST-PASSED' } else { 'FCITX5-RESTART-LIFECYCLE-TEST-PASSED' }
        $valid = $messages -match $marker -and $messages -notmatch 'Runtime error|Internal error|Exception message'
        if ($scenario -ne 'routing') {
            $restart = if ($scenario -eq 'stage') { 'Yes' } else { 'No' }
            $valid = $valid -and $messages -match "Need to restart Windows\? $restart"
            if ($scenario -in @('failure', 'zero')) {
                $valid = $valid -and ([regex]::Matches($messages, 'TEST cancel-upgrade').Count -eq 1)
            } else {
                $valid = $valid -and $messages -notmatch 'TEST cancel-upgrade'
            }
        }
        if ($process.ExitCode -ne $expectedCode -or !$valid) {
            Write-Output $messages
            throw "Restart test failed ($scenario, exit=$($process.ExitCode), expected=$expectedCode). See $log"
        }
    }
    Write-Output 'PASS: real Inno restart lifecycle, sealing failure, finished-page text, hidden/immediate/later/silent/fresh restart routing and helper failure (fake helper; no system changes or reboot).'
    $passed = $true
} finally {
    $resolved = [IO.Path]::GetFullPath($temporary)
    if (!$resolved.StartsWith(($root + '\build\'), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected cleanup path: $resolved"
    }
    if ($passed -and (Test-Path -LiteralPath $resolved)) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
