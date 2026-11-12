<#
image-compare-build.ps1 -- one build for #656's comparison (docs/image-compare.md, section 2): a store that
scripts/image-compare.py reads.

  pwsh scripts/image-compare-build.ps1 -Tree <worktree> -PioEnv <env> -Out <new store dir>

What it does, in order, and where it stops:
  1. records the inputs: the commit, the tree's porcelain (a plant shows here), pio's version, platformio.ini's sha256;
  2. deletes .pio/build/<env> and builds it from clean with SOURCE_DATE_EPOCH fixed (T0 is read on the store);
  3. M0 (amendment 1b): copies the ELF and the build's OWN map (the framework maps every link), and ties them on
     content: `image-compare.py m0` must find every allocated ELF section among the map's output sections with the
     same name, address and size, and the map must be no older than the build's start. Either false: it stops (exit 3),
     and nothing is compared;
  4. copies every input under the tree, and writes inputs.json (each LOAD line: its sha256, its copy, and its absolute
     path) and record.json into the store.
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
$startUtc = (Get-Date).ToUniversalTime()
pio run -d $Tree -e $PioEnv *> (Join-Path $Out 'build.log')
if ($LASTEXITCODE -ne 0) { Stop 2 "the build failed (exit $LASTEXITCODE); see build.log" }
$elf = Join-Path $build 'firmware.elf'
$map = Join-Path $build 'firmware.map'

# 3. M0 (amendment 1b): the build's own map, written by the same link (the framework's pioarduino-build.py passes
#    -Wl,-Map=${BUILD_DIR}/${PROGNAME}.map on every link), tied to the ELF on content
if (-not (Test-Path -LiteralPath $map)) { Stop 3 "M0: the build wrote no map at $map" }
$mapAfterStart = (Get-Item -LiteralPath $map).LastWriteTimeUtc -ge $startUtc
Copy-Item -LiteralPath $elf -Destination (Join-Path $Out 'firmware.elf')
Copy-Item -LiteralPath $map -Destination (Join-Path $Out 'firmware.map')
$tie = & python (Join-Path $PSScriptRoot 'image-compare.py') m0 (Join-Path $Out 'firmware.elf') (Join-Path $Out 'firmware.map')
$tieExit = $LASTEXITCODE
$tie | Set-Content -LiteralPath (Join-Path $Out 'm0.txt') -Encoding utf8
$record.m0 = [ordered]@{ tie = ($tie | Select-Object -First 1); tieExit = $tieExit; mapAfterStart = $mapAfterStart }
if (-not $mapAfterStart) { Stop 3 "M0: the map is older than this build's start" }
if ($tieExit -ne 0) { Stop 3 "M0: the map's output sections do not tie to the ELF (exit $tieExit); see m0.txt" }

# 4. every input the map loads: hashed, and copied if it lives in the tree
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
Write-Host ("image-compare-build: {0} {1} -> {2}: M0 held ({3}); {4} inputs" -f `
        $PioEnv, $record.commit.Substring(0, 12), $Out, $record.m0.tie, $inputs.Count)
exit 0
