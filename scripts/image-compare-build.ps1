<#
image-compare-build.ps1 -- one build for #656's comparison (docs/image-compare.md, section 2): a store that
scripts/image-compare.py reads.

  pwsh scripts/image-compare-build.ps1 -Tree <worktree> -PioEnv <env> -Out <new store dir>

What it does, in order, and where it stops:
  1. records the inputs: the commit, the tree's porcelain (a plant shows here), pio's version, platformio.ini's sha256;
  2. deletes .pio/build/<env> and builds it from clean with SOURCE_DATE_EPOCH fixed (T0 is read on the store);
  3. hashes every object and archive, then RELINKS the same objects with -Wl,-Map= added (PLATFORMIO_BUILD_FLAGS);
  4. M0: the objects must hash the same after the relink, and the relinked ELF must be byte-identical to the first.
     Either one false: it stops (exit 3), and nothing is compared;
  5. copies the ELF, the map, every input under the tree, and inputs.json (each LOAD line: its sha256, its copy,
     and its absolute path) into the store.
Run it from PowerShell, never from Git Bash: pio under Git Bash can gut esptool on this machine.
#>
param(
    [Parameter(Mandatory = $true)] [string]$Tree,
    [Parameter(Mandatory = $true)] [string]$PioEnv,
    [Parameter(Mandatory = $true)] [string]$Out,
    [string]$Epoch = '1767225600'   # 2026-01-01T00:00:00Z: __DATE__ "Jan  1 2026", __TIME__ "00:00:00"
)
$ErrorActionPreference = 'Stop'

function Sha([string]$p) { (Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLowerInvariant() }
function Stop([int]$code, [string]$why) { Write-Host "image-compare-build: STOP: $why"; exit $code }

$Tree = (Resolve-Path -LiteralPath $Tree).Path
if (Test-Path -LiteralPath $Out) { Stop 2 "the store $Out already exists; a store is written once" }
New-Item -ItemType Directory -Path $Out | Out-Null
$Out = (Resolve-Path -LiteralPath $Out).Path
$build = Join-Path $Tree ".pio\build\$PioEnv"

# 1. the inputs, before
$record = [ordered]@{
    env           = $PioEnv
    commit        = (git -C $Tree rev-parse HEAD).Trim()
    porcelain     = @(git -C $Tree status --porcelain)
    pio           = (pio --version).Trim()
    platformioIni = Sha (Join-Path $Tree 'platformio.ini')
    epoch         = $Epoch
    started       = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
}

# 2. from clean, with the stamp fixed
if (Test-Path -LiteralPath $build) { Remove-Item -LiteralPath $build -Recurse -Force }
$env:SOURCE_DATE_EPOCH = $Epoch
Remove-Item Env:PLATFORMIO_BUILD_FLAGS -ErrorAction SilentlyContinue
pio run -d $Tree -e $PioEnv *> (Join-Path $Out 'build.log')
if ($LASTEXITCODE -ne 0) { Stop 2 "the build failed (exit $LASTEXITCODE); see build.log" }
$elf = Join-Path $build 'firmware.elf'
Copy-Item -LiteralPath $elf -Destination (Join-Path $Out 'firmware.first.elf')

# 3. the same objects, relinked with a map
$objs = Get-ChildItem -LiteralPath $build -Recurse -File | Where-Object { $_.Extension -in '.o', '.a' }
$before = @{}; foreach ($o in $objs) { $before[$o.FullName] = Sha $o.FullName }
$map = Join-Path $Out 'firmware.map'
$env:PLATFORMIO_BUILD_FLAGS = "-Wl,-Map=$($map -replace '\\', '/')"
try {
    pio run -d $Tree -e $PioEnv *> (Join-Path $Out 'relink.log')
    $relinkExit = $LASTEXITCODE
} finally {
    Remove-Item Env:PLATFORMIO_BUILD_FLAGS -ErrorAction SilentlyContinue
}
if ($relinkExit -ne 0) { Stop 2 "the relink failed (exit $relinkExit); see relink.log" }
if (-not (Test-Path -LiteralPath $map)) { Stop 3 "M0: the relink wrote no map" }

# 4. M0
$after = Get-ChildItem -LiteralPath $build -Recurse -File | Where-Object { $_.Extension -in '.o', '.a' }
$moved = @($after | Where-Object { $before[$_.FullName] -ne (Sha $_.FullName) }).Count
$moved += [Math]::Abs($after.Count - $before.Count)
$same = (Sha $elf) -eq (Sha (Join-Path $Out 'firmware.first.elf'))
$record.m0 = [ordered]@{ objectsChanged = $moved; elfIdentical = $same }
if ($moved -ne 0) { Stop 3 "M0: $moved objects or archives changed across the relink" }
if (-not $same) { Stop 3 "M0: the relinked ELF is not byte-identical to the first" }
Copy-Item -LiteralPath $elf -Destination (Join-Path $Out 'firmware.elf')
Remove-Item -LiteralPath (Join-Path $Out 'firmware.first.elf')

# 5. every input the map loads: hashed, and copied if it lives in the tree
$inputs = @()
foreach ($line in Get-Content -LiteralPath $map) {
    if ($line -notmatch '^LOAD (.+)$') { continue }
    $p = $Matches[1].Trim()
    if ($p -eq 'linker stubs') { continue }
    $abs = if ([System.IO.Path]::IsPathRooted($p)) { $p } else { Join-Path $Tree $p }
    $abs = [System.IO.Path]::GetFullPath($abs)
    $copy = $null
    if ($abs.StartsWith($Tree, [System.StringComparison]::OrdinalIgnoreCase)) {
        $copy = 'inputs/' + ($abs.Substring($Tree.Length).TrimStart('\', '/') -replace '\\', '/')
        $dst = Join-Path $Out $copy
        New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
        Copy-Item -LiteralPath $abs -Destination $dst
    }
    $inputs += [ordered]@{ path = $p; sha256 = (Sha $abs); copy = $copy; abs = $abs }
}
@{ inputs = $inputs } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Out 'inputs.json') -Encoding utf8
$record.finished = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
$record.inputs = $inputs.Count
$record | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Out 'record.json') -Encoding utf8
Write-Host ("image-compare-build: {0} {1} -> {2}: M0 held (0 objects changed, ELF identical); {3} inputs" -f `
        $PioEnv, $record.commit.Substring(0, 12), $Out, $inputs.Count)
exit 0
