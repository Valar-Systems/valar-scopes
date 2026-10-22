# The trace-close test (docs/weatherscope-stage3.md, Addendum 7; frozen before it runs).
# Does a bench-trace.ps1 run that ENDS NORMALLY put the s3-128 into ROM download mode (a), or does the app leave the
# network on its own (b)? A TCP watcher runs throughout; USB is touched only by the trace, the discriminator and the
# recovery. The verdict table is IN this script (A7.2), so the result is classified before anyone reads it.
#
# Run from PowerShell:  .\tools\weatherscope\trace-close\run.ps1 -OutDir <dir>
param(
    [Parameter(Mandatory=$true)][string]$OutDir,
    [string]$Serial = '907069326E64',
    [string]$Name = 'weatherscope-326e64.local',
    # Classify an existing run's watch.csv + events.csv in OutDir without touching the board (and the selftest's input).
    [switch]$AnalyseOnly
)
$ErrorActionPreference = 'Stop'
$root   = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$py     = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$esptool = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$watchCsv = Join-Path $OutDir 'watch.csv'
$events   = Join-Path $OutDir 'events.csv'
$stopFile = Join-Path $OutDir 'watch.stop'
$verdictNote = ''
if (-not $AnalyseOnly) {
Remove-Item $stopFile -ErrorAction SilentlyContinue
"utc,event,detail" | Set-Content $events

function Now { (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss.fffZ') }
function Event($e, $d = '') { "$(Now),$e,$($d -replace ',',';')" | Add-Content $events; Write-Host "$(Now) $e $d" }
function Port {
    $p = (& python -I (Join-Path $root 'scripts\board-port.py') weatherscope --upload | Select-Object -First 1).Trim()
    if ($p -notmatch '^COM\d+$') { throw "serial $Serial did not resolve to a port: '$p'" }
    $p
}
function Esptool([string[]]$a) {
    $out = & $py $esptool --port (Port) @a 2>&1 | Out-String
    return @{ rc = $LASTEXITCODE; out = $out }
}
function Tcp($ip) {
    $c = New-Object System.Net.Sockets.TcpClient
    try { $t = $c.ConnectAsync($ip, 80); return ($t.Wait(1500) -and $c.Connected) } catch { return $false } finally { $c.Dispose() }
}

# ---- the address, pinned for the run ------------------------------------------------------------------------------
$ip = (Resolve-DnsName -Name $Name -Type A -ErrorAction Stop | Select-Object -First 1).IPAddress
Event 'address' "$Name -> $ip"

# ---- the watcher: TCP :80 every 2 s, never USB --------------------------------------------------------------------
"utc,ok" | Set-Content $watchCsv
$watcher = Start-Job -ArgumentList $ip, $watchCsv, $stopFile -ScriptBlock {
    param($ip, $csv, $stop)
    while (-not (Test-Path $stop)) {
        $t0 = Get-Date
        $c = New-Object System.Net.Sockets.TcpClient
        $ok = $false
        try { $task = $c.ConnectAsync($ip, 80); $ok = $task.Wait(1500) -and $c.Connected } catch { $ok = $false } finally { $c.Dispose() }
        "$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss.fffZ')),$([int]$ok)" | Add-Content $csv
        $left = 2000 - ((Get-Date) - $t0).TotalMilliseconds
        if ($left -gt 0) { Start-Sleep -Milliseconds $left }
    }
}
Event 'watcher' "started job $($watcher.Id)"

function Watch($seconds) { Start-Sleep -Seconds $seconds; if ($watcher.State -ne 'Running') { throw 'the watcher stopped' } }
function DownNow { $l = @(Import-Csv $watchCsv | Select-Object -Last 3); ($l.Count -eq 3) -and (@($l | Where-Object { $_.ok -eq '1' }).Count -eq 0) }
function WaitUp($seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) { if (Tcp $ip) { return $true }; Start-Sleep -Seconds 2 }
    return $false
}
function OnDrop($label) {
    $d = Esptool @('--before', 'no-reset', '--after', 'no-reset', 'read-mac')
    $connects = ($d.rc -eq 0) -and ($d.out -match 'MAC:')
    Event "discriminator" "$label connects=$connects rc=$($d.rc)"
    $d.out | Set-Content (Join-Path $OutDir "discriminator-$label.txt")
    $r = Esptool @('read-mac')
    Event 'recovery' "$label read-mac rc=$($r.rc)"
    $r.out | Set-Content (Join-Path $OutDir "recovery-$label.txt")
    $up = WaitUp 90
    Event 'recovered' "$label up=$up"
    if (-not $up) { throw "the board did not come back within 90 s after $label's recovery" }
    return $connects
}

try {
    # ---- 1. the precondition, read twice -------------------------------------------------------------------------
    foreach ($n in 1, 2) {
        $code = try { (Invoke-WebRequest -Uri "http://$ip/" -TimeoutSec 15 -UseBasicParsing).StatusCode } catch { 0 }
        Event 'precondition' "GET / #$n = $code"
        if ($code -ne 200) { throw "precondition: GET / #$n returned $code" }
        if ($n -eq 1) { Start-Sleep -Seconds 30 }
    }

    # ---- 2. C: the control for (b), three untouched windows --------------------------------------------------------
    foreach ($w in 1..3) {
        Event 'C-start' "window $w"
        Watch 180
        Event 'C-end' "window $w"
        if (DownNow) { $null = OnDrop "C$w"; throw "a drop in control window $w (outcome iii)" }
    }

    # ---- 3. T: three trials, one normally-ending trace each, then 120 s watched -----------------------------------
    foreach ($t in 1..3) {
        $p = Port
        Event 'T-trace-open' "trial $t port=$p"
        & (Join-Path $root 'scripts\bench-trace.ps1') -Port $p -Label "tc-T$t" -Minutes 1 -OutDir $OutDir | Out-Null
        Event 'T-trace-closed' "trial $t"
        Watch 120
        Event 'T-end' "trial $t"
        if (DownNow) { $null = OnDrop "T$t" }
    }

    # ---- 4. D: the discriminator's own control, while the board answers ------------------------------------------
    if (-not (Tcp $ip)) { throw 'D: the board is not up before its control' }
    $d = Esptool @('--before', 'no-reset', '--after', 'no-reset', 'read-mac')
    $dConnects = ($d.rc -eq 0) -and ($d.out -match 'MAC:')
    $d.out | Set-Content (Join-Path $OutDir 'discriminator-D.txt')
    Event 'D' "connects=$dConnects rc=$($d.rc)"
    Watch 120
    Event 'D-end' ''
    if (DownNow) { $null = OnDrop 'D-after' }
}
catch { $verdictNote = "STOPPED: $($_.Exception.Message)"; Event 'stopped' $_.Exception.Message }
finally {
    New-Item -ItemType File $stopFile -Force | Out-Null
    Start-Sleep -Seconds 4
    Stop-Job $watcher -ErrorAction SilentlyContinue; Remove-Job $watcher -Force -ErrorAction SilentlyContinue
}

}  # end of the run (skipped by -AnalyseOnly)

# ---- the reading: drops (>= 3 consecutive failed polls) placed against the phase events ---------------------------
$polls = @(Import-Csv $watchCsv | ForEach-Object { [pscustomobject]@{ t = [datetime]::Parse($_.utc).ToUniversalTime(); ok = $_.ok -eq '1' } })
$drops = @(); $run = @()
foreach ($p in $polls + @([pscustomobject]@{ t = [datetime]::MaxValue; ok = $true })) {
    if (-not $p.ok) { $run += $p } else { if ($run.Count -ge 3) { $drops += [pscustomobject]@{ start = $run[0].t; end = $run[-1].t; polls = $run.Count } }; $run = @() }
}
$ev = @(Import-Csv $events | ForEach-Object { [pscustomobject]@{ t = [datetime]::Parse($_.utc).ToUniversalTime(); e = $_.event; d = $_.detail } })
function PhaseOf($time) {
    $last = $ev | Where-Object { $_.t -le $time -and $_.e -match '^(C-start|T-trace-open|T-trace-closed|D|recovery)$' } | Select-Object -Last 1
    if ($last) { "$($last.e) $($last.d)" } else { 'before the phases' }
}
"`npolls: $($polls.Count); drops (>= 3 consecutive failed polls): $($drops.Count)"
foreach ($x in $drops) { "  drop $($x.start.ToString('HH:mm:ss'))Z-$($x.end.ToString('HH:mm:ss'))Z ($($x.polls) polls) after: $(PhaseOf $x.start)" }

# Drops caused by our own recovery reset are expected (a hard reset takes the board off the network while it boots);
# only drops that START before a recovery in their phase are graded.
$graded = @($drops | Where-Object { (PhaseOf $_.start) -notmatch '^recovery' })
$inC  = @($graded | Where-Object { (PhaseOf $_.start) -match '^C-start' })
$inTopen = @($graded | Where-Object { (PhaseOf $_.start) -match '^T-trace-open' })
$inT  = @($graded | Where-Object { (PhaseOf $_.start) -match '^T-trace-closed' })
$disc = @($ev | Where-Object { $_.e -eq 'discriminator' -and $_.d -match '^T' })
$dEv  = $ev | Where-Object { $_.e -eq 'D' } | Select-Object -Last 1

$verdict =
    if ($inC.Count -gt 0) { '(iii) a drop with no trace: (b) is real. STOP.' }
    elseif ($verdictNote) { "(v) the instrument or an unforeseen case -- $verdictNote. Stop and decide." }
    elseif ($inTopen.Count -gt 0) { '(v) a drop while a trace was open. Stop and decide.' }
    elseif (-not $dEv -or $dEv.d -notmatch 'connects=False') { '(v) D connected (or did not run): the discriminator is not what it claims. Stop and decide.' }
    elseif ($inT.Count -gt 0 -and @($disc | Where-Object { $_.d -match 'connects=False' }).Count -gt 0) { '(iv) a drop after a close, not in the ROM loader. Stop and decide.' }
    elseif ($inT.Count -gt 0 -and $disc.Count -ge 1 -and @($disc | Where-Object { $_.d -match 'connects=True' }).Count -eq $disc.Count) { '(i) (a) confirmed: a normal trace close can put this board into download mode.' }
    elseif ($inT.Count -gt 0) { '(v) a drop after a close with no discriminator reading. Stop and decide.' }
    else { '(ii) not reproduced in 3 closes, none in the control; the 10-21 event stays open.' }
"`nC drops: $($inC.Count); drops while a trace was open: $($inTopen.Count); T drops after a close: $($inT.Count)"
"T discriminators: $(@($disc | ForEach-Object { $_.d }) -join ' | ')"
"D: $($dEv.d)"
"VERDICT (A7.2): $verdict"
