function Resolve-TsfDll {
    param([string]$Prefix, [string]$DllPath)

    if ($Prefix -and $DllPath) {
        throw 'Specify either -Prefix or -DllPath, not both.'
    }
    if (!$DllPath) {
        if (!$Prefix) {
            $Prefix = Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'dist/pinyin'
        }
        $DllPath = Join-Path $Prefix 'tsf/fcitx5-x86_64.dll'
    }
    $item = Get-Item -LiteralPath $DllPath -ErrorAction Stop
    if ($item.PSIsContainer -or $item.Extension -ine '.dll') {
        throw "Expected a TSF DLL file: $DllPath"
    }
    return $item.FullName
}

function Get-TsfRegsvr32 {
    $systemDirectory = if ([Environment]::Is64BitOperatingSystem -and
                           ![Environment]::Is64BitProcess) { 'Sysnative' } else { 'System32' }
    return Join-Path $env:WINDIR "$systemDirectory/regsvr32.exe"
}

function Invoke-TsfRegistration {
    param([string]$DllPath, [switch]$Unregister)

    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $principal = New-Object Security.Principal.WindowsPrincipal($identity)
        $administrator = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    } finally {
        $identity.Dispose()
    }
    $arguments = if ($Unregister) { "/s /u `"$DllPath`"" } else { "/s `"$DllPath`"" }
    $start = @{
        FilePath = Get-TsfRegsvr32
        ArgumentList = $arguments
        Wait = $true
        PassThru = $true
        WindowStyle = 'Hidden'
        ErrorAction = 'Stop'
    }
    if (!$administrator) {
        $start.Verb = 'RunAs'
    }
    $process = Start-Process @start
    if ($process.ExitCode -ne 0) {
        $operation = if ($Unregister) { 'DllUnregisterServer' } else { 'DllRegisterServer' }
        throw "$operation failed with exit code $($process.ExitCode): $DllPath"
    }
}
