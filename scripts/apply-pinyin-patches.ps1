[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$lock = Get-Content "$root/cmake/pinyin-lock.json" -Raw | ConvertFrom-Json
$patches = @{
    'fcitx5' = 'fcitx5-windows-paths.patch'
    'libime' = 'libime-windows.patch'
    'chinese-addons' = @('chinese-addons-windows.patch', 'chinese-addons-windows-table.patch')
}
foreach ($repository in $patches.Keys) {
    $source = if ($repository -eq 'chinese-addons') { "$root/plugins/chinese-addons" } else { Join-Path $root $repository }
    $revision = & git -C $source rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $revision -ne $lock.repositories.$repository) {
        throw "Unexpected $repository revision. Initialize the pinned submodules first."
    }
    foreach ($patchName in @($patches[$repository])) {
        $patch = Join-Path "$root/patches" $patchName
        $previous = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & git -C $source apply --reverse --check $patch 2>$null
            $alreadyApplied = $LASTEXITCODE -eq 0
        } finally { $ErrorActionPreference = $previous }
        if ($alreadyApplied) {
            Write-Host "$repository compatibility patch is already applied: $patchName"
            continue
        }
        & git -C $source apply --check $patch
        if ($LASTEXITCODE -ne 0) {
            throw "Cannot apply $patch; preserve and inspect the existing submodule changes."
        }
        & git -C $source apply $patch
        if ($LASTEXITCODE -ne 0) { throw "Failed to apply $patch" }
    }
}
