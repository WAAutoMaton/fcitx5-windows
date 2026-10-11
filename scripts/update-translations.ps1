[CmdletBinding()]
param([string]$GettextDirectory = 'C:/msys64/clang64/bin', [switch]$ExtractOnly)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$sources = @('win32/settings/SettingsWindow.cpp', 'win32/settings/main.cpp',
    'win32/tsf/langbaritem.cpp', 'win32/tsf/aboutwindow.cpp', 'win32/tsf/servicecontrol.cpp',
    'src/windowsinputmethods.cpp')
$template = Join-Path $root 'po/fcitx5-windows.pot'
Push-Location $root
try {
& "$GettextDirectory/xgettext.exe" --language=C++ --from-code=UTF-8 --no-wrap `
    --package-name=fcitx5-windows --keyword=tr:1c,2 --keyword=mark:1c,2 `
    --keyword=wide:1c,2 --keyword=text:1c,2 --keyword=format:1c,2 --flag=format:2:c++-format --output=$template @sources
if ($LASTEXITCODE -ne 0) { throw 'Translation extraction failed.' }
} finally { Pop-Location }
if (!$ExtractOnly) {
    foreach ($language in @('zh_CN', 'zh_TW')) {
        & "$GettextDirectory/msgmerge.exe" --update --backup=none --no-wrap "$root/po/$language.po" $template
        if ($LASTEXITCODE -ne 0) { throw "Translation update failed: $language" }
    }
}
