[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$IsccPath
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$temporary = Join-Path $root "build/installer-wizard-test $([guid]::NewGuid().ToString('N'))"
$passed = $false
try {
    New-Item -ItemType Directory -Path $temporary | Out-Null
    $source = @(Get-Content -LiteralPath "$root/installer/fcitx5.iss" -Encoding utf8)
    $codeStart = [Array]::IndexOf($source, '[Code]')
    $helperStart = [Array]::IndexOf($source, 'function RunHelper(Command: String; OriginalUser: Boolean): Integer;')
    if ($codeStart -lt 0 -or $helperStart -le $codeStart) {
        throw 'Cannot locate the installer mode and wizard code.'
    }
    # Compile the real mode and page callbacks; omit all helper and system operations.
    $code = Join-Path $temporary 'mode.iss'
    $source[($codeStart + 1)..($helperStart - 1)] | Set-Content -LiteralPath $code -Encoding utf8
    foreach ($enabled in @(1, 0)) {
        & $IsccPath "/DTestOutput=$temporary" "/DTestCode=$code" "/DRebootUpgrade=$enabled" "$root/installer/tests/wizard.iss" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Wizard test compilation failed ($LASTEXITCODE)." }
        foreach ($managed in @($false, $true)) {
            $prefix = Join-Path $temporary "install $enabled $managed"
            New-Item -ItemType Directory -Path "$prefix/setup" | Out-Null
            if ($managed) {
                Set-Content -LiteralPath "$prefix/setup/managed-install" -Value 'FCITX5-MANAGED-1' -Encoding ascii
            }
            $expected = [int]($enabled -eq 1 -and $managed)
            $log = Join-Path $temporary "wizard-$enabled-$managed.log"
            $process = Start-Process -FilePath "$temporary/wizard-test-$enabled.exe" -WindowStyle Hidden -Wait -PassThru -ArgumentList @(
                '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
                "/DIR=`"$prefix`"", "/LOG=`"$log`"", "/EXPECTUPGRADE=$expected"
            )
            # Returning False at wpReady aborts silent Setup with code 1 before installation.
            $messages = Get-Content -LiteralPath $log -Raw
            if ($process.ExitCode -ne 1 -or $messages -notmatch 'FCITX5-WIZARD-TEST-PASSED' -or
                $messages -match 'Runtime error|Internal error|Exception message') {
                Write-Output $messages
                throw "Wizard test failed (reboot-upgrade=$enabled, managed=$managed, exit=$($process.ExitCode)). See $log"
            }
        }
    }
    Write-Output 'PASS: real Inno wizard initialization, fresh/managed installs, upgrade enabled/disabled, task visibility, destinations and stable mode (no installation or system changes).'
    $passed = $true
} finally {
    $resolved = [IO.Path]::GetFullPath($temporary)
    if (!$resolved.StartsWith(($root + '\build\'), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected cleanup path: $resolved"
    }
    if ($passed -and (Test-Path -LiteralPath $resolved)) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
