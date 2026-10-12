# Collect one D6 bench sitting into ONE folder, and say what is MISSING.
#
# WHY. The sitting (docs/bench-runbook-device-game.md, docs/bench-d6-checklist.md)
# leaves its evidence in four places: the captures and their sidecars in
# bench-logs\, the flash logs beside them, the image manifests under
# C:\Github\missileer-overnight\d6\images, and the filled checklist. A capture
# that cannot be tied to the image that produced it is not a fixture, and an
# expected capture that is quietly absent reads as "nothing happened". So this
# copies everything into bench-logs\d6-<date>\ and REPORTS every expected item it
# did not find -- it never skips one silently.
#
# WHAT IT NEVER DOES: open a serial port (file operations only), or read key or
# provisioning material. It copies only the name patterns below, and refuses any
# file whose name looks like provisioning/key/credential material even if a
# pattern matched it.
#
# USAGE (from the repo root; Windows PowerShell 5.1 or pwsh 7)
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\d6-collect-logs.ps1
#   ... -OutDir <path>          default bench-logs\d6-<yyyy-MM-dd>
#   ... -Since '2026-10-12 08:00'   only files written since then (default: last 24 h)
#   ... -Checklist <path>       the filled checklist (default: docs\bench-d6-checklist.md,
#                               plus the printable copy beside the images if it was edited)
#   ... -BenchCaptureDir <dir>  ALSO search bench-capture.ps1's folder for d6-* ledgers. Only
#                               needed if a flash ran WITHOUT -NoCapture; that recorder writes
#                               outside this repo. Not searched unless asked, and said so.
#   ... -Station2Skipped        station 2 was skipped by ruling: its absence is expected
#   ... -SelfTest               builds a fake sitting in a temp dir and checks the report
#
# EXIT  0 everything expected was found   1 something expected is missing   2 usage
param(
    [string]$RepoRoot = '',
    [string]$ImagesRoot = 'C:\Github\missileer-overnight\d6\images',
    [string]$OutDir = '',
    [string]$Since = '',
    [string]$Checklist = '',
    [string]$BenchCaptureDir = '',
    [switch]$Station2Skipped,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

# Names that are never copied, whatever matched them (CLAUDE.md: provisioning output
# and keys stay local; this collector has no business reading them).
$Deny = @('*provisioned*', '*provision-log*', '*credentials*.json', '*.key', '*.pem', '*secret*', '*device-key*')

function Test-Denied([string]$name) {
    foreach ($d in $Deny) { if ($name -like $d) { return $true } }
    return $false
}

function Invoke-Collect {
    param([string]$Repo, [string]$Images, [string]$Out, [datetime]$From, [string[]]$Checklists,
          [string]$CaptureDir, [bool]$SkipStation2)

    $logs = Join-Path $Repo 'bench-logs'
    $found = New-Object System.Collections.Generic.List[string]
    $missing = New-Object System.Collections.Generic.List[string]
    $notes = New-Object System.Collections.Generic.List[string]
    New-Item -ItemType Directory -Force -Path $Out | Out-Null
    foreach ($sub in 'captures', 'flash-logs', 'images', 'checklist') {
        New-Item -ItemType Directory -Force -Path (Join-Path $Out $sub) | Out-Null
    }

    function Copy-In([System.IO.FileInfo]$f, [string]$sub) {
        if (Test-Denied $f.Name) { $notes.Add("REFUSED to copy $($f.FullName) (provisioning/key-like name)"); return }
        $dest = Join-Path (Join-Path $Out $sub) $f.Name
        Copy-Item -LiteralPath $f.FullName -Destination $dest -Force
        $found.Add("$sub\$($f.Name)")
    }

    function Get-Recent([string]$dir, [string]$pattern) {
        if (-not (Test-Path -LiteralPath $dir)) { return @() }
        return @(Get-ChildItem -LiteralPath $dir -File -Filter $pattern -ErrorAction SilentlyContinue |
                 Where-Object { $_.LastWriteTime -ge $From } | Sort-Object LastWriteTime)
    }

    # ---- 1. captures, per station, each with its sidecar --------------------------
    $stations = @(
        @{ Key = 'station 1 gesture';      Patterns = @('gesture-*.csv') },
        @{ Key = 'station 2 screenstate';  Patterns = @('screenstate-*.log') },
        @{ Key = 'station 3 blit';         Patterns = @('blit-*.log') },
        @{ Key = 'station 4 arm A';        Patterns = @('armA-*.log', 'arm1-*.log') },
        @{ Key = 'station 4 arm B';        Patterns = @('armB-*.log', 'arm2-*.log') },
        @{ Key = 'station 4 arm C';        Patterns = @('armC-*.log', 'arm3-*.log') }
    )
    foreach ($s in $stations) {
        $hits = @()
        foreach ($p in $s.Patterns) { $hits += Get-Recent $logs $p }
        if ($hits.Count -eq 0) {
            if ($s.Key -eq 'station 2 screenstate' -and $SkipStation2) {
                $notes.Add("station 2 skipped by ruling (-Station2Skipped): no capture expected")
            } else {
                $missing.Add("$($s.Key): no capture ($($s.Patterns -join ' or ') in $logs since $From)")
            }
            continue
        }
        if ($s.Key -like 'station 4*' -and ($hits | Where-Object { $_.Name -like 'arm[123]-*' })) {
            $notes.Add("$($s.Key): named arm1/2/3 -- the arm LETTER is in the capture's REG,arm line; check the mapping (checklist Q2)")
        }
        if ($hits.Count -gt 1) { $notes.Add("$($s.Key): $($hits.Count) captures (a redo?) -- all copied; the checklist says which one counts") }
        foreach ($h in $hits) {
            Copy-In $h 'captures'
            if (Test-Denied $h.Name) { continue }   # refused above; no sidecar talk about it
            $meta = Get-Item -LiteralPath ($h.FullName + '.meta.txt') -ErrorAction SilentlyContinue
            if ($meta) { Copy-In $meta 'captures' }
            else { $missing.Add("$($s.Key): $($h.Name) has no .meta.txt sidecar (not captured with d6-capture.py?) -- it cannot be tied to an image") }
        }
    }
    # Any other sidecar from the window (e.g. a capture that NEVER OPENED) is evidence too.
    foreach ($m in (Get-Recent $logs '*.meta.txt')) {
        if (-not ($found -contains "captures\$($m.Name)")) { Copy-In $m 'captures'; $notes.Add("extra sidecar: $($m.Name)") }
    }

    # ---- 2. flash logs: d6-flash-image.py and bench-flash.ps1 both write flash-<label>-<stamp>.txt
    $flashExpect = @('d6-station1-gesture', 'd6-station2-screenstate*', 'd6-station3-blit',
                     'd6-station4-armA', 'd6-station4-armB', 'd6-station4-armC')
    foreach ($label in $flashExpect) {
        $hits = Get-Recent $logs "flash-$label-*.txt"
        if ($hits.Count -eq 0) {
            if ($label -like 'd6-station2*' -and $SkipStation2) { continue }
            $missing.Add("flash log for $label (flash-$label-*.txt in $logs)")
        }
        foreach ($h in $hits) { Copy-In $h 'flash-logs' }
    }
    foreach ($h in (Get-Recent $logs 'flash-*.txt')) {
        if (-not ($found -contains "flash-logs\$($h.Name)")) { Copy-In $h 'flash-logs'; $notes.Add("other flash log in the window: $($h.Name)") }
    }

    # ---- 3. bench-capture.ps1's ledgers, ONLY if asked ------------------------------
    if ($CaptureDir) {
        $hits = Get-Recent $CaptureDir 'd6-*.log'
        if ($hits.Count -eq 0) { $notes.Add("searched $CaptureDir for d6-*.log: none") }
        foreach ($h in $hits) { Copy-In $h 'captures' }
    } else {
        $notes.Add("NOT searched: bench-capture.ps1's folder (it writes outside this repo). Only matters if a flash ran without -NoCapture; then re-run with -BenchCaptureDir <that folder>.")
    }

    # ---- 4. image manifests --------------------------------------------------------
    if (-not (Test-Path -LiteralPath $Images)) {
        $missing.Add("image manifests: $Images does not exist")
    } else {
        foreach ($dir in @(Get-ChildItem -LiteralPath $Images -Directory)) {
            $dest = Join-Path (Join-Path $Out 'images') $dir.Name
            New-Item -ItemType Directory -Force -Path $dest | Out-Null
            foreach ($n in 'MANIFEST.txt', 'MANIFEST.json') {
                $f = Join-Path $dir.FullName $n
                if (Test-Path -LiteralPath $f) { Copy-Item -LiteralPath $f -Destination $dest -Force; $found.Add("images\$($dir.Name)\$n") }
                else { $missing.Add("image manifest $($dir.Name)\$n") }
            }
        }
    }

    # ---- 5. the checklist ------------------------------------------------------------
    $anyChecklist = $false
    foreach ($c in $Checklists) {
        if (-not $c -or -not (Test-Path -LiteralPath $c)) { continue }
        $anyChecklist = $true
        $f = Get-Item -LiteralPath $c
        $dest = Join-Path (Join-Path $Out 'checklist') $f.Name
        if (Test-Path -LiteralPath $dest) { $dest = Join-Path (Join-Path $Out 'checklist') ("{0}-{1}" -f $f.Directory.Name, $f.Name) }
        Copy-Item -LiteralPath $f.FullName -Destination $dest -Force
        $found.Add("checklist\$(Split-Path $dest -Leaf)")
        $blanks = @(Select-String -LiteralPath $f.FullName -Pattern '_{4,}' -AllMatches).Count
        $notes.Add("checklist $($f.Name): $blanks line(s) still holding a ____ blank")
    }
    if (-not $anyChecklist) { $missing.Add("the filled checklist (looked at: $($Checklists -join ', '))") }

    # ---- 6. which code ----------------------------------------------------------------
    $gitTxt = Join-Path $Out 'git.txt'
    $git = Get-Command git -ErrorAction SilentlyContinue
    if ($git -and (Test-Path -LiteralPath (Join-Path $Repo '.git'))) {
        $lines = @()
        $lines += "repo        $Repo"
        $lines += "HEAD        " + (& git -C $Repo rev-parse HEAD 2>&1)
        $lines += "branch      " + (& git -C $Repo rev-parse --abbrev-ref HEAD 2>&1)
        $lines += "origin/main " + (& git -C $Repo rev-parse origin/main 2>&1)
        $lines += "status (tracked changes only; untracked bench-logs are the sitting itself):"
        $lines += @(& git -C $Repo status --short --untracked-files=no 2>&1)
        Set-Content -LiteralPath $gitTxt -Value $lines -Encoding UTF8
        $found.Add('git.txt')
    } else {
        $missing.Add("git rev-parse HEAD: $Repo is not a git checkout (or git is not on PATH)")
    }

    # ---- 7. the report ------------------------------------------------------------------
    $report = @()
    $report += "D6 bench sitting -- collected $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') (files written since $From)"
    $report += "into $Out"
    $report += ""
    $report += "FOUND ($($found.Count)):"
    foreach ($f in $found) {
        $p = Join-Path $Out $f
        $h = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLower().Substring(0, 16)
        $report += "  $f  sha256 $h"
    }
    $report += ""
    $report += "MISSING ($($missing.Count)):"
    if ($missing.Count -eq 0) { $report += "  (nothing)" } else { foreach ($m in $missing) { $report += "  $m" } }
    $report += ""
    $report += "NOTES:"
    foreach ($n in $notes) { $report += "  $n" }
    Set-Content -LiteralPath (Join-Path $Out 'COLLECTED.txt') -Value $report -Encoding UTF8
    $report | ForEach-Object { Write-Host $_ }
    return $missing.Count
}

function Invoke-SelfTest {
    $t = Join-Path ([System.IO.Path]::GetTempPath()) ("d6-collect-selftest-" + [guid]::NewGuid().ToString('N'))
    $repo = Join-Path $t 'repo'; $imgs = Join-Path $t 'images'; $logs = Join-Path $repo 'bench-logs'
    New-Item -ItemType Directory -Force -Path $logs, (Join-Path $repo 'docs'), (Join-Path $imgs 'station1-gesture') | Out-Null
    $bad = 0
    function Check([string]$name, [bool]$ok) {
        if ($ok) { Write-Host "  PASS  $name" } else { Write-Host "  FAIL  $name"; $script:bad++ }
    }
    'seq,t_us' | Set-Content (Join-Path $logs 'gesture-20261012-0810.csv')
    'facts'    | Set-Content (Join-Path $logs 'gesture-20261012-0810.csv.meta.txt')
    'blit'     | Set-Content (Join-Path $logs 'blit-20261012-0830.log')            # no sidecar
    'REG,arm'  | Set-Content (Join-Path $logs 'armA-20261012-0840.log')
    'x'        | Set-Content (Join-Path $logs 'armA-20261012-0840.log.meta.txt')
    'flash'    | Set-Content (Join-Path $logs 'flash-d6-station1-gesture-2026-10-12-0805.txt')
    'old'      | Set-Content (Join-Path $logs 'screenstate-20200101-0000.log')
    (Get-Item (Join-Path $logs 'screenstate-20200101-0000.log')).LastWriteTime = [datetime]'2020-01-01'
    'serial,mac' | Set-Content (Join-Path $logs 'gesture-provisioned-20261012.csv')  # matches gesture-*.csv; must never be copied
    'm'        | Set-Content (Join-Path $imgs 'station1-gesture\MANIFEST.txt')
    '{}'       | Set-Content (Join-Path $imgs 'station1-gesture\MANIFEST.json')
    "| a | ____ |" | Set-Content (Join-Path $repo 'docs\bench-d6-checklist.md')
    $out = Join-Path $t 'out'
    $n = Invoke-Collect -Repo $repo -Images $imgs -Out $out -From ([datetime]'2026-01-01') `
        -Checklists @((Join-Path $repo 'docs\bench-d6-checklist.md')) -CaptureDir '' -SkipStation2 $false
    $rep = Get-Content (Join-Path $out 'COLLECTED.txt') -Raw
    Check 'the gesture capture and its sidecar are copied' ((Test-Path (Join-Path $out 'captures\gesture-20261012-0810.csv')) -and (Test-Path (Join-Path $out 'captures\gesture-20261012-0810.csv.meta.txt')))
    Check 'a capture without a sidecar is reported MISSING its sidecar' ($rep -match 'blit-20261012-0830.log has no .meta.txt sidecar')
    Check 'an old screenstate capture outside the window does not count: station 2 MISSING' ($rep -match 'station 2 screenstate: no capture')
    Check 'arm B and arm C are reported MISSING' (($rep -match 'station 4 arm B: no capture') -and ($rep -match 'station 4 arm C: no capture'))
    Check 'missing flash logs are reported' ($rep -match 'flash log for d6-station3-blit')
    Check 'the flash log that exists is copied' (Test-Path (Join-Path $out 'flash-logs\flash-d6-station1-gesture-2026-10-12-0805.txt'))
    Check 'image manifests are copied' (Test-Path (Join-Path $out 'images\station1-gesture\MANIFEST.json'))
    Check 'a non-git repo is reported MISSING git' ($rep -match 'git rev-parse HEAD: .* is not a git checkout')
    Check 'the checklist is copied and its blanks counted' (($rep -match 'checklist\\bench-d6-checklist.md') -and ($rep -match '1 line\(s\) still holding'))
    Check 'a provisioning-like name is never copied, even when a pattern matches it' ((-not (Get-ChildItem -Recurse $out -Filter '*provisioned*')) -and ($rep -match 'REFUSED to copy .*gesture-provisioned'))
    Check 'bench-capture.ps1 folder: said NOT searched' ($rep -match 'NOT searched')
    Check 'exit count = number of MISSING lines' ($n -gt 0)
    $null = Invoke-Collect -Repo $repo -Images $imgs -Out (Join-Path $t 'out2') -From ([datetime]'2026-01-01') `
        -Checklists @((Join-Path $repo 'docs\bench-d6-checklist.md')) -CaptureDir '' -SkipStation2 $true
    $rep2 = Get-Content (Join-Path $t 'out2\COLLECTED.txt') -Raw
    Check '-Station2Skipped: station 2 is a NOTE, not MISSING' (($rep2 -notmatch 'station 2 screenstate: no capture') -and ($rep2 -match 'skipped by ruling'))
    Remove-Item -Recurse -Force $t
    if ($script:bad -eq 0) { Write-Host 'SELFTEST PASSED'; return 0 } else { Write-Host "SELFTEST FAILED ($script:bad)"; return 1 }
}

if ($SelfTest) { $script:bad = 0; exit (Invoke-SelfTest | Select-Object -Last 1) }

if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path }
if (-not $OutDir) { $OutDir = Join-Path $RepoRoot ('bench-logs\d6-' + (Get-Date -Format 'yyyy-MM-dd')) }
if ($Since) { $from = [datetime]::Parse($Since) } else { $from = (Get-Date).AddHours(-24) }
$checklists = @()
if ($Checklist) { $checklists += $Checklist } else {
    $checklists += (Join-Path $RepoRoot 'docs\bench-d6-checklist.md')
    $printable = Join-Path (Split-Path $ImagesRoot -Parent) 'bench-d6-checklist.md'
    $template = Join-Path $RepoRoot 'docs\bench-d6-checklist.md'
    if ((Test-Path -LiteralPath $printable) -and (Test-Path -LiteralPath $template) -and
        ((Get-FileHash -LiteralPath $printable).Hash -ne (Get-FileHash -LiteralPath $template).Hash)) {
        $checklists += $printable
    }
}
$n = Invoke-Collect -Repo $RepoRoot -Images $ImagesRoot -Out $OutDir -From $from -Checklists $checklists `
    -CaptureDir $BenchCaptureDir -SkipStation2 ([bool]$Station2Skipped)
if ($n -gt 0) { Write-Host ""; Write-Host "$n expected item(s) MISSING -- see above. Collected what exists into $OutDir"; exit 1 }
Write-Host ""; Write-Host "Everything expected was found. $OutDir"
exit 0
