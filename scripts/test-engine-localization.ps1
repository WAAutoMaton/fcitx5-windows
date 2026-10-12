[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Prefix,
    [Parameter(Mandatory)][string]$TsfBuild,
    [string]$LogDirectory = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$LogDirectory) { $LogDirectory = "$root/build/engine-localization-test/$([guid]::NewGuid().ToString('N'))" }
New-Item -ItemType Directory -Force $LogDirectory | Out-Null
$settingsFile = [IO.Path]::GetFullPath("$LogDirectory/windows.conf")
$uiSettingsFile = [IO.Path]::GetFullPath("$LogDirectory/windows-ui.conf")
$previousSkipUserPath = $env:SKIP_FCITX_USER_PATH
$previousLocale = $env:LC_ALL
$core = $null
try {
    $env:SKIP_FCITX_USER_PATH = '1'
    $env:LC_ALL = 'C.UTF-8'
    foreach ($language in @('zh_CN', 'zh_TW', 'en_US')) {
        $setting = switch ($language) {
            'zh_CN' { 'zh-Hans' }
            'zh_TW' { 'zh-Hant' }
            'en_US' { 'en' }
        }
        [IO.File]::WriteAllText($uiSettingsFile, "[General]`nVersion=1`nLanguage=$setting`nRevision=1`n")
        $core = Start-Process "$Prefix/bin/Fcitx5.exe" -WindowStyle Hidden -PassThru -WorkingDirectory "$Prefix/bin" `
            -ArgumentList @('--windows-settings-file', "`"$settingsFile`"", '--windows-ui-settings-file', "`"$uiSettingsFile`"") `
            -RedirectStandardOutput "$LogDirectory/$language.stdout.log" -RedirectStandardError "$LogDirectory/$language.stderr.log"
        $ready = $false
        for ($attempt = 0; $attempt -lt 60; ++$attempt) {
            Start-Sleep -Milliseconds 200
            if ($core.HasExited) { throw "Test Core exited; use a free endpoint or build Core and probes with FCITX5_UI_TEST. See $LogDirectory/$language.stderr.log" }
            $previousErrorAction = $ErrorActionPreference
            try {
                $ErrorActionPreference = 'Continue'
                & "$TsfBuild/tests/ipc_probe.exe" --ready 2>$null
                $ready = $LASTEXITCODE -eq 0
            } finally { $ErrorActionPreference = $previousErrorAction }
            if ($ready) { break }
        }
        if (!$ready) { throw "Test Core did not become ready: $language" }
        & "$TsfBuild/tests/inputmethods_probe.exe" --localization $language
        if ($LASTEXITCODE -ne 0) { throw "Engine localization regression failed: $language" }
        Stop-Process -Id $core.Id
        $core.WaitForExit()
        $core = $null
    }
    Write-Output "Logs: $LogDirectory"
} finally {
    if ($core -and !$core.HasExited) {
        Stop-Process -Id $core.Id
        $core.WaitForExit()
    }
    $env:SKIP_FCITX_USER_PATH = $previousSkipUserPath
    $env:LC_ALL = $previousLocale
}
