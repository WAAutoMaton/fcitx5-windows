function Get-PeMachine {
    param([Parameter(Mandatory)][string]$Path)
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) { throw "Invalid PE image: $Path" }
        $stream.Position = 0x3c
        $offset = $reader.ReadUInt32()
        if ($offset -lt 64 -or $offset -gt $stream.Length - 24) { throw "Invalid PE header: $Path" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw "Invalid PE signature: $Path" }
        return $reader.ReadUInt16()
    } finally { $reader.Dispose() }
}

function Assert-PeMachine {
    param([string]$Path, [ValidateSet('x86', 'x64')][string]$Architecture)
    $expected = if ($Architecture -eq 'x86') { 0x14c } else { 0x8664 }
    $actual = Get-PeMachine $Path
    if ($actual -ne $expected) {
        throw ('PE machine mismatch: {0}; expected {1} (0x{2:x4}), got 0x{3:x4}' -f $Path, $Architecture, $expected, $actual)
    }
}
