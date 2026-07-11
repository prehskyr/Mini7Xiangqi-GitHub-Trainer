$ErrorActionPreference = 'Stop'
$engine = Join-Path $PSScriptRoot '..\engine\fairy-stockfish.exe'
$expected = 'f894e6db3e5f2842da57dbeab33505aabf976f55afccd30bb87c78cb8bcf2bb3'
if (!(Test-Path $engine)) {
    throw "Missing engine/fairy-stockfish.exe"
}
$actual = (Get-FileHash $engine -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $expected) {
    throw "Fairy-Stockfish SHA-256 mismatch. Expected $expected, got $actual. Update scripts/verify-engine.ps1 only after intentionally replacing the engine."
}
Write-Host "Fairy-Stockfish binary verified: $actual"
